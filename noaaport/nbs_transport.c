/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_transport.c
 * @author: Steven R. Emmerson
 *
 * This file implements the transport-layer of the NOAAPort Broadcast System
 * (NBS).
 */
#include "config.h"

#include "decode.h"
#include "ldm.h"
#include "log.h"
#include "frame_queue.h"
#include "nbs_presentation.h"
#include "nbs_transport.h"
#include "nport.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#define NBST_MAX(a, b) ((a) >= (b) ? (a) : (b))

/******************************************************************************
 * Utilities:
 ******************************************************************************/

static const uint32_t mod16 = ((uint32_t)1) << 16;
static const uint64_t mod32 = ((uint64_t)1) << 32;

static void decode_version_and_length(
        uint_fast32_t* const restrict version,
        uint_fast32_t* const restrict length,
        const uint8_t const           buf)
{
    *version = buf >> 4;
    *length = (buf & 0xF) * 4;
}

/******************************************************************************
 * NBS Transport-Layer Frame Header:
 ******************************************************************************/

/// NBS frame header
typedef struct fh {
    uint_fast32_t hdlc_address;
    uint_fast32_t hdlc_control;
    uint_fast32_t sbn_version;
    uint_fast32_t sbn_length;       ///< Length of frame header in bytes
    uint_fast32_t sbn_control;
    uint_fast32_t sbn_command;
    uint_fast32_t sbn_data_stream;
    uint_fast32_t sbn_source;
    uint_fast32_t sbn_destination;
    uint_fast32_t sbn_sequence_num;
    uint_fast32_t sbn_run;
    uint_fast32_t sbn_checksum;
} fh_t;

/**
 * Decodes the frame-header in a buffer.
 *
 * @param[out] fh                 Decoded frame-header
 * @param[in]  buf                Encoded frame header
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the decoding
 * @retval     0                  Success. `*fh` and `*nscanned` are set.
 * @retval     NBS_STATUS_INVAL   Invalid frame-header. `log_add()` called.
 */
static nbs_status_t fh_decode(
        fh_t* const restrict          fh,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    if (nbytes < 16) {
        log_add("Available bytes for frame header less than 16: %u", nbytes);
        return NBS_STATUS_INVAL;
    }

    fh->hdlc_address = buf[0];
    if (fh->hdlc_address != 255) {
        log_add("First byte of frame header not 255: %u", fh->hdlc_address);
        return NBS_STATUS_INVAL;
    }
    fh->hdlc_control = buf[1];
    decode_version_and_length(&fh->sbn_version, &fh->sbn_length, buf[2]);
    if (fh->sbn_length != 16) {
        log_add("Length of frame header not 16 bytes: %u", fh->sbn_length);
        return NBS_STATUS_INVAL;
    }

    fh->sbn_control = buf[3];
    fh->sbn_command = buf[4];
    if (fh->sbn_command != SBN_CMD_DATA && fh->sbn_command != SBN_CMD_TIME &&
            fh->sbn_command != SBN_CMD_TEST) {
        log_add("Invalid frame header command: %u", fh->sbn_command);
        return NBS_STATUS_INVAL;
    }

    fh->sbn_checksum = decode_uint16(buf+14);
    uint32_t cksum = 0;
    for (int i = 0; i < 16; i++)
        cksum += buf[i];
    if (cksum != fh->sbn_checksum) {
        log_add("Invalid frame header checksum");
        return NBS_STATUS_INVAL;
    }

    fh->sbn_data_stream = buf[5];
    fh->sbn_source = buf[6];
    fh->sbn_destination = buf[7];
    fh->sbn_sequence_num = decode_uint32(buf+8);
    fh->sbn_run = decode_uint16(buf+12);

    *nscanned = 16;
    return 0;
}

/**
 * Indicates whether or not a frame is for timing/synchronization purposes.
 *
 * @param[in] fh    Frame header
 * @retval    true  iff the frame is a timing frame
 */
static inline bool fh_is_sync_frame(
        const fh_t* const fh)
{
    return fh->sbn_command == 5;
}

/**
 * Indicates if one frame header logically follows another.
 *
 * @param[in] prev  Previous frame header
 * @param[in] curr  Current frame header
 * @return    true  iff `next` logically follows `prev`
 */
