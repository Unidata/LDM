/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_presentation.c
 * @author: Steven R. Emmerson
 *
 * This file implements the NOAAPort Broadcast System (NBS) presentation layer.
 */

#include "config.h"

#include "decode.h"
#include "log.h"
#include "nbs_presentation.h"
#include "nport.h"

#include <limits.h>
#include <stdint.h>
#include <zlib.h>

#define NBSP_MIN(a, b) ((a) < (b) ? (a) : (b))
/**
 * Product-definition block (not be be confused with the NBS transport layer's
 * product-definition header):
 */
typedef struct {
    uint_fast32_t source;
    uint_fast32_t creating_entity;
    uint_fast32_t sector_id;
    uint_fast32_t physical_element;
    uint_fast32_t num_logical_recs;
    uint_fast32_t logical_rec_size;
    uint_fast32_t year;
    uint_fast32_t month;
    uint_fast32_t day;
    uint_fast32_t hour;
    uint_fast32_t minute;
    uint_fast32_t second;
    uint_fast32_t centisecond;
    uint_fast32_t nx;
    uint_fast32_t ny;
    uint_fast32_t image_res;
} pdb_t;

/*
 * Maximum number of characters in a WMO header:
 *     T1T2A1A2ii (sp) CCCC (sp) YYGGgg [(sp)BBB] (cr)(cr)(lf)
 */
#define WMO_HEADER_MAX_LEN 25
typedef char wmo_header_t[WMO_HEADER_MAX_LEN-3+1]; // minus \r\r\n plus \0

/******************************************************************************
 * Utilities:
 ******************************************************************************/

static nbsp_status_t decompress(
        const uint8_t* const restrict deflate_buf,
        const unsigned                deflate_nbytes,
        uint8_t* const restrict       inflate_buf,
        const size_t                  inflate_size,
        unsigned* const restrict      inflate_nbytes)
{
    z_stream d_stream;
    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree  = (free_func)0;
    d_stream.opaque = (voidpf)0;

    int status = inflateInit(&d_stream);
    if (status) {
        log_add("zlib::inflateInit() failure");
        status = NBSP_STATUS_SYSTEM;
    }
    else {
        d_stream.next_in   = (uint8_t*)deflate_buf;
        d_stream.avail_in  = NBSP_MIN(deflate_nbytes, 540);
        d_stream.next_out  = inflate_buf;
        d_stream.avail_out = inflate_size;

        status = inflate(&d_stream, Z_NO_FLUSH);
        if (status != Z_STREAM_END) {
            log_add("zlib::inflate() failure: too much compressed data");
            (void)inflateEnd(&d_stream);
            status = NBSP_STATUS_SYSTEM;
        }
        else {
            (void)inflateEnd(&d_stream);
            *inflate_nbytes = d_stream.total_out;
            status = 0;
        }
    }
    return status;
}

/******************************************************************************
 * Dynamic buffer:
 ******************************************************************************/

typedef struct {
    size_t   max;
    size_t   used;
    uint8_t* buf;
} dynabuf_t;

static nbsp_status_t dynabuf_init(
        dynabuf_t* const dynabuf,
        const size_t     nbytes)
{
    int   status;
    dynabuf->buf = log_malloc(nbytes, "dynamic buffer's buffer");
    if (dynabuf->buf == NULL) {
        status = NBSP_STATUS_NOMEM;
    }
    else {
        dynabuf->max = nbytes;
        dynabuf->used = 0;
        status = 0;
    }
    return status;
}

static nbsp_status_t dynabuf_reserve(
        dynabuf_t* const dynabuf,
        const size_t     nbytes)
{
    int          status;
    const size_t max = dynabuf->used + nbytes;
    if (max <= dynabuf->max) {
        status = 0;
    }
    else {
        uint8_t* const buf = realloc(dynabuf->buf, max);
        if (buf == NULL) {
            log_add("Couldn't re-allocate %zu bytes for dynamic buffer's "
                    "buffer");
            status = NBSP_STATUS_NOMEM;
        }
        else {
            dynabuf->buf = buf;
            dynabuf->max = max;
            status = 0;
        }
    }
    return status;
}

