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
#include "dynabuf.h"
#include "ldm.h"
#include "log.h"
#include "nbs_transport.h"
#include "nbs.h"
#include "nport.h"

#include <arpa/inet.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/uio.h>

#define NBST_MAX(a, b) ((a) >= (b) ? (a) : (b))

/******************************************************************************
 * Utilities:
 ******************************************************************************/

static const uint32_t mod16 = ((uint32_t)1) << 16;
static const uint64_t mod32 = ((uint64_t)1) << 32;

static inline void serialize_version_and_length(
        uint8_t* const restrict buf,
        const uint_fast32_t     version,
        const uint_fast32_t     length)
{
    *buf = (version << 4) | ((length / 4) & 0xf);
}

static void deserialize_version_and_length(
        uint_fast32_t* const restrict version,
        uint_fast32_t* const restrict length,
        const unsigned                value)
{
    *version = value >> 4;
    *length = (value & 0xF) * 4;
}

#define SET_BITS(t, f)   (t |= (f))
#define UNSET_BITS(t, f) (t &= ~(unsigned long)(f))

/******************************************************************************
 * NBS Transport-Layer Frame Header:
 ******************************************************************************/

#define FH_LENGTH 16
#define CHECKSUM_OFFSET (FH_LENGTH - 2)

/// NBS frame header
typedef struct {
    struct fh {
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
    }       bin;            ///< Binary form
    uint8_t buf[FH_LENGTH]; ///< Serialized form
} fh_t;

static inline unsigned fh_get_length(void)
{
    return FH_LENGTH;
}

/**
 * Initializes an NBS transport-layer frame-header.
 *
 * @param[out] fh  Frame-header
 */
static void fh_init(
        fh_t* const    fh)
{
    (void)memset(fh, 0, sizeof(fh_t)); // To silence valgrind(1)

    fh->bin.hdlc_address = 255;
    fh->bin.hdlc_control = 0;    // Not used
    fh->bin.sbn_version = 1;     // Don't know what else to use
    fh->bin.sbn_length = FH_LENGTH;
    fh->bin.sbn_control = 0;     // Not used
    fh->bin.sbn_command = 3;     // Product format data transfer
    fh->bin.sbn_source = 2;      // Reserved
    fh->bin.sbn_destination = 0; // All
    fh->bin.sbn_sequence_num = 0;
    fh->bin.sbn_run = 0;

    fh->buf[0] = fh->bin.hdlc_address;
    fh->buf[1] = fh->bin.hdlc_control;
    (void)serialize_version_and_length(fh->buf+2, fh->bin.sbn_version,
            fh->bin.sbn_length);
    fh->buf[3] = fh->bin.sbn_control;
    fh->buf[4] = fh->bin.sbn_command;
    fh->buf[6] = fh->bin.sbn_source;
    fh->buf[7] = fh->bin.sbn_destination;
    encode_uint32(fh->buf+8, fh->bin.sbn_sequence_num);
    encode_uint16(fh->buf+12, fh->bin.sbn_run);
}

/**
 * Returns the formatted representation of a frame-header.
 *
 * @param[in] fh  Frame-header
 * @return        Formatted representation
 */
static const char* fh_format(
        const fh_t* const fh)
{
    static char            buf[256];
    const struct fh* const bin = &fh->bin;
    (void)snprintf(buf, sizeof(buf)-1, "{"
            "hdlc_address=%"PRIuFAST32", "
            "hdlc_control=%"PRIuFAST32", "
            "sbn_version=%"PRIuFAST32", "
            "sbn_length=%"PRIuFAST32", "
            "sbn_control=%"PRIuFAST32", "
            "sbn_command=%"PRIuFAST32", "
            "sbn_data_stream=%"PRIuFAST32", "
            "sbn_source=%"PRIuFAST32", "
            "sbn_destination=%"PRIuFAST32", "
            "sbn_seq_num=%"PRIuFAST32", "
            "sbn_run=%"PRIuFAST32", "
            "sbn_checksum=%"PRIuFAST32"}",
            bin->hdlc_address,
            bin->hdlc_control,
            bin->sbn_version,
            bin->sbn_length,
            bin->sbn_control,
            bin->sbn_command,
            bin->sbn_data_stream,
            bin->sbn_source,
            bin->sbn_destination,
            bin->sbn_sequence_num,
            bin->sbn_run,
            bin->sbn_checksum);
    return buf;
}

/**
 * Configures an NBS transport-layer frame-header for the start of a product.
 *
 * @param[out] fh           Frame-header
 * @param[in]  data_stream  Channel (i.e., data stream) identifier. One of
 *                            - 1 GOES East
 *                            - 2 GOES West
 *                            - 3 Reserved
 *                            - 4 Non-GOES Imagery/DCP
 *                            - 5 NCEP/NWSTG
 *                            - 6 Reserved
 *                            - 7 Reserved
 *                          See <http://www.nws.noaa.gov/noaaport/html/transprt.shtml>.
 */
static inline void fh_start(
        fh_t* const fh,
        const int   data_stream)
{
    fh->bin.sbn_data_stream = data_stream;
    fh->buf[5] = data_stream;
}

static void fh_compute_and_set_checksum(
        fh_t* const fh)
{
    uint16_t cksum = 0;
    for (int i = 0; i < CHECKSUM_OFFSET; i++)
        cksum += fh->buf[i];
    fh->bin.sbn_checksum = cksum;
    encode_uint16(fh->buf + CHECKSUM_OFFSET, cksum);
}

static inline uint8_t* fh_get_buf(
        fh_t* const fh)
{
    return fh->buf;
}

/**
 * Advances a frame-header for the next frame.
 *
 * @param[in,out] fh  Frame-header
 */
static void fh_advance(
        fh_t* const fh)
{
    encode_uint32(fh->buf+8, ++fh->bin.sbn_sequence_num);
}

/**
 * Deserializes the frame-header in a buffer.
 *
 * @param[out] fh                 Decoded frame-header
 * @param[in]  buf                Encoded frame header
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the decoding
 * @retval     0                  Success. `*fh` and `*nscanned` are set.
 * @retval     NBS_STATUS_INVAL   Invalid frame-header. `log_add()` called.
 */