static bool fh_is_next(
        const fh_t* const restrict prev,
        const fh_t* const restrict curr)
{
    return (curr->sbn_run == prev->sbn_run &&
                curr->sbn_sequence_num == (prev->sbn_sequence_num + 1)%mod32) ||
            (curr->sbn_run == (prev->sbn_run + 1)%mod16 &&
                curr->sbn_sequence_num == 0);
}

static void fh_log_if_unexpected(
        const fh_t* const restrict prev,
        const fh_t* const restrict curr)
{
    if (!fh_is_next(prev, curr))
        log_warning("Current frame is out-of-sequence: prev_run="PRIuFAST32", "
                "prev_seqnum="PRIuFAST32", curr_run="PRIuFAST32", "
                "curr_seqnum="PRIuFAST32, prev->sbn_run,
                prev->sbn_sequence_num, curr->sbn_run, curr->sbn_sequence_num);
}

/******************************************************************************
 * NBS Transport-Layer Product-Definition Header:
 ******************************************************************************/

/// Product-definition header
typedef struct pdh {
    uint_fast32_t version;
    uint_fast32_t pdh_length;
    /**
     * Transfer-type bit-mask:
     *   0x01  Start-of-product frame
     *   0x02  Product transfer. Set in Start-of-product frame
     *   0x04  End-of-product frame
     *   0x08  Product error
     *   0x10  Data-block is zlib(3) compressed
     *   0x20  Product abort
     *   0x40  ???
     */
    uint_fast32_t trans_type;
    uint_fast32_t psh_length;
    uint_fast32_t block_num;         ///< Data-block index. Start-frame is 0
    uint_fast32_t data_offset;       ///< Not used in pmStart()
    uint_fast32_t data_size;         ///< Size of data-block in bytes
    uint_fast32_t recs_per_block;    ///< Canonical: last block may have fewer
    uint_fast32_t blocks_per_rec;
    uint_fast32_t prod_sequence_num;
} pdh_t;

/**
 * Decodes a product-definition header.
 *
 * @param[out] pdh                Product-definition header
 * @param[in]  buf                Encoded product-definition header
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the decoding
 * @retval     0                  Success. `*pdh` and `nscanned` are set.
 * @retval     NBS_STATUS_INVAL  Invalid header. log_add() called.
 */
static nbs_status_t pdh_decode(
        pdh_t* const restrict         pdh,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int            status;
    if (nbytes < 16) {
        log_add("Available bytes for product-definition header less than 16: "
                "%u", nbytes);
        status = NBS_STATUS_INVAL;
    }
    else {
        decode_version_and_length(&pdh->version, &pdh->pdh_length, buf[0]);
        if (pdh->version != 1) {
            log_add("Product-definition header version not 1: %u",
                    pdh->version);
            status = NBS_STATUS_INVAL;
        }
        else if (pdh->pdh_length < 16) {
            log_add("Product-definition header shorter than 16 bytes: %u",
                    (unsigned)pdh->pdh_length);
            status = NBS_STATUS_INVAL;
        }
        else {
            pdh->trans_type = buf[1];
            pdh->psh_length = decode_uint16(buf+2) - pdh->pdh_length;
            pdh->block_num = decode_uint16(buf+4);
            pdh->data_offset = decode_uint16(buf+6);
            pdh->data_size = decode_uint16(buf+8);
            pdh->recs_per_block = buf[10];
            pdh->blocks_per_rec = buf[11];
            pdh->prod_sequence_num = decode_uint32(buf+12);
            status = 0;
            if (pdh->pdh_length > 16) {
                if (pdh->pdh_length <= nbytes) {
                    log_notice("Product-definition header longer than 16 "
                            "bytes: %u", (unsigned)pdh->pdh_length);
                }
                else {
                    log_add("Product-definition header longer than "
                            "available bytes: length=%u, avail=%u",
                            (unsigned)pdh->pdh_length, nbytes);
                    status = NBS_STATUS_INVAL;
                }
            }
            if (status == 0)
                *nscanned = pdh->pdh_length;
        }
    }
    return status;
}