static nbsp_status_t dynabuf_add(
        dynabuf_t* const restrict     dynabuf,
        const uint8_t* const restrict buf,
        const size_t                  nbytes)
{
    int status = dynabuf_reserve(dynabuf, 2 * (dynabuf->used + nbytes));
    if (status == 0) {
        (void)memcpy(dynabuf->buf + dynabuf->used, buf, nbytes);
        dynabuf->used += nbytes;
    }
    return status;
}

static nbsp_status_t dynabuf_set(
        dynabuf_t* const restrict dynabuf,
        const int                 byte,
        const size_t              nbytes)
{
    int status = dynabuf_reserve(dynabuf, nbytes);
    if (status == 0) {
        (void)memset(dynabuf->buf+dynabuf->used, byte, nbytes);
        dynabuf->used += nbytes;
    }
    return status;
}

static inline uint8_t* dynabuf_get_buf(
        const dynabuf_t* const dynabuf)
{
    return dynabuf->buf;
}

static inline size_t dynabuf_get_used(
        const dynabuf_t* const dynabuf)
{
    return dynabuf->used;
}

static void dynabuf_clear(
        dynabuf_t* const dynabuf)
{
    dynabuf->used = 0;
}

static void dynabuf_fini(
        dynabuf_t* dynabuf)
{
    free(dynabuf->buf);
}

/******************************************************************************
 * WMO Header:
 ******************************************************************************/

static nbsp_status_t wmoheader_decode(
        char* const restrict          wmo_header,
        const uint8_t* restrict const buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int               status;
    const char*       cp = buf;
    const char* const out = buf + NBSP_MIN(WMO_HEADER_MAX_LEN, nbytes);
    int               nchar = 0;
    for (; cp < out && *cp != '\n'; cp++) {
        if (*cp != '\r')
            wmo_header[nchar++] = *cp;
    }
    if (cp == out) {
        log_add("No newline character in WMO header");
        status = NBSP_STATUS_INVAL;
    }
    else {
        wmo_header[nchar] = 0;
        *nscanned = cp + 1 - (const char*)buf;
    }
    return status;
}

/******************************************************************************
 * Image Product-Definition Block:
 ******************************************************************************/

static nbsp_status_t pdb_decode(
        pdb_t* const restrict         pdb,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int status;
    /*
     * Always 512 bytes according to
     * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf>
     */
    if (nbytes < 512) {
        log_add("Product-definition block shorter than 512 bytes: %u",
                nbytes);
        status = NBSP_STATUS_INVAL;
    }
    else {
        pdb->source = buf[0];
        pdb->creating_entity = buf[1];
        pdb->sector_id = buf[2];
        pdb->physical_element = buf[3];
        pdb->num_logical_recs = decode_uint16(buf+4);
        pdb->logical_rec_size = decode_uint16(buf+6);
        pdb->year = ((buf[8] > 70) ? 1900 : 2000) + buf[8];
        pdb->month = buf[9];
        pdb->day = buf[10];
        pdb->hour = buf[11];
        pdb->minute = buf[12];
        pdb->second = buf[13];
        pdb->centisecond = buf[14];
        pdb->nx = decode_uint16(buf+16);
        pdb->ny = decode_uint16(buf+18);
        pdb->image_res = buf[41];
        *nscanned = 512;
        status = 0;
    }
    return status;
}

/******************************************************************************
 * NESDIS Information (WMO Header and Product-Definition Block):
 ******************************************************************************/

static nbsp_status_t nesdis_headers_decode(
        char* const restrict     wmo_header,
        pdb_t* const restrict    pdb,
        const uint8_t* restrict  buf,
        unsigned                 nbytes,
        unsigned* const restrict nscanned)
{
    unsigned n;
    int      status = wmoheader_decode(wmo_header, buf, nbytes, &n);
    if (status) {
        log_add("Couldn't decode NESDIS WMO header");
    }
    else {
        *nscanned = n;
        buf += n;
        nbytes -= n;
        status = pdb_decode(pdb, buf, nbytes, &n);
        if (status) {
            log_add("Couldn't decode NESDIS product-definition block");
        }
        else {
            *nscanned += n;
        }
    }
    return status;
}

/******************************************************************************
 * NESDIS-formatted products:
 ******************************************************************************/