static nbs_status_t fh_deserialize(
        fh_t* const restrict          fh,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    if (nbytes < FH_LENGTH) {
        log_add("Available bytes for frame header less than %d: %u",
                FH_LENGTH, nbytes);
        return NBS_STATUS_INVAL;
    }

    fh->bin.hdlc_address = buf[0];
    if (fh->bin.hdlc_address != 255) {
        log_add("First byte of frame header not 255: %u", fh->bin.hdlc_address);
        return NBS_STATUS_INVAL;
    }
    fh->bin.hdlc_control = buf[1];
    deserialize_version_and_length(&fh->bin.sbn_version, &fh->bin.sbn_length,
            buf[2]);
    if (fh->bin.sbn_length != FH_LENGTH) {
        log_add("Length of frame header not %d bytes: %u", FH_LENGTH,
                fh->bin.sbn_length);
        return NBS_STATUS_INVAL;
    }

    fh->bin.sbn_control = buf[3];
    fh->bin.sbn_command = buf[4];
    if (fh->bin.sbn_command != SBN_CMD_DATA &&
            fh->bin.sbn_command != SBN_CMD_TIME &&
            fh->bin.sbn_command != SBN_CMD_TEST) {
        log_add("Invalid frame header command: %u", fh->bin.sbn_command);
        return NBS_STATUS_INVAL;
    }

    fh->bin.sbn_checksum = decode_uint16(buf+CHECKSUM_OFFSET);
    uint16_t cksum = 0;
    for (int i = 0; i < CHECKSUM_OFFSET; i++)
        cksum += buf[i];
    if (cksum != fh->bin.sbn_checksum) {
        log_add("Invalid frame-header checksum: actual=%u, expected=%lu",
                cksum, (unsigned long)fh->bin.sbn_checksum);
        return NBS_STATUS_INVAL;
    }

    fh->bin.sbn_data_stream = buf[5];
    fh->bin.sbn_source = buf[6];
    fh->bin.sbn_destination = buf[7];
    fh->bin.sbn_sequence_num = decode_uint32(buf+8);
    fh->bin.sbn_run = decode_uint16(buf+12);

    *nscanned = FH_LENGTH;
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
    return fh->bin.sbn_command == 5;
}

/**
 * Indicates if one frame header logically follows another.
 *
 * @param[in] prev  Previous frame header
 * @param[in] curr  Current frame header
 * @return    true  iff `next` logically follows `prev`
 */
static inline bool fh_is_next(
        const fh_t* const restrict prev,
        const fh_t* const restrict curr)
{
    return (curr->bin.sbn_run == prev->bin.sbn_run &&
                curr->bin.sbn_sequence_num ==
                        (prev->bin.sbn_sequence_num + 1)%mod32) ||
            (curr->bin.sbn_run == (prev->bin.sbn_run + 1)%mod16 &&
                curr->bin.sbn_sequence_num == 0);
}

static void fh_log_if_unexpected(
        const fh_t* const restrict prev,
        const fh_t* const restrict curr)
{
    if (!fh_is_next(prev, curr))
        log_warning_q("Current frame is out-of-sequence: prev_run=%"PRIuFAST32", "
                "prev_seqnum=%"PRIuFAST32", curr_run=%"PRIuFAST32", "
                "curr_seqnum=%"PRIuFAST32, prev->bin.sbn_run,
                prev->bin.sbn_sequence_num, curr->bin.sbn_run,
                curr->bin.sbn_sequence_num);
}

/******************************************************************************
 * NBS Transport-Layer Product-Definition Header:
 ******************************************************************************/

#define PDH_LENGTH  16
#define PDH_VERSION 1

/// Product-definition header
typedef struct {
    struct pdh {
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
        uint_fast32_t block_num;       ///< Data-block index. Start-frame is 0
        uint_fast32_t data_offset;     ///< In compressed GINI product, number
                                       ///< of bytes from start of first data
                                       ///< block to compressed data (i.e.,
                                       ///< number of bytes in clear-text WMO
                                       ///< header). Not used in pmStart().
        uint_fast32_t data_size;       ///< Size of data-block in bytes
        uint_fast32_t recs_per_block;  ///< Canonical: last block may have fewer
        uint_fast32_t blocks_per_rec;
        uint_fast32_t prod_sequence_num; ///< Unique per product
    }       bin;             ///< Binary form
    uint8_t buf[PDH_LENGTH]; ///< Serialized form
} pdh_t;

static inline unsigned pdh_get_length(void)
{
    return PDH_LENGTH;
}

/**
 * Initializes a product-definition header.
 *
 * @param[out] pdh             Product-definition header
 */
static void pdh_init(
        pdh_t* const   pdh)
{
    (void)memset(pdh, 0, sizeof(pdh_t)); // To silence valgrind(1)

    pdh->bin.version = PDH_VERSION;
    pdh->bin.pdh_length = PDH_LENGTH;
    pdh->bin.trans_type = 3;   // Start of product & product transfer in progress
    pdh->bin.data_offset = 16; // Not sure of the meaning of this field
    pdh->bin.blocks_per_rec = 0; // Not sure about this

    serialize_version_and_length(pdh->buf, pdh->bin.version,
            pdh->bin.pdh_length);
    pdh->buf[1] = pdh->bin.trans_type;
    encode_uint16(pdh->buf+6,  pdh->bin.data_offset);
    pdh->buf[11] = pdh->bin.blocks_per_rec;
}

/**
 * Configures a product-definition header for the start of a product.
 *
 * @param[out] pdh             Product-definition header
 * @param[in]  psh_length      Number of bytes in product's product-specific
 *                             header
 * @param[in]  recs_per_block  Number of records in canonical data-block
 * @param[in]  psh_length      Number of bytes in product-specific header
 * @param[in]  is_compressed   Is data-block zlib(3)-compressed?
 */
static void pdh_start(
        pdh_t* const   pdh,
        const unsigned psh_length,
        const unsigned recs_per_block,
        const unsigned num_blocks,
        const bool     is_compressed)
{
    static uint32_t prod_sequence_num = 0;

    pdh->bin.psh_length = psh_length;
    pdh->bin.block_num = 0;
    pdh->bin.recs_per_block = recs_per_block;
    pdh->bin.trans_type = 1 | ((num_blocks <= 1) ? 4 : 2) |
            (is_compressed ? 16 : 0);
    pdh->bin.prod_sequence_num = prod_sequence_num++;

    pdh->buf[1] = pdh->bin.trans_type;
    encode_uint16(pdh->buf+2,  pdh->bin.pdh_length + pdh->bin.psh_length);
    encode_uint16(pdh->buf+4,  pdh->bin.block_num);
    pdh->buf[10] = pdh->bin.recs_per_block;
    encode_uint32(pdh->buf+12, pdh->bin.prod_sequence_num);
}

static inline void pdh_set_data_size(
        pdh_t* const   pdh,
        const unsigned nbytes)
{
    pdh->bin.data_size = nbytes;
    encode_uint16(pdh->buf+8, pdh->bin.data_size);
}

/**
 * Advances a product-definition header for the next data-block.
 *
 * @param[in,out] pdh         Product-definition header
 * @param[in]     num_blocks  Number of blocks in the product
 */
static void pdh_advance(
        pdh_t* const   pdh,
        const unsigned num_blocks)
{
    UNSET_BITS(pdh->bin.trans_type, 1);
    if (++pdh->bin.block_num + 1 >= num_blocks) {
        UNSET_BITS(pdh->bin.trans_type, 2);
        SET_BITS(pdh->bin.trans_type, 4);
    }
    pdh->buf[1] = pdh->bin.trans_type;
    pdh->bin.psh_length = 0; // PSH in first block only
    encode_uint16(pdh->buf+2,  pdh->bin.pdh_length);
    encode_uint16(pdh->buf+4, pdh->bin.block_num);
}

static inline uint8_t* pdh_get_buf(
        pdh_t* const pdh)
{
    return pdh->buf;
}