/**
 * Indicates if the current product-definition header refers to the same product
 * as the previous one.
 *
 * @param[in] prev  Previous product-definition header
 * @param[in] curr  Current product-definition header
 * @return    true  iff `curr` refers to the same product as `prev`
 */
static inline bool pdh_is_same_product(
        const pdh_t* const restrict prev,
        const pdh_t* const restrict curr)
{
    return curr->prod_sequence_num == prev->prod_sequence_num;
}

/**
 * Indicates if one product-definition header logically follows another.
 *
 * @param[in] prev  Previous product-definition header
 * @param[in] next  Next product-definition header
 * @return    true  iff `next` logically follows `prev`
 */
static bool pdh_is_next(
        const pdh_t* const restrict prev,
        const pdh_t* const restrict next)
{
    return (next->prod_sequence_num == prev->prod_sequence_num &&
                next->block_num == (prev->block_num + 1)%mod16);
}

/**
 * Indicates if the frame associated with a product-definition header is the
 * start of a product.
 *
 * @param[in] pdh   Product-definition header
 * @retval    true  iff the associated frame is the start of a product
 */
static inline bool pdh_is_product_start(
        const pdh_t* const pdh)
{
    return pdh->trans_type & 1;
}

/**
 * Indicates if the frame associated with a product-definition header is the
 * end of a product.
 *
 * @param[in] pdh   Product-definition header
 * @retval    true  iff the associated frame is the end of a product
 */
static inline bool pdh_is_product_end(
        const pdh_t* const pdh)
{
    return pdh->trans_type & 4;
}

/**
 * Indicates if the data associated with a product-definition header is
 * compressed.
 *
 * @param[in] pdh   Product-definition header
 * @retval    true  iff the associated data is compressed
 */
static inline bool pdh_is_compressed(
        const pdh_t* const pdh)
{
    return pdh->trans_type & 16;
}

/**
 * Indicates if the frame associated with a product-definition header should
 * have a product-specific header.
 *
 * @param[in] pdh   Product-definition header
 * @retval    true  iff the associated frame should have a product-specific
 *                  header
 */
static inline bool pdh_has_psh(
        const pdh_t* const pdh)
{
    return pdh->psh_length != 0;
}

/**
 * Returns the length, in bytes, of an encoded product-definition header.
 *
 * @param[in] pdh  Decoded product-definition header
 */
static inline unsigned pdh_get_length(
        const pdh_t* const pdh)
{
    return pdh->pdh_length;
}

/**
 * Returns the length, in bytes, of the product-specific header.
 *
 * @param[in] pdh  Decoded product-definition header
 */
static inline unsigned pdh_get_psh_length(
        const pdh_t* const pdh)
{
    return pdh->psh_length;
}

/**
 * Returns the offset, in bytes, to the start of the product's data from
 * the start of the NBS frame.
 *
 * @param[in] pdh  Decoded product-definition header
 * @return         Offset, in bytes, to start of the product's data from
 *                 start of NBS frame.
 */
static inline unsigned pdh_get_data_offset(
        const pdh_t* const pdh)
{
    return pdh->data_offset;
}

/**
 * Returns the amount of product data in bytes.
 *
 * @param[in] pdh  Decoded product-definition header
 * @return         Amount of product data in bytes
 */
static inline unsigned pdh_get_data_size(
        const pdh_t* const pdh)
{
    return pdh->data_size;
}

/**
 * Returns the number of records per data block.
 *
 * @param[in] pdh  Decoded product-definition header
 * @return         Amount of product data in bytes
 */
static inline unsigned pdh_get_recs_per_block(
        const pdh_t* const pdh)
{
    return pdh->recs_per_block;
}

/**
 * Returns the block number.
 *
 * @param[in] pdh  Decoded product-definition header
 * @return         Block number
 */
static inline unsigned pdh_get_block_num(
        const pdh_t* const pdh)
{
    return pdh->block_num;
}

/******************************************************************************
 * NBS Transport-Layer Product-Specific Header:
 ******************************************************************************/