typedef struct {
    dynabuf_t*     dynabuf;          ///< Dynamic buffer for accumulating product
    pdb_t          pdb;              ///< Product-definition block
    uint8_t        buf[6000];        ///< De-compression buffer
    wmo_header_t   wmo_header;       ///< WMO header: T1T2A1A2ii CCCC YYGGgg [BBB]
    unsigned       prev_block_index; ///< Index of previous block
    unsigned       recs_per_block;   ///< Number of records (i.e., scan lines)
                                     ///< in a data-block
    bool           started;          ///< Has product accumulation started?
    bool           compress;         ///< Should compression be ensured?
} nesdis_t;

static void nesdis_init(
        nesdis_t* const restrict  nesdis,
        dynabuf_t* const restrict dynabuf)
{
    log_assert(nesdis != NULL);
    log_assert(dynabuf != NULL);
    nesdis->dynabuf = dynabuf;
    nesdis->started = false;
}

static nbsp_status_t nesdis_ensure_decompressed_headers(
        nesdis_t* const restrict                nesdis,
        const uint8_t* restrict                 buf,
        unsigned                                nbytes,
        const bool                              is_compressed,
        const uint8_t* restrict* const restrict info_buf,
        unsigned* const restrict                info_nbytes)
{
    int status;
    if (!is_compressed) {
        *info_buf = buf;
        *info_nbytes = nbytes;
        nesdis->compress = false;
        status = 0;
    }
    else {
        status = decompress(buf, nbytes, nesdis->buf, sizeof(nesdis->buf),
                info_nbytes);
        if (status) {
            log_add("Couldn't uncompress start of image");
        }
        else {
            *info_buf = nesdis->buf;
            nesdis->compress = true;
        }
    }
    return status;
}

static nbsp_status_t nesdis_start(
        nesdis_t* const restrict nesdis,
        const uint8_t* restrict  buf,
        unsigned                 nbytes,
        const unsigned           recs_per_block,
        const bool               is_compressed)
{
    int status;
    if (nesdis->started) {
        log_add("NESDIS product already started");
        status = NBSP_STATUS_STATE;
    }
    else {
        status = nesdis_ensure_decompressed_headers(nesdis, buf, nbytes,
                is_compressed, &buf, &nbytes);
        if (status == 0) {
            unsigned n;
            status = nesdis_headers_decode(nesdis->wmo_header, &nesdis->pdb,
                    buf, nbytes, &n);
            if (status == 0) {
                status = dynabuf_add(nesdis->dynabuf, buf, nbytes);
                if (status == 0) {
                    nesdis->prev_block_index = 0;
                    nesdis->recs_per_block = recs_per_block;
                    nesdis->started = true;
                }
            }
        }
    }
    return status;
}

static nbsp_status_t nesdis_is_ready(
        nesdis_t* const nesdis)
{
    return nesdis->started ? 0 : NBSP_STATUS_STATE;
}

static nbsp_status_t nesdis_add(
        nesdis_t* const restrict      nesdis,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_compressed)
{
    return -1;
}

/**
 *
 * @param nesdis
 * @param curr_block_index
 * @retval NBSP_STATUS_INVAL  `curr_block_index <= nesdis->prev_block_index`
 */
static nbsp_status_t nesdis_add_missing_blocks(
        nesdis_t* const nesdis,
        const unsigned  curr_block_index)
{
    log_assert(nesdis->started);
    int status;
    if (curr_block_index <= nesdis->prev_block_index) {
        status = NBSP_STATUS_INVAL;
    }
    else {
        unsigned nblocks_missing = curr_block_index - nesdis->prev_block_index - 1;
        unsigned nrecs_missing = nblocks_missing * nesdis->recs_per_block;
        unsigned nbytes_missing = nrecs_missing * nesdis->pdb.logical_rec_size;
        status = dynabuf_set(nesdis->dynabuf, 0, nbytes_missing);
        nesdis->prev_block_index = curr_block_index;
    }
    return status;
}

/******************************************************************************
 * NBS Presentation-Layer:
 ******************************************************************************/

struct nbsp {
    nesdis_t  nesdis;   ///< NBS NESDIS product
    dynabuf_t dynabuf;  ///< Dynamic buffer for accumulating product
    unsigned  type;     ///< Type of product-in-progress
};