/**
 * Formats a product-definition header.
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @param[in]  pdh   Product-definition header
 * @return           Formatted representation
 */
static const char* pdh_format(
        const pdh_t* const pdh)
{
    static char  buf[256];
    const struct pdh* const bin = &pdh->bin;
    (void)snprintf(buf, sizeof(buf)-1, "{version=%d, length=%d, trans_type=%#X, "
            "psh_length=%d, block_num=%d, data_off=%d, data_size=%d, "
            "recs_per_block=%d, blocks_per_rec=%d, prod_seq_num=%d}",
        (int)bin->version,
        (int)bin->pdh_length,
        (int)bin->trans_type,
        (int)bin->psh_length,
        (int)bin->block_num,
        (int)bin->data_offset,
        (int)bin->data_size,
        (int)bin->recs_per_block,
        (int)bin->blocks_per_rec,
        (int)bin->prod_sequence_num);
    return buf;
}

/**
 * De-serializes a product-definition header.
 *
 * @param[out] pdh                Product-definition header
 * @param[in]  buf                Serialized product-definition header
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the de-serialization
 * @retval     0                  Success. `*pdh` and `nscanned` are set.
 * @retval     NBS_STATUS_INVAL   Invalid header. log_add() called.
 */
static nbs_status_t pdh_deserialize(
        pdh_t* const restrict         pdh,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int status;
    if (nbytes < PDH_LENGTH) {
        log_add("Insufficient bytes for product-definition header: actual=%u, "
                "expected=%d", nbytes, PDH_LENGTH);
        status = NBS_STATUS_INVAL;
    }
    else {
        struct pdh* bin = &pdh->bin;
        deserialize_version_and_length(&bin->version, &bin->pdh_length, buf[0]);
        if (bin->version != PDH_VERSION) {
            log_add("Invalid version of product-definition header: actual=%u, "
                    "expected=%d", bin->version, PDH_VERSION);
            status = NBS_STATUS_INVAL;
        }
        else if (bin->pdh_length < PDH_LENGTH) {
            log_add("Invalid length of product-definition header: actual=%u, "
                    "expected=%d", (unsigned)bin->pdh_length, PDH_LENGTH);
            status = NBS_STATUS_INVAL;
        }
        else {
            log_debug_1("bin->pdh_length=%"PRIuFAST32, bin->pdh_length);
            bin->trans_type = buf[1];
            bin->psh_length = decode_uint16(buf+2) - bin->pdh_length;
            bin->block_num = decode_uint16(buf+4);
            bin->data_offset = decode_uint16(buf+6);
            bin->data_size = decode_uint16(buf+8);
            bin->recs_per_block = buf[10];
            bin->blocks_per_rec = buf[11];
            bin->prod_sequence_num = decode_uint32(buf+12);
            status = 0;
            if (bin->pdh_length > PDH_LENGTH) {
                if (bin->pdh_length <= nbytes) {
                    log_notice_q("Product-definition header longer than %d "
                            "bytes: %u", PDH_VERSION,
                            (unsigned)bin->pdh_length);
                }
                else {
                    log_add("Product-definition header longer than "
                            "available bytes: length=%u, avail=%u",
                            (unsigned)bin->pdh_length, nbytes);
                    status = NBS_STATUS_INVAL;
                }
            }
            if (status == 0) {
                if (log_is_enabled_debug)
                    log_debug_1("pdh=%s", pdh_format(pdh));
                *nscanned = bin->pdh_length;
            }
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
    return curr->bin.prod_sequence_num == prev->bin.prod_sequence_num;
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
    return pdh->bin.trans_type & 1;
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
    return pdh->bin.trans_type & 4;
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
    return pdh->bin.trans_type & 16;
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
    return pdh->bin.psh_length != 0;
}

/**
 * Returns the length, in bytes, of the product-specific header.
 *
 * @param[in] pdh  Product-definition header
 */
static inline unsigned pdh_get_psh_length(
        const pdh_t* const pdh)
{
    return pdh->bin.psh_length;
}

/**
 * Returns the origin-0 block number.
 *
 * @param[in] pdh  Product-definition header
 * @return         Block number
 */
static inline unsigned pdh_get_block_num(
        const pdh_t* const pdh)
{
    return pdh->bin.block_num;
}

/**
 * Returns the amount of product data in bytes.
 *
 * @param[in] pdh  Product-definition header
 * @return         Amount of product data in bytes
 */
static inline unsigned pdh_get_data_size(
        const pdh_t* const pdh)
{
    return pdh->bin.data_size;
}

/**
 * Returns the number of records per data block.
 *
 * @param[in] pdh  Product-definition header
 * @return         Amount of product data in bytes
 */
static inline unsigned pdh_get_recs_per_block(
        const pdh_t* const pdh)
{
    return pdh->bin.recs_per_block;
}

/******************************************************************************
 * NBS Transport-Layer Product-Specific Header:
 ******************************************************************************/

#define PSH_LENGTH 48

/// Product-specific header
typedef struct {
    struct psh {
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
        int_fast32_t  num_fragments;    ///< Includes start-frame
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
    } bin;                   ///< Binary form
    uint8_t buf[PSH_LENGTH]; ///< Serialized form
} psh_t;

static inline unsigned psh_get_length(void)
{
    return PSH_LENGTH;
}

/**
 * Initializes a product-specific header.
 *
 * @param[out] psh               Product-specific header
 */
static void psh_init(
        psh_t* const   psh)
{
    (void)memset(psh, 0, sizeof(psh_t)); // To silence valgrind(1)

    struct psh* bin = &psh->bin;
    bin->opt_field_num = 0;    // Unknown field
    bin->opt_field_type = 0;   // Unknown field
    bin->opt_field_length = PSH_LENGTH;
    bin->version = 1;          // Don't know what else to do
    bin->flag = 1;             // Unknown field
    bin->next_head_offset = 0; // Unknown field
    bin->prod_seq_num = 0;
    bin->prod_source = 0;      // Don't know what else to do
    bin->prod_start_time = 0;  // Don't know what else to do
    bin->ncf_recv_time = 0;    // Don't know what else to do
    bin->ncf_send_time = 0;    // Don't know what else to do
    bin->proc_cntl_flag = 0;   // OK
    bin->put_buf_last = 0;     // Don't know what else to do
    bin->put_buf_first = 0;    // Don't know what else to do
    bin->expect_buf_num = 0;   // Unknown field
    bin->prod_run_id = 0;

    uint8_t* buf = psh->buf;
    buf[0] = bin->opt_field_num;
    buf[1] = bin->opt_field_type;
    encode_uint16(buf+2, bin->opt_field_length);
    buf[4] = bin->version;
    buf[5] = bin->flag;
    encode_uint16(buf+16, bin->next_head_offset);
    encode_uint32(buf+18, bin->prod_seq_num);
    encode_uint16(buf+22, bin->prod_source);
    encode_uint32(buf+24, bin->prod_start_time);
    encode_uint32(buf+28, bin->ncf_recv_time);
    encode_uint32(buf+32, bin->ncf_send_time);
    encode_uint16(buf+36, bin->proc_cntl_flag);
    encode_uint16(buf+38, bin->put_buf_last);
    encode_uint16(buf+40, bin->put_buf_first);
    encode_uint16(buf+42, bin->expect_buf_num);
    encode_uint32(buf+44, bin->prod_run_id);
}

/**
 * Configures a product-specific header for the start of a product.
 *
 * @param[out] psh               Product-specific header
 * @param[in]  bytes_per_record  Number of bytes per data-record
 * @param[in]  prod_type         Type of product
 * @param[in]  num_blocks        Total number of data-blocks in product
 */
static void psh_start(
        psh_t* const   psh,
        const unsigned bytes_per_record,
        const int      prod_type,
        const int      num_blocks)
{
    static uint32_t prod_seq_num = 0; // Product sequence number as set by NCF

    struct psh* bin = &psh->bin;
    bin->flag = 1; // Start-of-product
    if (num_blocks >= 0) {
        // Product has a known number of data-blocks
        if (num_blocks <= 1)
            bin->flag |= 4; // End-of-product
    }
    bin->bytes_per_rec = bytes_per_record;
    bin->prod_type = prod_type;
    bin->num_fragments = num_blocks;
    bin->prod_seq_num = prod_seq_num++;  // Don't know what else to do
    bin->prod_run_id = bin->prod_seq_num;

    uint8_t* buf = psh->buf;
    buf[5] = bin->flag;
    encode_uint16(buf+8, bin->bytes_per_rec);
    buf[10] = bin->prod_type;
    encode_uint16(buf+14, bin->num_fragments);
    encode_uint32(buf+18, bin->prod_seq_num);
    encode_uint32(buf+44, bin->prod_run_id);
}

static inline uint8_t* psh_get_buf(
        psh_t* const psh)
{
    return psh->buf;
}

/**
 * Formats a binary product-specific header.
 *
 * @param[in]  psh   Binary product-specific header
 * @param[out] buf   Buffer to format `psh` into
 * @param[in]  size  Number of bytes in `buf`
 * @return           Number of bytes that would have been written to `buf` had
 *                   `size` been sufficiently large excluding the terminating
 *                   NUL.
 */
static int psh_format(
        const psh_t* const restrict psh,
        char* const restrict        buf,
        const size_t                size)
{
    const struct psh* const bin = &psh->bin;
    int          nbytes = snprintf(buf, size, "{opt_field_num=%"PRIuFAST32", "
            "opt_field_type=%"PRIuFAST32", opt_field_length=%"PRIuFAST32", "
            "version=%"PRIuFAST32", flag=%#"PRIXFAST32", "
            "data_length=%"PRIuFAST32", bytes_per_rec=%"PRIuFAST32", "
            "prod_type=%"PRIuFAST32", prod_category=%"PRIuFAST32", "
            "prod_code=%"PRIdFAST32", num_frags=%"PRIdFAST32", "
            "next_head_off=%"PRIuFAST32", prod_seq_num=%"PRIuFAST32", "
            "prod_source=%"PRIuFAST32", prod_start_time=%"PRIuFAST32", "
            "ncf_recv_time=%"PRIuFAST32,
            bin->opt_field_num, bin->opt_field_type, bin->opt_field_length,
            bin->version, bin->flag, bin->data_length, bin->bytes_per_rec,
            bin->prod_type, bin->prod_category, bin->prod_code,
            bin->num_fragments, bin->next_head_offset, bin->prod_seq_num,
            bin->prod_source, bin->prod_start_time, bin->ncf_recv_time);
    if (nbytes < size && bin->opt_field_length >= 36) {
        nbytes += snprintf(buf+nbytes, size-nbytes,
                ", ncf_send_time=%"PRIuFAST32, bin->ncf_send_time);
        if (nbytes < size && bin->opt_field_length >= 38) {
            nbytes += snprintf(buf+nbytes, size-nbytes,
                    ", proc_cntl_flag=%#"PRIXFAST32, bin->proc_cntl_flag);
            if (nbytes < size && bin->opt_field_length >= 40) {
                nbytes += snprintf(buf+nbytes, size-nbytes,
                        ", put_buf_last=%"PRIuFAST32, bin->put_buf_last);
                if (nbytes < size && bin->opt_field_length >= 42) {
                    nbytes += snprintf(buf+nbytes, size-nbytes,
                            ", put_buf_first=%"PRIdFAST32, bin->put_buf_first);
                    if (nbytes < size && bin->opt_field_length >= 44) {
                        nbytes += snprintf(buf+nbytes, size-nbytes,
                                ", expect_buf_num=%"PRIuFAST32,
                                bin->expect_buf_num);
                        if (nbytes < size && bin->opt_field_length >= 48) {
                            nbytes += snprintf(buf+nbytes, size-nbytes,
                                    ", prod_run_id=%"PRIdFAST32,
                                    bin->prod_run_id);
                        }
                    }
                }
            }
        }
    }
    if (nbytes < size)
        nbytes += snprintf(buf+nbytes, size-nbytes, "}");
    return nbytes;
}

/**
 * Deserializes a product-specific header.
 *
 * @param[out] psh               Decoded product-specific header
 * @param[in]  buf               Encoded product-specific header
 * @param[in]  nbytes            Number of bytes in `buf`
 * @param[out] nscanned          Number of bytes in `buf` scanned in order to
 *                               perform the decoding
 * @retval     0                 Success. `*psh` and `*nscanned` are set.
 * @retval     NBS_STATUS_INVAL  Invalid header. log_add() called.
 */
static nbs_status_t psh_deserialize(
        psh_t* const restrict         psh,
        const uint8_t* const restrict buf,
        unsigned                      nbytes,
        unsigned* const restrict      nscanned)
{
    int status;
    if (nbytes < 32) {
        log_add("Product-specific header shorter than 32: %u", nbytes);
        status = NBS_STATUS_INVAL;
    }
    else {
        struct psh* bin = &psh->bin;
        bin->opt_field_num = buf[0];
        bin->opt_field_type = buf[1];
        bin->opt_field_length = decode_uint16(buf+2);
        if (bin->opt_field_length > nbytes) {
            log_add("Product-specific header longer than available data: "
                    "length=%u, avail=%u", bin->opt_field_length, nbytes);
            status = NBS_STATUS_INVAL;
        }
        else {
            bin->version = buf[4];
            bin->flag = buf[5];
            bin->data_length = decode_uint16(buf+6);
            nbytes = bin->opt_field_length;
            bin->bytes_per_rec = decode_uint16(buf+8);
            bin->prod_type = buf[10];
            bin->prod_category = buf[11];
            bin->prod_code = decode_uint16(buf+12);
            uint16_t x = decode_uint16(buf+14);
            bin->num_fragments = (x > INT16_MAX) ? -1 : x; // -1 => unknown
            bin->next_head_offset = decode_uint16(buf+16);
            bin->prod_seq_num = decode_uint32(buf+18);
            bin->prod_source = decode_uint16(buf+22);
            bin->prod_start_time = decode_uint32(buf+24);
            bin->ncf_recv_time = decode_uint32(buf+28);
            unsigned n = 32;
            if (nbytes >= 36) {
                bin->ncf_send_time = decode_uint32(buf+32);
                n += 4;
                if (nbytes >= 38) {
                    bin->proc_cntl_flag = decode_uint16(buf+36);
                    n += 2;
                    if (nbytes >= 40) {
                        bin->put_buf_last = decode_uint16(buf+38);
                        n += 2;
                        if (nbytes >= 42) {
                            bin->put_buf_first = decode_uint16(buf+40);
                            n += 2;
                            if (nbytes >= 44) {
                                bin->expect_buf_num = decode_uint16(buf+42);
                                n += 2;
                                if (nbytes >= 48) {
                                    bin->prod_run_id = decode_uint32(buf+44);
                                    n += 4;
                                }
                            }
                        }
                    }
                }
            }
            if (log_is_enabled_debug) {
                char string[512];
                (void)psh_format(psh, string, sizeof(string));
                string[sizeof(string)-1] = 0;
                log_debug_1("psh=%s", string);
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
 * @param[in] psh      The deserialized product-specific header
 * @return             The type of the associated product. One of
 *                     PROD_TYPE_GOES_EAST, PROD_TYPE_GOES_WEST,
 *                     PROD_TYPE_NESDIS_NONGOES, PROD_TYPE_NOAAPORT_OPT,
 *                     PROD_TYPE_NWSTG, or PROD_TYPE_NEXRAD.
 */
static inline unsigned psh_get_type(
        const psh_t* const psh)
{
    return psh->bin.prod_type;
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
    return psh->bin.bytes_per_rec;
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
    return psh->bin.num_fragments;
}

/******************************************************************************
 * Combined NBS Transport-Layer Product-Definition and Product-Specific Headers:
 ******************************************************************************/

/**
 * Deserializes the product-specific header, if appropriate.
 *
 * @param[out] psh               Possibly deserialized product-specific header
 * @param[in]  pdh               Decoded product-definition header
 * @param[in]  buf               Possibly encoded product-specific header
 * @param[in]  nbytes            Number of bytes in `buf`
 * @param[out] nscanned          Number of bytes in `buf` scanned in order to
 *                               perform the decoding
 * @retval     0                 Success. Either `buf` contained a encoded
 *                               product-specific header (in which case
 *                               `*psh` and `*nscanned` are set) or the
 *                               associated frame is after the start-of-product
 *                               and `buf` doesn't contain an encoded
 *                               product-specific header (in which case
 *                               `*nscanned` is set to `0`).
 * @retval     NBS_STATUS_INVAL  Invalid product-specific header. log_add()
 *                               called.
 */
static nbs_status_t pdhpsh_deserialize_psh(
        psh_t* const restrict         psh,
        const pdh_t* const restrict   pdh,
        const uint8_t* const restrict buf,
        unsigned                      nbytes,
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
            log_notice_q("Frame after start-of-product has product-specific "
                    "header");
        /*
         * Because the number of bytes in a start-of-product frame has been seen
         * to be greater than the sum of the lengths of the frame,
         * product-definition, and product-specific headers, the length of the
         * product-specific header is set to the value in the product-definition
         * header.
         */
        nbytes = pdh_get_psh_length(pdh);
        status = psh_deserialize(psh, buf, nbytes, nscanned);
        if (status) {
            log_add("Invalid product-specific header");
        }
        else {
            const unsigned expected = pdh_get_psh_length(pdh);
            if (*nscanned != expected) {
                log_add("Actual length of product-specific header doesn't "
                        "match expected length: actual=%u, expected=%u",
                        *nscanned, expected);
                //status = NBS_STATUS_INVAL;
                status = NBS_STATUS_SYSTEM;
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
 * @param[out] psh                Possibly deserialized product-specific header
 * @param[in]  buf                Encoded product-definition header
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the decoding
 * @retval     0                  Success. `*pdh` and `*nscanned` are set.
 *                                `*psh` is set if appropriate.
 * @retval     NBS_STATUS_INVAL   Invalid header. `log_add()` called.
 */
static nbs_status_t pdhpsh_deserialize(
        pdh_t* const restrict    pdh,
        psh_t* const restrict    psh,
        const uint8_t* restrict  buf,
        unsigned                 nbytes,
        unsigned* const restrict nscanned)
{
    const uint8_t* const pdh_start = buf;
    unsigned             n;
    int                  status = pdh_deserialize(pdh, buf, nbytes, &n);
    if (status == 0) {
        buf += n;
        nbytes -= n;
        status = pdhpsh_deserialize_psh(psh, pdh, buf, nbytes, &n);
        if (status == 0)
            *nscanned = buf + n - pdh_start;
    }
    return status;
}

/******************************************************************************
 * NBS transport-layer frame for receiving products:
 ******************************************************************************/

typedef struct {
    fh_t  fh;              ///< Frame header
    pdh_t pdh;             ///< Product-definition header
} headers_t;

typedef struct {
    headers_t headers[2];  ///< Frame-dependent previous and current FH and PDH
    psh_t     psh;         ///< Product-specific header. Previous and current
                           ///< not needed
    uint8_t   buf[65507];  ///< Buffer for serialized frame. Maximum UDP payload
    int       curr_index;  ///< Index of current headers
    bool      prev_exists; ///< Do previous headers exist?
} recvFrame_t;
#define CURR_HEADERS(frame) (frame->headers + frame->curr_index)
#define CURR_FH(frame)      (&CURR_HEADERS(frame)->fh)
#define CURR_PDH(frame)     (&CURR_HEADERS(frame)->pdh)

#define PREV_HEADERS(frame) (frame->headers + !frame->curr_index)
#define PREV_FH(frame)      (&PREV_HEADERS(frame)->fh)
#define PREV_PDH(frame)     (&PREV_HEADERS(frame)->pdh)

/**
 * Initializes a receiving frame.
 *
 * @param[in] frame  Receiving frame
 */
static void recvFrame_init(
        recvFrame_t* const frame)
{
    for (int i = 0; i < 2; i++) {
        fh_init(&frame->headers[i].fh);
        pdh_init(&frame->headers[i].pdh);
    }
    psh_init(&frame->psh);
    frame->curr_index = 0;
    frame->prev_exists = false;
}

/**
 * Returns the formatted string representation of a receive-frame.
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @param[in] frame  Receive-frame
 * @return           Formatted string representation
 */
static const char* recvFrame_format(
        const recvFrame_t* const frame)
{
    static char string[512];
    (void)snprintf(string, sizeof(string)-1, "{fh=%s, pdh=%s}",
            fh_format(CURR_FH(frame)), pdh_format(CURR_PDH(frame)));
    return string;
}

/**
 * De-serializes (and vets) a received frame.
 *
 * @param[out] frame              Receiving frame
 * @param[in]  buffer             Serialized NBS frame
 * @param[in]  nbytes             Number of bytes in `buf`
 * @param[out] nscanned           Number of bytes in `buf` scanned in order to
 *                                perform the de-serialization
 * @retval     0                  Success. `*frame` and `*nscanned` are set.
 * @retval     NBS_STATUS_INVAL   Invalid header. `log_add()` called.
 */
static nbs_status_t recvFrame_deserialize(
        recvFrame_t* const restrict   frame,
        const uint8_t* const restrict buffer,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    const uint8_t* buf = buffer;
    unsigned       len = nbytes;
    unsigned       nscan;
    fh_t* const    fh = CURR_FH(frame);
    int            status = fh_deserialize(fh, buf, len, &nscan);
    if (status) {
        log_add("Invalid frame header");
    }
    else {
        if (frame->prev_exists)
            fh_log_if_unexpected(PREV_FH(frame), fh);
        if (!fh_is_sync_frame(fh)) {
            buf += nscan;
            len -= nscan;
            pdh_t* const pdh = CURR_PDH(frame);
            psh_t* const psh = &frame->psh;
            status = pdhpsh_deserialize(pdh, psh, buf, len, &nscan);
            if (status == 0) {
                buf += nscan;
                len -= nscan;
                unsigned data_size = pdh_get_data_size(pdh);
                if (data_size > len) {
                    log_add("Frame too small to contain data specified: "
                            "available (%u) < specified (%u)", len, data_size);
                    status = NBS_STATUS_INVAL;
                }
                else {
                    *nscanned = buf - buffer;
                }
            }
        }
    }
    return status;
}

/**
 * Indicates if a received frame is for timing/synchronization purposes.
 *
 * @param[in] frame    Received frame
 * @retval    true     iff the frame is for timing/synchronization purposes
 */
static inline bool recvFrame_is_sync_frame(
        const recvFrame_t* const frame)
{
    return fh_is_sync_frame(CURR_FH(frame));
}

static inline bool recvFrame_is_same_product(
        const recvFrame_t* const restrict frame)
{
    return frame->prev_exists &&
            pdh_is_same_product(PREV_PDH(frame), CURR_PDH(frame));
}

static inline bool recvFrame_is_start(
        const recvFrame_t* const restrict frame)
{
    return pdh_is_product_start(CURR_PDH(frame));
}

/**
 * Returns the type of the product associated with given headers.
 *
 * @param[in] headers  The deserialized headers
 * @return             The type of the associated product. One of
 *                     PROD_TYPE_GOES_EAST, PROD_TYPE_GOES_WEST,
 *                     PROD_TYPE_NESDIS_NONGOES, PROD_TYPE_NOAAPORT_OPT,
 *                     PROD_TYPE_NWSTG, or PROD_TYPE_NEXRAD.
 */
static inline unsigned recvFrame_get_product_type(
        const recvFrame_t* const frame)
{
    return psh_get_type(&frame->psh);
}

static inline unsigned recvFrame_get_data_size(
        const recvFrame_t* const frame)
{
    return pdh_get_data_size(CURR_PDH(frame));
}

static inline unsigned recvFrame_is_product_start(
        const recvFrame_t* const frame)
{
    return pdh_is_product_start(CURR_PDH(frame));
}

static inline unsigned recvFrame_is_product_end(
        const recvFrame_t* const frame)
{
    return pdh_is_product_end(CURR_PDH(frame));
}

static inline unsigned recvFrame_is_compressed(
        const recvFrame_t* const frame)
{
    return pdh_is_compressed(CURR_PDH(frame));
}

static inline unsigned recvFrame_get_num_fragments(
        const recvFrame_t* const frame)
{
    return psh_get_num_fragments(&frame->psh);
}

static inline unsigned recvFrame_get_rec_len(
        const recvFrame_t* const frame)
{
    return psh_get_rec_len(&frame->psh);
}

static inline unsigned recvFrame_get_recs_per_block(
        const recvFrame_t* const frame)
{
    return pdh_get_recs_per_block(CURR_PDH(frame));
}

static inline unsigned recvFrame_get_type(
        const recvFrame_t* const frame)
{
    return psh_get_type(&frame->psh);
}

static inline unsigned recvFrame_get_block_num(
        const recvFrame_t* const frame)
{
    return pdh_get_block_num(CURR_PDH(frame));
}

static inline void recvFrame_switch_headers(
        recvFrame_t* const frame)
{
    frame->curr_index = !frame->curr_index;
    frame->prev_exists = true;
}

static void recvFrame_get_buf(
        recvFrame_t* const restrict       frame,
        uint8_t* restrict* const restrict buf,
        unsigned* const restrict          nbytes)
{
    *buf = frame->buf;
    *nbytes = sizeof(frame->buf);
}

/******************************************************************************
 * NBS transport-layer frame for sending products:
 ******************************************************************************/

typedef struct {
    fh_t         fh;
    pdh_t        pdh;
    psh_t        psh;
    #define IOVEC_FH    0 ///< Frame header
    #define IOVEC_PDH   1 ///< Product-definition header
    #define IOVEC_PSH   2 ///< Product-specific header
    #define IOVEC_BLOCK 3 ///< Data block
    #define IOVCNT      4 ///< Number of contiguous segments
    struct iovec iovec[IOVCNT];
} sendFrame_t;

/**
 * Initializes a sending-frame.
 *
 * @param[out] send_frame  Sending-frame
 */
static inline void sendFrame_init(
        sendFrame_t* const send_frame)
{
    fh_init(&send_frame->fh);
    pdh_init(&send_frame->pdh);
    psh_init(&send_frame->psh);

    struct iovec* iovec = send_frame->iovec;
    iovec[IOVEC_FH].iov_base = fh_get_buf(&send_frame->fh);
    iovec[IOVEC_FH].iov_len = fh_get_length();

    iovec[IOVEC_PDH].iov_base = pdh_get_buf(&send_frame->pdh);
    iovec[IOVEC_PDH].iov_len = pdh_get_length();

    iovec[IOVEC_PSH].iov_base = psh_get_buf(&send_frame->psh);
}

/**
 * Configures a sending-frame for the start of a product.
 *
 * @param[out] send_frame        Sending-frame
 * @param[in]  data_stream       Channel (i.e., data stream) identifier. One of
 *                                 - 1 GOES East
 *                                 - 2 GOES West
 *                                 - 3 Reserved
 *                                 - 4 Non-GOES Imagery/DCP
 *                                 - 5 NCEP/NWSTG
 *                                 - 6 Reserved
 *                                 - 7 Reserved
 *                               See <http://www.nws.noaa.gov/noaaport/html/transprt.shtml>.
 * @param[in]  recs_per_block    Number of records in canonical data-block
 * @param[in]  bytes_per_record  Number of bytes in an uncompressed, serialized
 *                               data-record
 * @param[in]  prod_type         Type of product
 * @param[in]  num_blocks        Total number of data-blocks in product
 * @param[in]  is_compressed     Is data-block zlib(3)-compressed?
 */
static void sendFrame_start(
        sendFrame_t* const  send_frame,
        const int           data_stream,
        const unsigned      recs_per_block,
        const unsigned      bytes_per_record,
        const int           prod_type,
        const int           num_blocks,
        const bool          is_compressed)
{
    psh_start(&send_frame->psh, bytes_per_record, prod_type, num_blocks);
    pdh_start(&send_frame->pdh, PSH_LENGTH, recs_per_block, num_blocks,
            is_compressed);
    fh_start(&send_frame->fh, data_stream);
    send_frame->iovec[IOVEC_PSH].iov_len = psh_get_length(); // PSH in 1st frame
}

static nbs_status_t sendFrame_send(
        sendFrame_t* const restrict   send_frame,
        const uint8_t* const restrict block,
        const unsigned                nbytes,
        nbsl_t* const restrict        nbsl)
{
    int            status;
    const unsigned num_fragments = psh_get_num_fragments(&send_frame->psh);
    if (pdh_get_block_num(&send_frame->pdh) >= num_fragments) {
        log_add("Too many data-blocks: expected=%u", num_fragments);
        status = NBS_STATUS_LOGIC;
    }
    else {
        fh_compute_and_set_checksum(&send_frame->fh);
        pdh_set_data_size(&send_frame->pdh, nbytes);
        struct iovec* const iovec = send_frame->iovec;
        // Safe cast because only reading
        iovec[IOVEC_BLOCK].iov_base = (void*)block;
        iovec[IOVEC_BLOCK].iov_len = nbytes;
        status = nbsl_send(nbsl, iovec, IOVCNT);
        if (status)
            log_add("nbsl_send() failure");
    }
    return status;
}

/**
 * Advances a sending-frame for the next time it's passed to sendFrame_send()
 * for the same product.
 *
 * @param[in,out] frame       Sending-frame
 */
static void sendFrame_advance(
        sendFrame_t* const send_frame)
{
    pdh_advance(&send_frame->pdh, psh_get_num_fragments(&send_frame->psh));
    fh_advance(&send_frame->fh);
    send_frame->iovec[IOVEC_PSH].iov_len = 0; // PSH in first frame only
}

/******************************************************************************
 * NBS Transport Layer:
 ******************************************************************************/

struct nbst {
    nbsl_t*      nbsl;            ///< NBS link-layer object
    nbsp_t*      nbsp;            ///< NBS presentation-layer object
    sendFrame_t  send_frame;      ///< NBS transport-layer frame for sending
    recvFrame_t recv_frame;      ///< NBS transport-layer frame for receiving
                                  ///< products
    bool         start_processed; ///< Has start of product been processed?
};

/**
 * Processes the data-block of an NBS frame from the link-layer to the
 * presentation-layer.
 *
 * @param[in] nbst               NBS transport-layer object
 * @param[in] buf                Product data
 * @param[in] buflen             Number of bytes in `buf`
 * @retval    0                  Success
 * @retval    NBS_STATUS_INVAL   `headers` or `buf` is invalid. log_add()
 *                               called.
 * @retval    NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval    NBS_STATUS_UNSUPP  Unsupported product. log_add() called.
 * @retval    NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
static nbs_status_t nbst_recv_data(
        nbst_t* const restrict          nbst,
        const uint8_t* const restrict   buf,
        const unsigned                  buflen)
{
    int                      status;
    const recvFrame_t* const frame = &nbst->recv_frame;
    const unsigned           nbytes = recvFrame_get_data_size(frame);
    if (nbytes > buflen) {
        log_add("Data-block too small: actual=%u, expected=%u", buflen, nbytes);
        status = NBS_STATUS_INVAL;
    }
    else {
        const unsigned          type = recvFrame_get_product_type(frame);
        const bool              is_start = recvFrame_is_product_start(frame);
        const bool              is_end = recvFrame_is_product_end(frame);
        const bool              is_compressed = recvFrame_is_compressed(frame);
        nbsp_t* const           nbsp = nbst->nbsp;
        switch (type) {
        case PROD_TYPE_GOES_EAST:
        case PROD_TYPE_GOES_WEST: {
            int num_frag = recvFrame_get_num_fragments(frame);
            num_frag = NBST_MAX(num_frag, 1); // `num_frag` could be `-1`
            status = is_start
                    ? nbsp_recv_gini_start(nbsp, buf, nbytes,
                            recvFrame_get_rec_len(frame),
                            recvFrame_get_recs_per_block(frame), is_compressed,
                            recvFrame_get_type(frame),
                            num_frag * 5120) // 5120 == data-block max size
                    : nbsp_recv_gini_block(nbsp, buf, nbytes,
                            recvFrame_get_block_num(frame), is_compressed);
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
    return status;
}

/**
 * Transfers a non-synchronization frame from the link-layer to the
 * presentation-layer.
 *
 * @param[in,out] nbst         NBS transport-layer object
 * @param[in]     buf          Rest of frame after NBS transport-layer headers
 * @param[in]     nbytes       Number of bytes in `buf`
 * @retval 0                   Success
 * @retval NBS_STATUS_INVAL    `nbst->headers` or `buf` is invalid. log_add()
 *                             called.
 * @retval NBS_STATUS_LOGIC    Logic error. log_add() called.
 * @retval NBS_STATUS_NOSTART  No start-frame for product was seen. log_add()
 *                             called iff frame is for different product.
 * @retval NBS_STATUS_UNSUPP   Unsupported product. log_add() called.
 * @retval NBS_STATUS_SYSTEM   System failure. log_add() called.
 */
static nbs_status_t nbst_recv_non_sync_frame(
        nbst_t* const restrict        nbst,
        const uint8_t* const restrict buf,
        const unsigned                nbytes)
{
    int status;
    if (recvFrame_is_start(&nbst->recv_frame)) {
        nbsp_recv_end(nbst->nbsp);
        status = nbst_recv_data(nbst, buf, nbytes);
        nbst->start_processed = status == 0;
    }
    else if (recvFrame_is_same_product(&nbst->recv_frame)) {
        if (!nbst->start_processed) {
            status = NBS_STATUS_NOSTART;
        }
        else {
            status = nbst_recv_data(nbst, buf, nbytes);
        }
    }
    else {
        log_add("No start-frame received for product: recv_frame=%s",
                recvFrame_format(&nbst->recv_frame));
        nbst->start_processed = false;
        status = NBS_STATUS_NOSTART;
    }
    return status;
}

/******************************************************************************
 * Public:
 ******************************************************************************/

/**
 * Returns a new NBS transport-layer object.
 *
 * @param[out] nbst              New NBS transport-layer object
 *                               Will _not_ be freed by nbst_free().
 * @retval     0                 Success. `*nbst` is set.
 * @retval     NBS_STATUS_INVAL  `nbst == NULL`. log_add() called.
 * @retval     NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbst_new(
        nbst_t** const restrict nbst)
{
    int status;
    if (nbst == NULL) {
        log_add("Invalid argument: nbst=%p", nbst);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbst_t* const obj = log_malloc(sizeof(nbst_t),
                "NBS transport-layer object");
        if (obj == NULL) {
            status = NBS_STATUS_NOMEM;
        }
        else {
            recvFrame_init(&obj->recv_frame);
            obj->nbsp = NULL;
            obj->nbsl = NULL;
            obj->start_processed = false;
            *nbst = obj;
            status = 0;
        }
    }
    return status;
}

/**
 * Configures an NBS transport-layer object for sending products by setting its
 * NBS link-layer object.
 *
 * @param[out] nbst          NBS transport-layer object
 * @param[in]  nbsl          NBS link-layer object
 * @retval     0             Success
 * @retval NBS_STATUS_INVAL  `nbst == NULL || nbsl == NULL`. log_add() called.
 * @retval NBS_STATUS_LOGIC  Link-layer object already set. log_add() called.
 */
nbs_status_t nbst_set_link_layer(
        nbst_t* const restrict nbst,
        nbsl_t* const restrict nbsl)
{
    int status;
    if (nbst == NULL || nbsl == NULL) {
        log_add("Invalid argument: nbst=%p, nbsl=%p", nbst, nbsl);
        status = NBS_STATUS_INVAL;
    }
    else if (nbst->nbsl) {
        log_add("NBS link-layer already set");
        status = NBS_STATUS_LOGIC;
    }
    else {
        sendFrame_init(&nbst->send_frame);
        nbst->nbsl = nbsl;
        status = 0;
    }
    return status;
}

/**
 * Sets the associated NBS presentation-layer object of an NBS transport-layer
 * object.
 *
 * @param[out] nbst               NBS transport-layer object
 * @param[in]  nbsp               NBS presentation-layer object
 * @retval     0                  Success
 * @retval     NBS_STATUS_INVAL  `nbst == NULL || nbsp == NULL`. log_add()
 *                                called.
 */
nbs_status_t nbst_set_presentation_layer(
        nbst_t* const restrict nbst,
        nbsp_t* const restrict nbsp)
{
    int status;
    if (nbst == NULL || nbsp == NULL) {
        log_add("Invalid argument: nbst=%p, nbsp=%p", nbst, nbsp);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbst->nbsp = nbsp;
        status = 0;
    }
    return status;
}

/**
 * Frees the internal resources of an NBS transport-layer object.
 *
 * @param[in]  nbst  NBS transport-layer object or `NULL`.
 */
void nbst_free(
        nbst_t* const nbst)
{
    free(nbst);
}

/**
 * Receives an NBS frame.
 *
 * @param[in,out] nbst               NBS transport-layer object
 * @param[in]     buf                Serialized NBS frame
 * @param[in]     nbytes             Number of bytes in `buf`
 * @retval        0                  Success
 * @retval        NBS_STATUS_INVAL   `buf` is invalid. log_add() called.
 * @retval        NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval        NBS_STATUS_NOSTART No start-frame for product was seen.
 *                                   log_add() called iff frame is for different
 *                                   product.
 * @retval        NBS_STATUS_UNSUPP  Unsupported product type. log_add() called.
 * @retval        NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
nbs_status_t nbst_recv(
        nbst_t* const restrict  nbst,
        const uint8_t* restrict buf,
        unsigned                nbytes)
{
    unsigned n;
    int      status = recvFrame_deserialize(&nbst->recv_frame, buf, nbytes, &n);
    if (status == 0) {
        if (!recvFrame_is_sync_frame(&nbst->recv_frame)) {
            buf += n;
            nbytes -= n;
            status = nbst_recv_non_sync_frame(nbst, buf, nbytes);
        }
        recvFrame_switch_headers(&nbst->recv_frame);
    }
    return status;
}

/**
 * Returns the space for receiving a serialized frame.
 *
 * @param[in,out] nbst       NBS transport-layer object
 * @param[out]    frame      Frame buffer
 * @param[out]    nbytes     Number of bytes in `frame`
 * @retval 0                 Success. `*frame` and `*nbytes` are set.
 * @retval NBS_STATUS_INVAL  `nbst == NULL || frame == NULL || nbytes == NULL`.
 *                           log_add() called.
 */
nbs_status_t nbst_get_recv_frame_buf(
        nbst_t* const restrict            nbst,
        uint8_t* restrict* const restrict frame,
        unsigned* const restrict          nbytes)
{
    int status;
    if (nbst == NULL || frame == NULL || nbytes == NULL) {
        log_add("Invalid argument: nbst=%p, frame=%p, nbytes=%p", nbst, frame,
                nbytes);
        status = NBS_STATUS_INVAL;
    }
    else {
        recvFrame_get_buf(&nbst->recv_frame, frame, nbytes);
        status = 0;
    }
    return status;
}

/**
 * Accepts notification of the end of input from the link-layer.
 *
 * @param[in] nbst            NBS transport-layer object
 * @retval    0               Success
 * @retval NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval NBS_STATUS_NOMEM   Out of memory. log_add() called.
 * @retval NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
nbs_status_t nbst_recv_end(
        nbst_t* const nbst)
{
    return nbsp_recv_end(nbst->nbsp);
}

/**
 * Starts the process of sending a product.
 *
 * @param[in] nbst              NBS transport-layer
 * @param[in] recs_per_block    Number of records in a canonical block
 * @param[in] bytes_per_record  Number of bytes in a canonical record
 * @param[in] prod_type         Type of product. One of
 *                                - 1 GOES East
 *                                - 2 GOES West
 *                                - 3 Non-GOES Imagery/DCP
 *                                - 4 NCEP/NWSTG
 *                                - 5 NEXRAD
 * @param[in] num_blocks        Number of blocks (including the start-block)
 * @param[in] is_compressed     Are the data-blocks zlib(3)-compressed?
 */
void nbst_send_start(
    nbst_t* const restrict nbst,
    const unsigned         recs_per_block,
    const unsigned         bytes_per_record,
    const int              prod_type,
    const int              num_blocks,
    const bool             is_compressed)
{
    unsigned data_stream = (prod_type == PROD_TYPE_GOES_EAST)
            ? 1
            : (prod_type == PROD_TYPE_GOES_WEST)
              ? 2
              : (prod_type == PROD_TYPE_NESDIS_NONGOES)
                  ? 4
                  : (prod_type == PROD_TYPE_NWSTG)
                    ? 5  // Also NCEP
                    : 6; // Reserved
    sendFrame_start(&nbst->send_frame, data_stream, recs_per_block,
            bytes_per_record, prod_type, num_blocks, is_compressed);
}

/**
 * Sends a data-block.
 *
 * @param[in] nbst           NBS transport-layer object
 * @param[in] block          Data-block to send
 * @param[in] nbytes         Number of bytes in `block`
 * @retval 0                 Success
 * @retval NBS_STATUS_LOGIC  Logic error. log_add() called.
 */
nbs_status_t nbst_send_block(
        nbst_t* const restrict        nbst,
        const uint8_t* const restrict block,
        const unsigned                nbytes)
{
    int status = sendFrame_send(&nbst->send_frame, block, nbytes, nbst->nbsl);
    if (status) {
        log_add("Couldn't send frame");
    }
    else {
        sendFrame_advance(&nbst->send_frame);
        status = 0;
    }
    return status;
}