/// Product-specific header
typedef struct psh {
    uint_fast32_t opt_field_num;
    uint_fast32_t opt_field_type;
    uint_fast32_t opt_field_length;
    uint_fast32_t version;
    uint_fast32_t flag;
    uint_fast32_t data_length;      ///< Length of AWIPS data-header in bytes
    uint_fast32_t bytes_per_rec;    ///< In the data-blocks of NBS frames
    uint_fast32_t prod_type;
    uint_fast32_t prod_category;
    int_fast32_t  prod_code;
    uint_fast32_t num_fragments;    ///< Includes start-frame
    uint_fast32_t next_head_offset;
    uint_fast32_t prod_seq_num;
    uint_fast32_t prod_source;
    uint_fast32_t prod_start_time;
    uint_fast32_t ncf_recv_time;
    uint_fast32_t ncf_send_time;
    uint_fast32_t proc_cntl_flag;
    uint_fast32_t put_buf_last;
    uint_fast32_t put_buf_first;
    uint_fast32_t expect_buf_num;
    uint_fast32_t prod_run_id;
} psh_t;

/**
 * Decodes a product-specific header.
 *
 * @param[out] psh                Decoded product-specific header
 * @param[in]  buf                Encoded product-specific header
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the decoding
 * @retval     0                  Success. `*psh` and `*nscanned` are set.
 * @retval     NBS_STATUS_INVAL  Invalid header. log_add() called.
 */
static nbs_status_t psh_decode(
        psh_t* const restrict         psh,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int status;
    if (nbytes < 32) {
        log_add("Product-specific header shorter than 32: %u", nbytes);
        status = NBS_STATUS_INVAL;
    }
    else {
        psh->opt_field_num = buf[0];
        psh->opt_field_type = buf[1];
        psh->opt_field_length = decode_uint16(buf+2);
        if (psh->opt_field_length > nbytes) {
            log_add("Product-specific header longer than available data: "
                    "length=%u, avail=%u", psh->opt_field_length, nbytes);
            status = NBS_STATUS_INVAL;
        }
        else {
            psh->version = buf[4];
            psh->flag = buf[5];
            psh->data_length = decode_uint16(buf+6);
            psh->bytes_per_rec = decode_uint16(buf+8);
            psh->prod_type = buf[10];
            psh->prod_category = buf[11];
            psh->prod_code = decode_uint16(buf+12);
            uint16_t x = decode_uint16(buf+14);
            psh->num_fragments = (x > INT16_MAX) ? -1 : x; // -1 => unknown
            psh->next_head_offset = decode_uint16(buf+16);
            psh->prod_seq_num = decode_uint32(buf+18);
            psh->prod_source = decode_uint16(buf+22);
            psh->prod_start_time = decode_uint32(buf+24);
            psh->ncf_recv_time = decode_uint32(buf+28);
            unsigned n = 32;
            if (nbytes >= 36) {
                psh->ncf_send_time = decode_uint32(buf+32);
                n += 4;
                if (nbytes >= 38) {
                    psh->proc_cntl_flag = decode_uint16(buf+36);
                    n += 2;
                    if (nbytes >= 40) {
                        psh->put_buf_last = decode_uint16(buf+38);
                        n += 2;
                        if (nbytes >= 42) {
                            psh->put_buf_first = decode_uint16(buf+40);
                            n += 2;
                            if (nbytes >= 44) {
                                psh->expect_buf_num = decode_uint16(buf+42);
                                n += 2;
                                if (nbytes >= 48) {
                                    psh->prod_run_id = decode_uint32(buf+44);
                                    n += 4;
                                }
                            }
                        }
                    }
                }
            }
            *nscanned = n;
            status = 0;
        }
    }
    return status;
}

/**
 * Returns the type of the product associated with a given product-specific
 * header.
 *
 * @param[in] psh      The decoded product-specific header
 * @return             The type of the associated product. One of
 *                     PROD_TYPE_GOES_EAST, PROD_TYPE_GOES_WEST,
 *                     PROD_TYPE_NESDIS_NONGOES, PROD_TYPE_NOAAPORT_OPT,
 *                     PROD_TYPE_NWSTG, or PROD_TYPE_NEXRAD.
 */
static inline unsigned psh_get_type(
        const psh_t* const psh)
{
    return psh->prod_type;
}

/**
 * Returns the number of bytes in a record.
 *
 * @param[in] pdh  Decoded product-definition header
 * @return         Number of bytes in a record
 */