static nbsp_status_t nbsp_is_ready(
        nbsp_t* const nbsp)
{
    int status = nbsp->type ? NBSP_STATUS_STATE : 0;
    if (status)
        log_add("nbsp_end_product() not called");
    return status;
}

nbsp_status_t nbsp_new(
        nbsp_t* restrict* const restrict nbsp)
{
    int status;
    if (nbsp == NULL) {
        log_add("NULL argument: nbsp=%p", nbsp);
        status = NBSP_STATUS_INVAL;
    }
    else {
        nbsp_t* const ptr = log_malloc(sizeof(nbsp),
                "NBS presentation-layer object");
        if (ptr == NULL) {
            status = NBSP_STATUS_NOMEM;
        }
        else {
            status = dynabuf_init(&ptr->dynabuf, 10000);
            if (status == 0) {
                nesdis_init(&ptr->nesdis, &ptr->dynabuf);
                ptr->type = 0; // No product-in-progress
                *nbsp = ptr;
            }
        }
    }
    return status;
}

/**
 * Ends the current product. Does nothing if there's no current product.
 * Idempotent.
 */
void nbsp_end_product(
        nbsp_t* const nbsp)
{
}

/**
 * Processes the start of a NESDIS-formatted product.
 *
 * @pre       nbsp_end_product() called on previous product
 * @param[in] nbsp               NBS presentation-layer object
 * @param[in] buf                Encoded and possibly compressed data block
 * @param[in] nbytes             Number of bytes in `buf`
 * @param[in] recs_per_block     Number of records in data block
 * @param[in] is_compressed      Is `buf` zlib(3) compressed by NBS
 *                               transport layer?
 * @param[in] size_estimate      Size estimate of product in bytes
 * @retval    0                  Success
 * @retval    NBSP_STATUS_INVAL  Invalid `buf`. log_add() called.
 * @retval    NBSP_STATUS_STATE  `nbsp` in wrong state. log_add() called.
 */
nbsp_status_t nbsp_nesdis_start(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                recs_per_block,
        const bool                    is_compressed,
        size_t                        size_estimate)
{
    int status = nbsp_is_ready(nbsp);
    if (status == 0) {
        dynabuf_t* const dynabuf = &nbsp->dynabuf;
        dynabuf_clear(dynabuf);
        status = dynabuf_reserve(dynabuf, size_estimate);
        if (status == 0) {
            status = nesdis_start(&nbsp->nesdis, buf, nbytes, recs_per_block,
                    is_compressed);
            if (status)
                log_add("Couldn't start NESDIS product");
        }
    }
    return status;
}

/**
 * Processes a NESDIS-formatted block of data.
 *
 * @pre       nbsp_nesdis_start() called
 * @param[in] nbsp               NBS presentation-layer object
 * @param[in] buf                Encoded and possibly compressed NESDIS data
 *                               block
 * @param[in] nbytes             Number of bytes in `buf`
 * @param[in] block_index        Origin-1 data-block index of `buf`
 * @param[in] is_compressed      Is `buf` zlib(3) compressed by the NBS
 *                               transport layer?
 * @retval    0                  Success
 * @retval    NBSP_STATUS_INVAL  Invalid `buf`. log_add() called.
 * @retval    NBSP_STATUS_STATE  `nbsp` in wrong state. log_add() called.
 */
nbsp_status_t nbsp_nesdis_block(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                block_index,
        const bool                    is_compressed)
{
    nesdis_t* const nesdis = &nbsp->nesdis;
    int             status = nesdis_is_ready(nesdis);
    if (status == 0) {
        status = nesdis_add_missing_blocks(nesdis, block_index);
        if (status) {
            log_add("Couldn't add missing NESDIS records (i.e., scan lines)");
        }
        else {
            status = nesdis_add(nesdis, buf, nbytes, is_compressed);
            if (status)
                log_add("Couldn't add NESDIS data-block");
        }
    }
    return status;
}

nbsp_status_t nbsp_nongoes(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end,
        const bool                    is_compressed)
{
    return -1;
}

nbsp_status_t nbsp_nwstg(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end)
{
    return -1;
}

nbsp_status_t nbsp_nexrad(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end)
{
    return -1;
}

void nbsp_free(
        nbsp_t* const nbsp)
{
    if (nbsp) {
        dynabuf_fini(&nbsp->dynabuf);
        free(nbsp);
    }
}
