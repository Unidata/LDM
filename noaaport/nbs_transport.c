/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_transport.c
 * @author: Steven R. Emmerson
 *
 * This file implements the API for the Noaaport Broadcast System (NBS)
 * transport-layer.
 */

#include <nbs_presentation.h>
#include "config.h"

#include "ldm.h"
#include "log.h"
#include "frame_queue.h"
#include "nbs_presentation.h"
#include "nbs_transport.h"
#include "nport.h"
#include <stdint.h>

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

/// Product-definition header
typedef struct pdh {
    uint_fast32_t version;
    uint_fast32_t pdh_length;
    uint_fast32_t trans_type;
    uint_fast32_t psh_length;
    uint_fast32_t block_num;
    uint_fast32_t data_offset;
    uint_fast32_t data_size;
    uint_fast32_t recs_per_block;
    uint_fast32_t blocks_per_rec;
    uint_fast32_t prod_sequence_num;
} pdh_t;

/// Product-specific header
typedef struct psh {
    uint_fast32_t opt_field_num;
    uint_fast32_t opt_field_type;
    uint_fast32_t opt_field_length;
    uint_fast32_t version;
    uint_fast32_t flag;
    uint_fast32_t data_length;
    uint_fast32_t bytes_per_rec;
    uint_fast32_t prod_type;
    uint_fast32_t prod_category;
    uint_fast32_t prod_code;
    uint_fast32_t num_fragments;
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

struct nbst {
    fq_t*    fq;        ///< Queue from which to read NBS frames
    nbsp_t   nbsp;      ///< NBS presentation-layer object
    fh_t     fh;        ///< NBS transport-layer frame header object
    pdh_t    pdh;       ///< Product-definition header
    psh_t    psh;       ///< Product-specific header
    uint8_t* data;      ///< Data block of the current frame
    unsigned nbytes;    ///< Amount of data in bytes
};

/******************************************************************************
 * NBS transport-layer object
 ******************************************************************************/

static void decode_version_and_length(
        uint_fast32_t* const restrict version,
        uint_fast32_t* const restrict length,
        const uint8_t const restrict buf)
{
    *version = buf >> 4;
    *length = (buf & 0xF) * 4;
}

static inline uint16_t decode_uint16(
        const uint8_t* const buf)
{
    return (buf[0] << 8) + buf[1];
}

static inline uint32_t decode_uint32(
        const uint8_t* const buf)
{
    return (((((buf[0] << 8) + buf[1]) << 8) + buf[2]) << 8) + buf[3];
}

/**
 * Decodes the frame-header in a buffer.
 *
 * @param[out]    fh                 Decoded frame-header
 * @param[in,out] buf                Buffer
 * @param[in,out] nbytes             Number of bytes in the buffer
 * @retval        0                  Success. `*fh` is set. `*buf` and `*nbytes`
 *                                   are appropriately adjusted.
 * @retval        NBST_STATUS_INVAL  Invalid frame-header. `log_add()` called.
 */
static nbst_status_t fh_decode(
        fh_t* const restrict     fh,
        uint8_t** const restrict buf,
        unsigned* const restrict nbytes)
{
    uint8_t* ptr = *buf;
    unsigned left = *nbytes;
    if (left < 16) {
        log_add("Available bytes for frame header less than 16: %u", left);
        return NBST_STATUS_INVAL;
    }

    fh->hdlc_address = ptr[0];
    if (fh->hdlc_address != 255) {
        log_add("First byte of frame header not 255: %u", fh->hdlc_address);
        return NBST_STATUS_INVAL;
    }
    fh->hdlc_control = ptr[1];
    decode_version_and_length(&fh->sbn_version, &fh->sbn_length, ptr[2]);
    if (fh->sbn_length != 16) {
        log_add("Length of frame header not 16 bytes: %u", fh->sbn_length);
        return NBST_STATUS_INVAL;
    }

    fh->sbn_control = ptr[3];
    fh->sbn_command = ptr[4];
    if (fh->sbn_command != SBN_CMD_DATA && fh->sbn_command != SBN_CMD_TIME &&
            fh->sbn_command != SBN_CMD_TEST) {
        log_add("Invalid frame header command: %u", fh->sbn_command);
        return NBST_STATUS_INVAL;
    }

    fh->sbn_checksum = decode_uint16(ptr+14);
    uint32_t cksum = 0;
    for (int i = 0; i < 16; i++)
        cksum += ptr[i];
    if (cksum != fh->sbn_checksum) {
        log_add("Invalid frame header checksum");
        return NBST_STATUS_INVAL;
    }

    fh->sbn_data_stream = ptr[5];
    fh->sbn_source = ptr[6];
    fh->sbn_destination = ptr[7];
    fh->sbn_sequence_num = decode_uint32(ptr+8);
    fh->sbn_run = decode_uint16(ptr+12);

    *buf += 16;
    *nbytes -= 16;
    return 0;
}

static nbst_status_t pdh_decode(
        pdh_t* const restrict    pdh,
        uint8_t** const restrict buf,
        unsigned* const restrict nbytes)
{
    int      status;
    uint8_t* ptr = *buf;
    unsigned left = *nbytes;
    if (left < 16) {
        log_add("Available bytes for product-definition header less than 16: "
                "%u", left);
        status = NBST_STATUS_INVAL;
    }
    else {
        decode_version_and_length(&pdh->version, &pdh->pdh_length, ptr[0]);
        if (pdh->version != 1) {
            log_add("Product-definition header version not 1: %u",
                    pdh->version);
            status = NBST_STATUS_INVAL;
        }
        else if (pdh->pdh_length < 16) {
            log_add("Product-definition header shorter than 16 bytes: %u",
                    (unsigned)pdh->pdh_length);
            status = NBST_STATUS_INVAL;
        }
        else {
            pdh->trans_type = ptr[1];
            pdh->psh_length = decode_uint16(ptr+2) - pdh->pdh_length;
            pdh->block_num = decode_uint16(ptr+4);
            pdh->data_offset = decode_uint16(ptr+6);
            pdh->data_size = decode_uint16(ptr+8);
            pdh->recs_per_block = ptr[10];
            pdh->blocks_per_rec = ptr[11];
            pdh->prod_sequence_num = decode_uint32(ptr+12);
            status = 0;
            if (pdh->pdh_length > 16) {
                if (pdh->pdh_length <= left) {
                    log_notice("Product-definition header longer than 16 "
                            "bytes: %u", (unsigned)pdh->pdh_length);
                }
                else {
                    log_add("Product-definition header longer than "
                            "available bytes: length=%u, avail=%u",
                            (unsigned)pdh->pdh_length, left);
                    status = NBST_STATUS_INVAL;
                }
            }
            if (status == 0) {
                *buf += pdh->pdh_length;
                *nbytes -= pdh->pdh_length;
            }
        }
    }
    return status;
}


static inline bool pdh_is_product_start(
        pdh_t* const pdh)
{
    return pdh->trans_type == 1;
}

static inline bool pdh_has_psh(
        pdh_t* const pdh)
{
    return pdh->psh_length != 0;
}

static nbst_status_t psh_decode(
        psh_t* const restrict    psh,
        uint8_t** const restrict buf,
        unsigned* const restrict nbytes,
        const unsigned           expected_len)
{
    int      status;
    uint8_t* ptr = *buf;
    unsigned left = *nbytes;
    if (left < 32) {
        log_add("Available bytes for product-specific header less than 32: %u",
                left);
        status = NBST_STATUS_INVAL;
    }
    else {
        psh->opt_field_num = ptr[0];
        psh->opt_field_type = ptr[1];
        psh->opt_field_length = decode_uint16(ptr+2);
        if (psh->opt_field_length > left) {
            log_add("Length of product-specific header greater than amount of "
                    "data: length=%u, left=%u", psh->opt_field_length, left);
            status = NBST_STATUS_INVAL;
        }
        else {
            psh->version = ptr[4];
            psh->flag = ptr[5];
            psh->data_length = decode_uint16(ptr+6);
            psh->bytes_per_rec = decode_uint16(ptr+8);
            psh->prod_type = ptr[10];
            psh->prod_category = ptr[11];
            psh->prod_code = decode_uint16(ptr+12);
            psh->num_fragments = decode_uint16(ptr+14);
            psh->next_head_offset = decode_uint16(ptr+16);
            psh->prod_seq_num = decode_uint32(ptr+18);
            psh->prod_source = decode_uint16(ptr+22);
            psh->prod_start_time = decode_uint32(ptr+24);
            psh->ncf_recv_time = decode_uint32(ptr+28);
            if (nbytes >= 36) {
                psh->ncf_send_time = decode_uint32(ptr+32);
                if (nbytes >= 38) {
                    psh->proc_cntl_flag = decode_uint16(ptr+36);
                    if (nbytes >= 40) {
                        psh->put_buf_last = decode_uint16(ptr+38);
                        if (nbytes >= 42) {
                            psh->put_buf_first = decode_uint16(ptr+40);
                            if (nbytes >= 44) {
                                psh->expect_buf_num = decode_uint16(ptr+42);
                                if (nbytes >= 48)
                                    psh->prod_run_id = decode_uint32(ptr+44);
                            }
                        }
                    }
                }
            }
            const unsigned actual_len = ptr - *buf;
            if (actual_len != expected_len) {
                log_add("Actual length of product-specific header doesn't "
                        "match expected length: actual=%u, expected=%u",
                        actual_len, expected_len);
                status = NBST_STATUS_INVAL;
            }
            *buf += psh->opt_field_length;
            *nbytes -= psh->opt_field_length;
            status = 0;
        }
    }
    return status;
}

static nbst_status_t psh_next_product(
        psh_t* const restrict       psh,
        const pdh_t* const restrict pdh,
        uint8_t** const restrict    buf,
        unsigned* const restrict    nbytes)
{
    psh_clear(psh);
    uint8_t* ptr = *buf;
    unsigned left = *nbytes;
    int      status = psh_decode(psh, &ptr, &left);
    if (status == 0) {
        if (pdh_get_psh_length(pdh) != psh_get_length(psh)) {
            log_add("Length of product-specific header doesn't match that "
                    "specified by product-definition header: psh=%u, pdh=%u",
                    psh_get_length(psh), pdh_get_psh_length(pdh));
            status = NBST_STATUS_INVAL;
        }
        else if (ptr != *buf + pdh_get_data_offset(pdh)) {
            log_add("Data doesn't start immediately after product-specific "
                    "header: pdh_len=%u, psh_len=%u, pdh->data_offset=%u",
                    pdh_get_length(pdh), psh_get_length(psh),
                    pdh_get_data_offset(pdh));
            status = NBST_STATUS_INVAL;
        }
        else {
            *buf = ptr;
            *nbytes = left;
        }
    }
    return status;
}

static nbst_status_t pdh_next_product(
        pdh_t* const restrict    pdh,
        psh_t* const restrict    psh,
        uint8_t** const restrict buf,
        unsigned* const restrict nbytes)
{
    pdh_clear(pdh);
    uint8_t* ptr = *buf;
    unsigned left = *nbytes;
    int      status = pdh_decode_and_vet(pdh, &ptr, &left);
    if (status == 0) {
        if ((pdh->trans_type & 0x1) == 0) {
            log_add("Product-definition header's transfer type not "
                    "start-of-product (1): %#X", (unsigned)pdh->trans_type);
            status = NBST_STATUS_INVAL;
        }
        else {
            if (!pdh_has_psh(pdh)) {
                /*
                 * productMaker.c:pmStart() doesn't declare the frame invalid
                 * at this point unless the transfer type is also 0, which make
                 * no sense to me. Steve Emmerson 2016-03-19
                 */
                log_add("No product-specific header");
                status = NBST_STATUS_INVAL;
            }
            else {
                status = psh_next_product(psh, pdh, &ptr, &left);
                if (status == 0) {
                    *buf = ptr;
                    *nbytes = left;
                }
            }
        }
    }
    return status;
}

static nbst_status_t fh_next_product(
        fh_t* const restrict     fh,
        pdh_t* const restrict    pdh,
        psh_t* const restrict    psh,
        uint8_t** const restrict buf,
        unsigned* const restrict nbytes)
{
    uint8_t* ptr = *buf;
    unsigned left = *nbytes;
    fh_clear(fh);
    int      status = fh_decode_and_vet(fh, &ptr, &left);
    if (status == 0) {
        status = pdh_next_product(pdh, psh, &ptr, &left);
        if (status == 0) {
            *buf = ptr;
            *nbytes = left;
        }
    }
    return status;
}

/**
 * Decodes the product-specific header, if appropriate.
 *
 * @param[in]     psh                Decoded product-specific header
 * @param[in]     pdh                Decoded product-definition header
 * @param[in,out] buf                Buffer possibly containing product-specific
 *                                   header
 * @param[in,out] nbytes             Number of bytes in `buf`
 * @retval        0                  Not start-of-product and no product-
 *                                   specific header or start-of-product and
 *                                   product-specific header successfully
 *                                   decoded.
 * @retval        NBST_STATUS_INVAL  Invalid product-specific header. log_add()
 *                                   called.
 */
static nbst_status_t decode_psh_if_appropriate(
        psh_t* const restrict    psh,
        pdh_t* const restrict    pdh,
        uint8_t** const restrict buf,
        unsigned* const restrict nbytes)
{
    int status;
    if (!pdh_is_start(pdh)) {
        if (!pdh_has_psh(pdh)) {
            status = 0;
        }
        else {
            log_add("Not start-of-product frame has product-specific header");
            status = NBST_STATUS_INVAL;
        }
    }
    else if (!pdh_has_psh(pdh)) {
        log_add("Start-of-product frame doesn't have product-specific header");
        status = NBST_STATUS_INVAL;
    }
    else {
        status = psh_decode(psh, buf, nbytes, pdh_get_psh_length(pdh));
        if (status)
            log_add("Invalid product-specific header");
    }
    return status;
}

/**
 * Decodes the product-definition header and the (optional) product-specific
 * header (if appropriate).
 *
 * @param[in]     pdh                Decoded product-definition header
 * @param[in]     psh                Decoded product-specific header
 * @param[in,out] buf                Start of product-definition header
 * @param[in,out] nbytes             Number of bytes in `buf`
 * @return        0                  Success. `*pdh` is set. `*psh` is set if
 *                                   appropriate. `*buf` and `*nbytes` are
 *                                   appropriately modified.
 * @retval        NBST_STATUS_INVAL  Invalid header. `log_add()` called.
 */
static nbst_status_t decode_pdh_and_psh(
        pdh_t* const restrict  pdh,
        psh_t* const restrict  psh,
        uint8_t** const restrict buf,
        unsigned* const restrict nbytes)
{
    uint8_t* pdh_start = *buf;
    int      status = pdh_decode(pdh, buf, nbytes);
    if (status) {
        log_add("Invalid product-definition header");
    }
    else {
        status = decode_psh_if_appropriate(psh, pdh, buf, nbytes);
        if (status == 0) {
            if (*buf != pdh_start + pdh_get_data_offset(pdh)) {
                log_add("Product data doesn't start immediately after product-"
                        "specific header: pdh_len=%u, psh_len=%u, "
                        "pdh->data_offset=%u", pdh_get_length(pdh),
                        pdh_get_psh_length(psh), pdh_get_data_offset(pdh));
                status = NBST_STATUS_INVAL;
            }
        }
    }
    return status;
}

/**
 * Decodes (and vets) the NBS headers.
 *
 * @param[in]     nbst               NBS transport-layer object
 * @param[in,out] buf                NBS frame buffer
 * @param[in,out] nbytes             Number of bytes in buffer
 * @retval        0                  Success
 * @retval        NBST_STATUS_INVAL  Invalid header. `log_add()` called.
 */
static nbst_status_t decode_headers(
        nbst_t* const restrict   nbst,
        uint8_t** const restrict buf,
        unsigned* const restrict nbytes)
{
    int         status = fh_decode(&nbst->fh, buf, nbytes);
    if (status) {
        log_add("Invalid frame header");
    }
    else {
        status = decode_pdh_and_psh(&nbst->pdh, &nbst->psh, buf, nbytes);
    }
    return status;
}

/**
 * Processes an NBS frame.
 *
 * @param[in] nbst                NBS transport-layer object
 * @param[in] buf                 Frame buffer
 * @param[in] nbytes              Number of bytes in buffer
 * @retval    0                   Success
 * @retval    NBST_STATUS_INVAL   Invalid frame. `log_add()` called.
 * @retval    NBST_STATUS_UNSUPP  Unsupported product. `log_add()` called.
 */
static nbst_status_t process_frame(
        nbst_t* const restrict  nbst,
        uint8_t* const restrict buf,
        const unsigned          nbytes)
{
    int status = decode_headers(&nbst, &buf, &nbytes);
    if (status) {
        log_add("Invalid header");
    }
    else {
        unsigned type = pdh_get_transfer_type(&nbst->pdh);
        switch (type) {
        case PROD_TYPE_GOES_EAST:
            status = nbsp_goes_east(buf, nbytes);
            break;
        case PROD_TYPE_GOES_WEST:
            status = nbsp_goes_west(buf, nbytes);
            break;
        case PROD_TYPE_NESDIS_NONGOES:
        case PROD_TYPE_NOAAPORT_OPT:
            status = nbsp_nongoes(buf, nbytes);
            break;
        case PROD_TYPE_NWSTG:
            status = nbsp_nwstg(buf, nbytes);
            break;
        case PROD_TYPE_NEXRAD:
            status = nbsp_nexrad(buf, nbytes);
            break;
        default:
            log_add("Unsupported product type: %u", type);
            status = NBST_STATUS_UNSUPP;
        }
    }
    return status;
}

/**
 * Returns a new NBS transport-layer object.
 *
 * @param[out] nbst               New NBS transport-layer object
 * @param[in]  fq                 Frame-queue from which to read NBS frames.
 *                                Will _not_ be freed by nbst_free().
 * @param[in]  nbsp               NBS Presentation-layer object
 * @retval     0                  Success. `*nbst` is set.
 * @retval     NBST_STATUS_INVAL  `fq == NULL || nbst == NULL`. log_add()
 *                                called.
 * @retval     NBST_STATUS_NOMEM  Out of memory. log_add() called.
 */
static nbst_status_t new(
        nbst_t** const restrict nbst,
        fq_t* const restrict    fq,
        nbsp_t* const restrict  nbsp)
{
    int status;
    if (nbst == NULL || fq == NULL || nbsp == NULL) {
        log_add("Invalid argument: nbst=%p, fq=%p, nbsp=%p", nbst, fq, nbsp);
        status = NBST_STATUS_INVAL;
    }
    else {
        nbst_t* const ptr = log_malloc(sizeof(nbst_t),
                "NBS transport-layer object");
        if (ptr == NULL) {
            status = NBST_STATUS_NOMEM;
        }
        else {
            ptr->fq = fq;
            ptr->nbsp = nbsp;
            *nbst = ptr;
            status = 0;
        }
    }
    return status;
}

/**
 * Runs the transport-layer. Reads frames from the frame queue and calls the
 * product processor. Doesn't return until the frame queue is shut down or an
 * unrecoverable error occurs.
 *
 * @param[in]  fq               Frame-queue from which to read NBS frames
 * @param[in]  nbsp             NBS Presentation-layer object
 * @retval     0                Success. Frame-queue was shut down.
 */
static nbst_status_t run(
        nbst_t* const nbst)
{
    int status;
    for (;;) {
        uint8_t* buf;
        unsigned nbytes;
        int      status = fq_peek(nbst->fq, &buf, &nbytes);
        if (status) {
            status = 0;
            break;
        }
        status = process_frame(nbst, buf, nbytes);
        if (status)
            log_warning("Discarding current frame");
        (void)fq_remove(nbst->fq);
    }
    return status;
}

/**
 * Frees the resources associated with an NBS transport-layer object. Does _not_
 * free the associated frame-queue or product-processor.
 *
 * @param[in]  nbst  NBS transport-layer object or `NULL`.
 */
static void nbst_free(
        nbst_t* const nbst)
{
    if (nbst)
        free(nbst);
}

/******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * Starts the transport-layer. Reads frames from the frame queue and calls the
 * product processor. Doesn't return until the frame-queue is shut down or an
 * unrecoverable error occurs.
 *
 * @param[in]  fq                 Frame-queue from which to read NBS frames.
 *                                Caller should free when it's no longer needed.
 * @param[in]  nbsp               NBS Presentation-layer object
 *                                Caller should free when it's no longer needed.
 * @retval     0                  Success. Frame-queue was shut down.
 * @retval     NBST_STATUS_INVAL  ` nbst == NULL || fq == NULL || nbsp == NULL`.
 *                                log_add() called.
 * @retval     NBST_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbst_status_t nbst_start(
        fq_t* const restrict    fq,
        nbsp_t* const restrict  nbsp)
{
    nbst_t* nbst;
    int     status = new(&nbst, fq, nbsp);
    if (status) {
        log_add("Couldn't create new NBS transport-layer object");
    }
    else {
        status = run(nbst);
        if (status)
            log_add("Problem running NBS transport-layer");
        nbst_free(nbst);
    }
    return status;
}