static inline unsigned psh_get_rec_len(
        const psh_t* const psh)
{
    return psh->bytes_per_rec;
}

/**
 * Returns the number of fragments.
 *
 * @param[in] pdh  Decoded product-definition header
 * @return         Number of fragments
 */
static inline unsigned psh_get_num_fragments(
        const psh_t* const psh)
{
    return psh->num_fragments;
}

/******************************************************************************
 * Combined NBS Transport-Layer Product-Definition and Product-Specific Headers:
 ******************************************************************************/

/**
 * Decodes the product-specific header, if appropriate.
 *
 * @param[out] psh                Possibly decoded product-specific header
 * @param[in]  pdh                Decoded product-definition header
 * @param[in]  buf                Possibly encoded product-specific header
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the decoding
 * @retval     0                  Success. Either `buf` contained a encoded
 *                                product-specific header (in which case
 *                                `*psh` and `*nscanned` are set) or the
 *                                associated frame is after the start-of-product
 *                                and `buf` doesn't contain an encoded
 *                                product-specific header (in which case
 *                                `*nscanned` is set to `0`).
 * @retval     NBS_STATUS_INVAL  Invalid product-specific header. log_add()
 *                                called.
 */
static nbs_status_t pdhpsh_decode_psh(
        psh_t* const restrict         psh,
        const pdh_t* const restrict   pdh,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int status;
    if (!pdh_has_psh(pdh)) {
        if (!pdh_is_product_start(pdh)) {
            *nscanned = 0;
            status = 0;
        }
        else {
            log_add("Start-of-product frame doesn't have product-specific "
                    "header");
            status = NBS_STATUS_INVAL;
        }
    }
    else {
        if (!pdh_is_product_start(pdh))
            log_notice("Frame after start-of-product has product-specific "
                    "header");
        status = psh_decode(psh, buf, nbytes, nscanned);
        if (status) {
            log_add("Invalid product-specific header");
        }
        else {
            const unsigned expected = pdh_get_psh_length(pdh);
            if (*nscanned != expected) {
                log_add("Actual length of product-specific header doesn't "
                        "match expected length: actual=%u, expected=%u",
                        *nscanned, expected);
                status = NBS_STATUS_INVAL;
            }
        }
    }
    return status;
}

/**
 * Decodes the product-definition header and (possibly) the optional product-
 * specific header.
 *
 * @param[out] pdh                Decoded product-definition header
 * @param[out] psh                Possibly decoded product-specific header
 * @param[in]  buf                Encoded product-definition header
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the decoding
 * @return     0                  Success. `*pdh` and `*nscanned` are set.
 *                                `*psh` is set if appropriate.
 * @retval     NBS_STATUS_INVAL  Invalid header. `log_add()` called.
 */
static nbs_status_t pdhpsh_decode(
        pdh_t* const restrict    pdh,
        psh_t* const restrict    psh,
        const uint8_t* restrict  buf,
        unsigned                 nbytes,
        unsigned* const restrict nscanned)
{
    const uint8_t* pdh_start = buf;
    unsigned       n;
    int            status = pdh_decode(pdh, buf, nbytes, &n);
    if (status == 0) {
        buf += n;
        nbytes -= n;
        status = pdhpsh_decode_psh(psh, pdh, buf, nbytes, &n);
        if (status == 0)
            *nscanned = buf + n - pdh_start;
    }
    return status;
}

/******************************************************************************
 * Headers in an NBS Transport-Layer Frame (all of them):
 ******************************************************************************/

typedef struct {
    fh_t  fh;  ///< Frame header
    pdh_t pdh; ///< Product-definition header
    psh_t psh; ///< Product-specific header
} headers_t;

/**
 * Clears a product's headers.
 *
 * @param[in] headers  Product's headers to be cleared
 */
static inline void headers_clear(
        headers_t* const headers)
{
    (void)memset(headers, 0, sizeof(headers));
}

/**
 * Decodes (and vets) the NBS headers.
 *
 * @param[out] headers            Decoded product headers
 * @param[in]  buf                Encoded NBS frame
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the decoding
 * @retval     0                  Success. `*headers` and `*nscanned` are set.
 * @retval     NBS_STATUS_INVAL  Invalid header. `log_add()` called.
 */
static nbs_status_t headers_decode(
        headers_t* const restrict headers,
        const uint8_t* restrict   buf,
        unsigned                  nbytes,
        unsigned* const restrict  nscanned)
{
    const uint8_t* const frame_start = buf;
    const unsigned       frame_size = nbytes;
    unsigned             n;
    int                  status = fh_decode(&headers->fh, buf, nbytes, &n);
    if (status) {
        log_add("Invalid frame header");
    }
    else if (!fh_is_sync_frame(&headers->fh)) {
        buf += n;
        nbytes -= n;
        pdh_t* const pdh = &headers->pdh;
        status = pdhpsh_decode(pdh, &headers->psh, buf, nbytes, &n);
        if (status == 0) {
            unsigned data_offset = pdh_get_data_offset(pdh);
            unsigned data_size = pdh_get_data_size(pdh);
            if (data_offset + data_size > frame_size) {
                log_add("Frame too small to contain data specified: "
                        "data_offset (%u) + data_size (%u) > frame_size (%u)",
                        data_offset, data_size, frame_size);
                status = NBS_STATUS_INVAL;
            }
            else {
                *nscanned = data_offset;
            }
        }
    }
    return status;
}

/**
 * Indicates if a frame is for timing/synchronization purposes.
 *
 * @param[in] headers  The decoded headers
 * @retval    true     iff the frame is for timing/synchronization purposes
 */
static inline bool headers_is_sync_frame(
        const headers_t* const headers)
{
    return fh_is_sync_frame(&headers->fh);
}

static inline void headers_log_if_unexpected(
        const headers_t* const restrict prev,
        const headers_t* const restrict curr)
{
    fh_log_if_unexpected(&prev->fh, &curr->fh);
}

static inline bool headers_is_same_product(
        const headers_t* const restrict prev,
        const headers_t* const restrict curr)
{
    return pdh_is_same_product(&prev->pdh, &curr->pdh);
}

static inline bool headers_is_start(
        const headers_t* const restrict headers)
{
    return pdh_is_product_start(&headers->pdh);
}

static inline bool headers_is_next(
        const headers_t* const restrict prev,
        const headers_t* const restrict next)
{
    return pdh_is_next(&prev->pdh, &next->pdh);
}

/**
 * Returns the type of the product associated with given headers.
 *
 * @param[in] headers  The decoded headers
 * @return             The type of the associated product. One of
 *                     PROD_TYPE_GOES_EAST, PROD_TYPE_GOES_WEST,
 *                     PROD_TYPE_NESDIS_NONGOES, PROD_TYPE_NOAAPORT_OPT,
 *                     PROD_TYPE_NWSTG, or PROD_TYPE_NEXRAD.
 */
static inline unsigned headers_get_product_type(
        const headers_t* const headers)
{
    return psh_get_type(&headers->psh);
}

/******************************************************************************
 * NBS Transport Layer:
 ******************************************************************************/

struct nbst {
    fq_t*     fq;              ///< Queue from which to read NBS frames
    nbsp_t*   nbsp;            ///< NBS presentation-layer object
    headers_t headers[2];      ///< Previous and current NBS transport-layer
                               ///< headers
    unsigned  curr_header;     ///< Current header index
#define CURR_HEADER(nbst)    nbst->headers[nbst->curr_header]
#define PREV_HEADER(nbst)    nbst->headers[!nbst->curr_header]
#define SWITCH_HEADERS(nbst) (nbst->curr_header = !nbst->curr_header)
    bool      start_processed; ///< Has the start of a product been processed?
};

/**
 * Returns a new NBS transport-layer object.
 *
 * @param[out] nbst               New NBS transport-layer object
 * @param[in]  fq                 Frame-queue from which to read NBS frames.
 *                                Will _not_ be freed by nbst_free().
 * @param[in]  nbsp               NBS Presentation-layer object
 * @retval     0                  Success. `*nbst` is set.
 * @retval     NBS_STATUS_INVAL  `fq == NULL || nbst == NULL`. log_add()
 *                                called.
 * @retval     NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
static nbs_status_t nbst_new(
        nbst_t** const restrict nbst,
        fq_t* const restrict    fq,
        nbsp_t* const restrict  nbsp)
{
    int status;
    if (nbst == NULL || fq == NULL || nbsp == NULL) {
        log_add("Invalid argument: nbst=%p, fq=%p, nbsp=%p", nbst, fq, nbsp);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbst_t* const ptr = log_malloc(sizeof(nbst_t),
                "NBS transport-layer object");
        if (ptr == NULL) {
            status = NBS_STATUS_NOMEM;
        }
        else {
            ptr->fq = fq;
            ptr->nbsp = nbsp;
            headers_clear(ptr->headers);
            headers_clear(ptr->headers+1);
            ptr->curr_header = 0;
            ptr->start_processed = false;
            *nbst = ptr;
            status = 0;
        }
    }
    return status;
}

/**
 * Processes the data-block of an NBS frame.
 *
 * @param[in] nbst                NBS transport-layer object
 * @param[in] headers             Decoded product headers
 * @param[in] buf                 Product data
 * @retval    0                   Success
 * @retval    NBS_STATUS_INVAL   `headers` or `buf` is invalid. log_add()
 *                                called.
 * @retval    NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval    NBS_STATUS_UNSUPP  Unsupported product. log_add() called.
 * @retval    NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
static nbs_status_t nbst_process_data(
        nbst_t* const restrict          nbst,
        const uint8_t* const restrict   buf)
{
    nbsp_t* const          nbsp = nbst->nbsp;
    const headers_t* const headers = &CURR_HEADER(nbst);
    const unsigned         type = headers_get_product_type(headers);
    const pdh_t* const     pdh = &headers->pdh;
    const psh_t* const     psh = &headers->psh;
    const unsigned         nbytes = pdh_get_data_size(pdh);
    const bool             is_start = pdh_is_product_start(pdh);
    const bool             is_end = pdh_is_product_end(pdh);
    const bool             is_compressed = pdh_is_compressed(pdh);
    switch (type) {
        case PROD_TYPE_GOES_EAST:
        case PROD_TYPE_GOES_WEST: {
            int num_frag = psh_get_num_fragments(psh);
            num_frag = NBST_MAX(num_frag, 1); // `num_frag` could be `-1`
            int status = is_start
                    ? nbsp_gini_start(nbsp, buf, nbytes, psh_get_rec_len(psh),
                            pdh_get_recs_per_block(pdh), is_compressed,
                            psh_get_type(psh),
                            num_frag * 5120) // Canonical data-block max size
                    : nbsp_gini_block(nbsp, buf, nbytes,
                            pdh_get_block_num(pdh), is_compressed);
            return (status == NBS_STATUS_INVAL || status == NBS_STATUS_LOGIC ||
                    status == 0)
                      ? status
                      : NBS_STATUS_SYSTEM;
        }

        case PROD_TYPE_NESDIS_NONGOES: // also PROD_TYPE_NOAAPORT_OPT
            return nbsp_nongoes(nbsp, buf, nbytes, is_start, is_end,
                    is_compressed) ? NBS_STATUS_SYSTEM : 0;

        case PROD_TYPE_NWSTG:
            return nbsp_nwstg(nbsp, buf, nbytes, is_start, is_end)
                    ? NBS_STATUS_SYSTEM : 0;

        case PROD_TYPE_NEXRAD:
            return nbsp_nexrad(nbsp, buf, nbytes, is_start, is_end)
                    ? NBS_STATUS_SYSTEM : 0;

        default:
            log_add("Unsupported product type: %u", type);
            return NBS_STATUS_UNSUPP;
    }
}

/**
 * Process a non-synchronization frame.
 *
 * @param[in,out] nbst         NBS transport-layer object
 * @param[in]     buf          Rest of frame after NBS transport-layer headers
 * @retval 0                   Success
 * @retval NBS_STATUS_INVAL   `nbst->headers` or `buf` is invalid. log_add()
 *                             called.
 * @retval NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval NBS_STATUS_UNSUPP  Unsupported product. log_add() called.
 * @retval NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
static nbs_status_t nbst_process_non_sync_frame(
        nbst_t* const restrict        nbst,
        const uint8_t* const restrict buf)
{
    int status;
    if (headers_is_same_product(&PREV_HEADER(nbst), &CURR_HEADER(nbst))) {
        if (nbst->start_processed)
            status = nbst_process_data(nbst, buf);
    }
    else {
        nbsp_end_product(nbst->nbsp);
        nbst->start_processed = false;
        if (headers_is_start(&CURR_HEADER(nbst))) {
            status = nbst_process_data(nbst, buf);
            nbst->start_processed = status == 0;
        }
    }
    SWITCH_HEADERS(nbst);
    return status;
}

/**
 * Processes an NBS frame.
 *
 * @param[in,out] nbsp                NBS transport-layer object
 * @param[in]     buf                 Encoded NBS frame
 * @param[in]     nbytes              Number of bytes in `buf`
 * @retval        0                   Continue
 * @retval        NBS_STATUS_INVAL   `buf` is invalid. log_add() called.
 * @retval        NBS_STATUS_UNSUPP  Unsupported product type. log_add()
 *                                    called.
 * @retval        NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval        NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
static nbs_status_t nbst_process_frame(
        nbst_t* const restrict  nbst,
        const uint8_t* restrict buf,
        unsigned                nbytes)
{
    unsigned n;
    int      status = headers_decode(&CURR_HEADER(nbst), buf, nbytes, &n);
    if (status == 0) {
        headers_log_if_unexpected(&PREV_HEADER(nbst), &CURR_HEADER(nbst));
        if (!headers_is_sync_frame(&CURR_HEADER(nbst))) {
            buf += n;
            status = nbst_process_non_sync_frame(nbst, buf);
        }
    }
    return status;
}

/**
 * Runs the NBS transport-layer. Reads frames from the frame-queue and calls the
 * NBS presentation-layer object with data-blocks. Doesn't return until the
 * frame-queue is shut down or an unrecoverable error occurs.
 *
 * @param[in]  nbst                NBS transport-layer object
 * @retval     0                   Success. Frame-queue was shut down.
 * @retval     NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval     NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
static nbs_status_t nbst_run(
        nbst_t* const nbst)
{
    int status = 0;
    while (status == 0) {
        uint8_t* buf;
        unsigned nbytes;
        status = fq_peek(nbst->fq, &buf, &nbytes);
        if (status) {
            status = NBS_STATUS_END;
        }
        else {
            status = nbst_process_frame(nbst, buf, nbytes);
            if (status == NBS_STATUS_INVAL || status == NBS_STATUS_UNSUPP) {
                log_debug("Discarding frame");
                status = 0;
            }
            (void)fq_remove(nbst->fq);
        }
    }
    nbsp_end_product(nbst->nbsp);
    return status == NBS_STATUS_END ? 0 : status;
}

/**
 * Frees the resources associated with an NBS transport-layer object. Does _not_
 * free the associated frame-queue or NBS presentation-layer object.
 *
 * @param[in]  nbst  NBS transport-layer object or `NULL`.
 */
static void nbst_free(
        nbst_t* const nbst)
{
    if (nbst)
        free(nbst);
}

/**
 * Executes the transport-layer. Reads frames from the frame-queue and calls the
 * NBS presentation-layer with blocks of data. Doesn't return until the
 * frame-queue is shut down or an unrecoverable error occurs.
 *
 * @param[in]  fq                 Frame-queue from which to read NBS frames.
 *                                Caller should free when it's no longer needed.
 * @param[in]  nbsp               NBS presentation-layer object. Caller should
 *                                free when it's no longer needed.
 * @retval     0                  Success. Frame-queue was shut down.
 * @retval     NBS_STATUS_INVAL  ` nbst == NULL || fq == NULL || nbsp == NULL`.
 *                                log_add() called.
 * @retval     NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbst_execute(
        fq_t* const restrict   fq,
        nbsp_t* const restrict nbsp)
{
    nbst_t* nbst;
    int     status = nbst_new(&nbst, fq, nbsp);
    if (status) {
        log_add("Couldn't create new NBS transport-layer object");
    }
    else {
        status = nbst_run(nbst);
        if (status)
            log_add("Problem running NBS transport-layer");
        nbst_free(nbst);
    }
    return status;
}
