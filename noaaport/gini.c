/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: gini.c
 * @author: Steven R. Emmerson
 *
 * This file implements a GINI product. A GINI product comprises multiple blocks.
 * The first block is the GINI product-definition block. Subsequent data-blocks
 * contain the number of scan-lines specified in the product-definition block
 * (except for the last data-block which might have fewer). The last block of
 * GINI prooduct is an end-of-product block containing a single, synthetic
 * scan-line. All blocks are either uncompressed or zlib(3)-compressed.
 */

#include "config.h"

#include "decode.h"
#include "dynabuf.h"
#include "gini.h"
#include "ldm.h"
#include "log.h"
#include "nbs.h"
#include "nport.h"

#include <search.h>
#include <stdbool.h>
#include <zlib.h>

/*
 * Maximum number of characters in a WMO header:
 *     T1T2A1A2ii (sp) CCCC (sp) YYGGgg [(sp)BBB] (cr)(cr)(lf)
 */
#define WMO_HEADER_MAX_ENCODED_LEN 25
typedef char wmo_header_t[WMO_HEADER_MAX_ENCODED_LEN-3+1]; // minus \r\r\n plus \0

#define GINI_MIN(a, b) ((a) <= (b) ? (a) : (b))

/******************************************************************************
 * Block of (possibly compressed) data::
 ******************************************************************************/

typedef struct {
    uint8_t* data;           ///< Pixel values
    unsigned nbytes;         ///< Number of _uncompressed_ bytes
    unsigned data_len;       ///< Number of bytes in `data`
    bool     is_compressed;  ///< Is `data` zlib(3)-compressed?
} block_t;

/**
 * Initializes a block of data.
 *
 * @param[out] block          Data block
 * @param[in]  data           Pixel data
 * @param[in]  nbytes         Number of uncompressed bytes
 * @param[in]  data_len       Number of bytes in `data`
 * @param[in]  is_compressed  Is `data` zlib(3)-compressed?
 */
static inline void block_init(
        block_t* const restrict block,
        uint8_t* const restrict data,
        const unsigned          nbytes,
        const unsigned          data_len,
        const bool              is_compressed)
{
    block->data = data;
    block->nbytes = nbytes;
    block->data_len = data_len;
    block->is_compressed = is_compressed;
}

/******************************************************************************
 * GINI Image Gap:
 *
 * Used for missing scan-lines in a GINI product.
 ******************************************************************************/

typedef block_t gap_t;

/**
 * Returns a new gap.
 *
 * @param[out] gap                 New gap
 * @param[in]  nbytes              Number of uncompressed bytes in `gap`
 * @param[in]  compressed          Should the gap be zlib(3) compressed?
 * @retval     0                   Success
 * @retval     GINI_STATUS_NOMEM   Out of memory. log_add() called.
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
static gini_status_t gap_new(
        gap_t** const   gap,
        const unsigned  nbytes,
        const bool      compressed)
{
    int status;
    gap_t* obj = log_malloc(sizeof(gap_t), "gap");
    if (obj == NULL) {
        status = GINI_STATUS_NOMEM;
    }
    else {
        obj->data = log_malloc(nbytes, "gap");
        if (obj->data == NULL) {
            status = GINI_STATUS_NOMEM;
        }
        else {
            obj->nbytes = nbytes;
            obj->is_compressed = compressed;
            if (!compressed) {
                (void)memset(obj->data, 255, nbytes);
                obj->data_len = nbytes;
                status = 0;
            }
            else {
                uint8_t buf[nbytes];
                /*
                 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf>
                 * says "For all remapped products the binary value 255 is
                 * reserved to indicate missing or bad data."
                 */
                (void)memset(buf, 255, nbytes);
                status = gini_pack(buf, nbytes, obj->data, nbytes,
                        &obj->data_len);
                if (status)
                    log_add("Couldn't compress %u-byte blank gap", nbytes);
            }
            if (status)
                free(obj->data);
        } // `obj->data` allocated
        if (status) {
            free(obj);
        }
        else {
            *gap = obj;
        }
    } // `obj` allocated
    return status;
}

/**
 * Frees a gap.
 *
 * @param[in] gap Gap
 */
static void gap_free(
        gap_t* const gap)
{
    if (gap) {
        free(gap->data);
        free(gap);
    }
}

/******************************************************************************
 * Gap Array:
 *
 * Contains an array of gaps -- one for each possible number of records
 * (i.e., scan lines) to replace (excluding 0).
 ******************************************************************************/

typedef struct {
    gap_t**   gaps;
    unsigned  rec_len;
    unsigned  max_recs;
    bool      compressed;
} ga_t;

/**
 * Initializes the key portion of a gap-array object; does _not_ set the value
 * portion.
 *
 * @param[in,out] ga                 Gap-array to initialize
 * @param[in]     rec_len            Number of uncompressed bytes in a record
 * @param[in]     max_recs           Maximum number of records (i.e., scan
 *                                   lines)
 * @param[in]     compressed         Should the blank lines be compressed?
 * @retval        0                  Success
 * @retval        GINI_STATUS_INVAL  `max_recs == 0`. log_add() called.
 */
static gini_status_t ga_init(
        ga_t* const    ga,
        const unsigned rec_len,
        const unsigned max_recs,
        const bool     compressed)
{
    int status;
    if (max_recs == 0) {
        log_add("Invalid argument: max_recs=%u", max_recs);
        status = GINI_STATUS_INVAL;
    }
    else {
        ga->gaps = NULL; // Set on-demand
        ga->rec_len = rec_len;
        ga->max_recs = max_recs;
        ga->compressed = compressed;
        status = 0;
    }
    return status;
}

/**
 * Returns a new gap-array object.
 *
 * @param[out] ga                 New gap-array
 * @param[in]  rec_len            Number of uncompressed bytes in a record
 *                                (i.e., scan line)
 * @param[in]  max_recs           Maximum number of records (i.e., scan
 *                                lines)
 * @param[in]  compressed         Should the blank lines be compressed?
 * @retval     0                  Success
 * @retval     GINI_STATUS_INVAL  `max_recs == 0`. log_add() called.
 * @retval     GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
static gini_status_t ga_new(
        ga_t** const   ga,
        const unsigned rec_len,
        const unsigned max_recs,
        const bool     compressed)
{
    int status;
    ga_t* obj = log_malloc(sizeof(ga_t), "gap-array");
    if (obj == NULL) {
        status = GINI_STATUS_NOMEM;
    }
    else {
        status = ga_init(obj, rec_len, max_recs, compressed);
        if (status) {
            log_add("Couldn't initialize gap-array");
            free(obj);
        }
        else {
            *ga = obj;
        }
    }
    return status;
}

/**
 * Compares two gap-arrays.
 *
 * @param[in] o1  First gap-array
 * @param[in] o2  Second gap-array
 * @return        A value less than, equal to, or greater than zero as the first
 *                gap-array is considered less than, equal to, or greater than
 *                the second gap-array, respectively
 */
static int ga_compare(
        const void* o1,
        const void* o2)
{
    const ga_t* br1 = o1;
    const ga_t* br2 = o2;
    return br1->rec_len < br2->rec_len
            ? -1
            : br1->rec_len > br2->rec_len
              ? 1
              : br1->max_recs < br2->max_recs
                ? -1
                : br1->max_recs > br2->max_recs
                  ? 1
                  : br1->compressed < br2->compressed
                    ? -1
                    : br1->compressed > br2->compressed
                      ? 1
                      : 0;
}

/**
 * Ensures that the array of pointers to gaps exists. The array is initialized
 * to `NULL` pointers if it's created.
 *
 * @param[in,out] ga                 Gap-array
 * @retval        0                  Success. `ga->gaps != NULL`.
 * @retval        GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
static gini_status_t ga_ensure_pointer_array(
        ga_t* const ga)
{
    int status = 0;
    if (ga->gaps == NULL) {
        gap_t** gaps = log_malloc(ga->max_recs*sizeof(gap_t*),
                "array of pointers to gaps");
        if (gaps == NULL) {
            status = GINI_STATUS_NOMEM;
        }
        else {
            for (unsigned i = 0; i < ga->max_recs; i++)
                gaps[i] = NULL;
            ga->gaps = gaps;
            status = 0;
        }
    }
    return status;
}

/**
 * Ensures that a gap of a given size exists in a gap-array.
 *
 * @param[in,out] ga                  Gap-array
 * @param[in]     nrecs               Number of records (i.e., scan lines) in
 *                                    the gap
 * @retval        0                   Success. `ga->gaps[nrec-1] !=
 *                                    NULL`.
 * @retval        GINI_STATUS_NOMEM   Out of memory. log_add() called.
 * @retval        GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
static gini_status_t ga_ensure_gap(
        ga_t* const    ga,
        const unsigned nrecs)
{
    int     status = 0;
    gap_t** gap = ga->gaps + nrecs - 1;
    if (*gap == NULL) {
        status = gap_new(gap, nrecs*ga->rec_len, ga->compressed);
        if (status)
            log_add("Couldn't create new gap: nrecs=%u", nrecs);
    }
    return status;
}

/**
 * Returns the gap corresponding to a given number of records (i.e., scan lines)
 * in a gap-array.
 *
 * @param[in]  ga                  Gap-array
 * @param[in]  nrecs               Number of records (i.e., scan lines)
 * @param[out] gap                 Returned gap
 * @retval     0                   Success. `*gap` is set.
 * @retval     GINI_STATUS_INVAL   `nrecs` is invalid. log_add() called.
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
static gini_status_t ga_get(
        ga_t* const           ga,
        const unsigned        nrecs,
        gap_t* const restrict gap)
{
    int status;
    log_assert(ga);
    log_assert(nrecs);
    log_assert(gap);
    if (nrecs > ga->max_recs) {
        log_add("Number of records (%u) > maximum possible (%u)", nrecs,
                ga->max_recs);
        status = GINI_STATUS_INVAL;
    }
    else {
        status = ga_ensure_pointer_array(ga);
        if (status == 0) {
            status = ga_ensure_gap(ga, nrecs);
            if (status == 0)
                *gap = *ga->gaps[nrecs-1];
        }
    }
    return status;
}

/**
 * Frees a gap-array
 *
 * @param[in,out] Gap-array
 */
static void ga_free(
        ga_t* ga)
{
    if (ga) {
        if (ga->gaps) {
            for (unsigned i = 0; i < ga->max_recs; i++)
                gap_free(ga->gaps[i]);
            free(ga->gaps);
        }
        free(ga);
    }
}

/******************************************************************************
 * Gap Database:
 *
 * A database of missing records (i.e., scan lines).
 ******************************************************************************/

typedef struct {
    void* root;
} gadb_t;

/**
 * Initializes a missing records database.
 *
 * @param[in,out] gadb  Database to be initialized
 */
static void gadb_init(
        gadb_t* const gadb)
{
    log_assert(gadb);
    gadb->root = NULL;
}

/**
 * Returns the gap-array corresponding to a given record length, maximum number
 * of records, and compression.
 *
 * @param[in]  gadb               Gap-array database
 * @param[in]  rec_len            Number of bytes in a record
 * @param[in]  max_recs           Maximum number of records
 * @param[in]  compressed         Should the gaps be compressed?
 * @param[out] ga                 Returned gap-array
 * @retval     0                  Success. `*ga` set.
 * @retval     GINI_STATUS_INVAL  `max_recs == 0`. log_add() called.
 * @retval     GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
static gini_status_t gadb_get(
        gadb_t* const restrict         gadb,
        const unsigned                 rec_len,
        const unsigned                 max_recs,
        const bool                     compressed,
        ga_t* restrict* const restrict ga)
{
    ga_t key; // NB: Transient and incomplete (no gaps)
    int  status = ga_init(&key, rec_len, max_recs, compressed);
    if (status == 0) {
        ga_t** node = tsearch(&key, &gadb->root, ga_compare);
        if (node == NULL) {
            log_add("Couldn't add blank gaps to database");
            status = GINI_STATUS_NOMEM;
        }
        else {
            status = 0;
            if (&key == *node) {
                /*
                 * The node was created. Replace its transient and incomplete
                 * value with a persistent and complete one that compares equal.
                 */
                ga_t* value;
                status = ga_new(&value, rec_len, max_recs, compressed);
                if (status == 0)
                    *node = value;
            }
            if (status == 0)
                *ga = *node;
        }
    } // `key` is valid
    return status;
}

/**
 * Finalizes a gap-array database.
 *
 * @param[in,out] gadb  Gap-array database to be finalized
 */
static void gadb_fini(
        gadb_t* gadb)
{
    while (gadb->root) {
        ga_t* ga = *(ga_t**)gadb->root;
        (void)tdelete(ga, &gadb->root, ga_compare);
        ga_free(ga);
    }
}

/******************************************************************************
 * Filler of Missing Records:
 *
 * Adds missing scan-lines to a GINI product.
 ******************************************************************************/

typedef struct {
    ga_t* ga; ///< Gap-array
} filler_t;

static gadb_t gadb;
static size_t filler_count = 0;

/**
 * Initializes a filler object.
 *
 * @param[in,out] filler         Object to be initialized
 * @param[in]     rec_len        Number of bytes in a record (i.e., scan line)
 * @param[in]     max_recs       Maximum number of records in a block
 * @param[in]     is_compressed  Should records be zlib(3)-compressed?
 * @retval 0                     Success
 * @retval GINI_STATUS_INVAL     `max_recs == 0`. log_add() called.
 * @retval GINI_STATUS_NOMEM     Out of memory. log_add() called.
 */
static gini_status_t filler_init(
        filler_t* const filler,
        const unsigned  rec_len,
        const unsigned  max_recs,
        const bool      is_compressed)
{
    if (filler_count++ == 0)
        gadb_init(&gadb);
    return gadb_get(&gadb, rec_len, max_recs, is_compressed, &filler->ga);
}

/**
 * Returns a gap.
 *
 * @param[in]  filler              Filler object
 * @param[in]  nrecs               Size of gap in records (i.e., scan lines)
 * @param[out] gap                 Returned gap
 * @retval     0                   Success. `*gap` is set.
 * @retval     GINI_STATUS_INVAL   `nrecs` is invalid. log_add() called.
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 * @retval     GINI_STATUS_NOMEM   Out of memory. log_add() called.
 */
static gini_status_t filler_get_gap(
        filler_t* const restrict filler,
        const unsigned           nrecs,
        gap_t* const restrict    gap)
{
    log_assert(filler);
    log_assert(filler->ga);
    int status = ga_get(filler->ga, nrecs, gap);
    if (status)
        log_add("Couldn't get gap: nrecs=%u", nrecs);
    return status;
}

/**
 * Finalizes a filler object.
 *
 * @param[in,out] filler  Object to be finalized
 */
static void filler_fini(
        filler_t* const filler)
{
    if (filler_count) {
        if (--filler_count == 0)
            gadb_fini(&gadb);
    }
    filler->ga = NULL;
}

/******************************************************************************
 * WMO Header:
 ******************************************************************************/

/**
 * De-serializes a WMO header.
 *
 * @param[out] wmo_header     De-serialized WMO header
 * @param[in]  buf            Serialized WMO header
 * @param[in]  nbytes         Number of bytes in `buf`
 * @param[out] nscanned       Number of bytes scanned
 * @retval 0                  Success. `*wmo_header` is set.
 * @retval GINI_STATUS_INVAL  Invalid WMO header
 */
static gini_status_t wmoheader_deserialize(
        char* const restrict          wmo_header,
        const uint8_t* restrict const buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int               status;
    const char*       cp = (const char*)buf;
    const char* const out = cp + GINI_MIN(WMO_HEADER_MAX_ENCODED_LEN, nbytes);
    int               nchar = 0;
    for (; cp < out && *cp != '\n'; cp++) {
        if (*cp != '\r')
            wmo_header[nchar++] = *cp;
    }
    if (cp == out) {
        log_add("No newline character in WMO header");
        status = GINI_STATUS_INVAL;
    }
    else {
        wmo_header[nchar] = 0;
        *nscanned = cp + 1 - (const char*)buf;
        status = 0;
    }
    return status;
}


/******************************************************************************
 * GINI Product-Definition Block:
 ******************************************************************************/

/**
 * Product-definition block (not be be confused with the NBS transport layer's
 * product-definition header):
 */
typedef struct {
    uint_fast32_t source;
    uint_fast32_t creating_entity;
    uint_fast32_t sector_id;
    uint_fast32_t physical_element;
    uint_fast32_t num_logical_recs; ///< Number of scan lines
    uint_fast32_t logical_rec_size; ///< Number of bytes per scan line
    uint_fast32_t year;
    uint_fast32_t month;
    uint_fast32_t day;
    uint_fast32_t hour;
    uint_fast32_t minute;
    uint_fast32_t second;
    uint_fast32_t centisecond;
    uint_fast32_t map_proj;         ///< Map projection:
                                    ///<   1 Mercator
                                    ///<   3 Lambert Conformal
                                    ///<   5 Polar Stereographic
                                    ///<   51-254 Reserved
    uint_fast32_t nx;               ///< Number of pixels per scan line
    uint_fast32_t ny;               ///< Number of scan lines (i.e., records)
    uint_fast32_t image_res;
    uint_fast32_t is_compressed;
    uint_fast32_t version;          ///< Creating entity's PDB version
    uint_fast32_t length;           ///< Length of serialized PDB in bytes
} pdb_t;

/**
 * @retval    GINI_STATUS_INVAL  Invalid header
 */
gini_status_t pdb_decode(
        pdb_t* const restrict         pdb,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int status;
    /*
     * Always 512 bytes according to
     * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf> -- but
     * only the first 46 are decoded.
     */
    if (nbytes < 46) {
        log_add("Product-definition block shorter than 46 bytes: %u",
                nbytes);
        status = GINI_STATUS_INVAL;
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
        pdb->map_proj = buf[15];
        pdb->nx = decode_uint16(buf+16);
        pdb->ny = decode_uint16(buf+18);
        pdb->image_res = buf[41];
        pdb->is_compressed = buf[42];
        pdb->version = buf[43];
        pdb->length = decode_uint16(buf+44);
        *nscanned = pdb->length;
        status = 0;
    }
    return status;
}

/**
 * Returns the length of a serialized product-definition header in bytes.
 *
 * @param[in] pdb  Product-definition header
 */
static inline unsigned pdb_get_length(
        const pdb_t* const pdb)
{
    return pdb->length;
}

static inline unsigned pdb_get_rec_len(
        const pdb_t* const pdb)
{
    return pdb->logical_rec_size;
}

static inline unsigned pdb_get_num_logical_recs(
        const pdb_t* const pdb)
{
    return pdb->num_logical_recs;
}

static inline uint8_t pdb_get_creating_entity(
        const pdb_t* const pdb)
{
    return pdb->creating_entity;
}

static inline uint8_t pdb_get_sector(
        const pdb_t* const pdb)
{
    return pdb->sector_id;
}

static inline uint8_t pdb_get_physical_element(
        const pdb_t* const pdb)
{
    return pdb->physical_element;
}

static inline int pdb_get_year(
        const pdb_t* const pdb)
{
    return pdb->year;
}

static inline int pdb_get_month(
        const pdb_t* const pdb)
{
    return pdb->month;
}

static inline int pdb_get_day(
        const pdb_t* const pdb)
{
    return pdb->day;
}

static inline int pdb_get_hour(
        const pdb_t* const pdb)
{
    return pdb->hour;
}

static inline int pdb_get_minute(
        const pdb_t* const pdb)
{
    return pdb->minute;
}

static inline uint8_t pdb_get_image_resolution(
        const pdb_t* const pdb)
{
    return pdb->image_res;
}

/******************************************************************************
 * GINI Headers: WMO Header and (possibly-compressed) WMO Header and
 * Product-Definition Block:
 ******************************************************************************/

/**
 * De-serializes an uncompressed GINI product-definition block.
 *
 * @param[out] wmo_header        WMO header
 * @param[out] pdb               Product-definition block
 * @param[in]  buf               Uncompressed and serialized product-definition
 *                               block
 * @param[in]  nbytes            Number of bytes in `buf`
 * @param[in]  nscanned          Number of bytes scanned in `buf`
 * @retval 0                     Success. `*wmo_header`, `*pdb`, and `*nscanned`
 *                               are set.
 * @retval    GINI_STATUS_INVAL  Invalid header. log_add() called.
 */
static gini_status_t gini_headers_decode(
        char* const restrict     wmo_header,
        pdb_t* const restrict    pdb,
        const uint8_t* restrict  buf,
        unsigned                 nbytes,
        unsigned* const restrict nscanned)
{
    unsigned n;
    int      status = wmoheader_deserialize(wmo_header, buf, nbytes, &n);
    if (status) {
        log_add("Couldn't decode WMO header");
    }
    else {
        *nscanned = n;
        buf += n;
        nbytes -= n;
        status = pdb_decode(pdb, buf, nbytes, &n);
        if (status) {
            log_add("Couldn't de-serialize product-definition block");
        }
        else {
            *nscanned += n;
        }
    }
    return status;
}

/******************************************************************************
 * GINI End-of-Product Block:
 ******************************************************************************/

/**
 * A GINI end-of-product block.
 */
typedef struct {
    unsigned nbytes;     ///< Number of _uncompressed_ bytes
    unsigned data_len;   ///< Number of bytes in `data`
    bool     compressed; ///< Is `data` zlib(3)-compressed
    uint8_t  data[1];    ///< Data buffer
} eopb_t;

/**
 * Sets the pixel data in an uncompressed GINI end-of-product block.
 *
 * @param[out] data    GINI end-of-product record to be set
 * @param[in]  nbytes  Number of bytes in `data`
 */
static void eopb_set(
        uint8_t* const data,
        const unsigned nbytes)
{
    for (unsigned i = 0; i < nbytes; ) {
        data[i++] = 255;
        data[i++] = 0;
    }
}

/**
 * Initializes a GINI end-of-product block. Does _not_ set the pixel data.
 *
 * @param[out] block          End-of-product block
 * @param[in]  nbytes         Number of uncompressed bytes in `block`
 * @param[in]  is_compressed  Is the data zlib(3)-compressed?
 */
static inline void eopb_init(
        eopb_t* const   block,
        const unsigned  nbytes,
        const bool      is_compressed)
{
    block->nbytes = nbytes;
    block->compressed = is_compressed;
}

/**
 * Returns a new GINI end-of-product block.
 *
 * @param[out] block          GINI end-of-product block
 * @param[in]  nbytes         Number of uncompressed bytes in `block`
 * @param[in]  is_compressed  Should the data be zlib(3)-compressed?
 * @retval 0                  Success. `*block` is set.
 * @retval GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
static nbs_status_t eopb_new(
        eopb_t* restrict* const block,
        const unsigned          nbytes,
        const bool              is_compressed)
{
    int     status;
    eopb_t* blk = log_malloc(sizeof(eopb_t)+nbytes-1, "end-of-product block");
    if (blk == NULL) {
        status = GINI_STATUS_NOMEM;
    }
    else {
        blk->nbytes = nbytes;
        blk->compressed = is_compressed;
        if (!is_compressed) {
            eopb_set(blk->data, nbytes);
            blk->data_len = nbytes;
            status = 0;
        }
        else {
            uint8_t buf[nbytes];
            eopb_set(buf, nbytes);
            status = gini_pack(buf, nbytes, blk->data, nbytes, &blk->data_len);
            if (status)
                log_add("Couldn't compress %u-byte end-of-product block",
                        nbytes);
        }
        if (status) {
            free(blk);
        }
        else {
            *block = blk;
        }
    } // `blk` allocated
    return status;
}

/**
 * Frees a GINI end-of-product block.
 *
 * @param[in,out] block  End-of-product block
 */
static inline void eopb_free(
        eopb_t* const block)
{
    free(block);
}

/**
 * Compares two GINI end-of-product blocks.
 *
 * @param[in] o1  First end-of-product block
 * @param[in] o2  Second end-of-product block
 * @returns       A value less than, equal to, or greater than zero as the first
 *                block is considered less than, equal to, or greater than the
 *                second block
 */
static int eopb_compare(
        const void* o1,
        const void* o2)
{
    const eopb_t* block1 = (eopb_t*)o1;
    const eopb_t* block2 = (eopb_t*)o2;
    return (block1->nbytes < block2->nbytes)
            ? -1
            : (block1->nbytes > block2->nbytes)
              ? 1
              : (block1->compressed < block2->compressed)
                ? -1
                : (block1->compressed > block2->compressed)
                  ? 1
                  : 0;
}

/******************************************************************************
 * Terminator:
 *
 * Manages end-of-product blocks for GINI images.
 ******************************************************************************/

/**
 * The database of end-of-product blocks.
 */
static struct {
    void* root;
} eopb_db;

/**
 * Initializes the GINI terminator.
 */
static void giniTerm_init(void)
{
    eopb_db.root = NULL; // Unnecessary but symmetric with giniTerm_fini()
}

/**
 * Finalizes the GINI terminator.
 */
static void giniTerm_fini(void)
{
    while (eopb_db.root) {
        eopb_t* block = *(eopb_t**)eopb_db.root;
        (void)tdelete(block, &eopb_db.root, eopb_compare);
        eopb_free(block);
    }
}

/**
 * Returns a GINI end-of-product block.
 *
 * @param[in]  nbytes         Number of uncompressed bytes in block
 * @param[in]  is_compressed  Should data be zlib(3)-compressed?
 * @param[out] data           Block data
 * @param[out] data_len       Number of bytes in `data`
 * @retval 0                  Success. `*data` and `*data_len` are set.
 * @retval GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
static nbs_status_t giniTerm_get(
        const unsigned                    nbytes,
        const bool                        is_compressed,
        uint8_t* restrict* const restrict data,
        unsigned* const restrict          data_len)
{
    int      status;
    eopb_t   key;  // NB: Transient and incomplete (doesn't have pixel data)
    eopb_init(&key, nbytes, is_compressed);
    eopb_t** node = tsearch(&key, &eopb_db.root, eopb_compare);
    if (node == NULL) {
        log_add("Couldn't add end-of-product block to database");
        status = GINI_STATUS_NOMEM;
    }
    else {
        status = 0;
        if (&key == *node) {
            /*
             * The node was created. Replace its transient and incomplete value
             * with a persistent and complete one that compares equal.
             */
            eopb_t* block;
            status = eopb_new(&block, nbytes, is_compressed);
            if (status == 0)
                *node = block;
        }
        if (status == 0) {
            *data = (*node)->data;
            *data_len = (*node)->data_len;
        }
    }
    return status;
}

/******************************************************************************
 * GINI Object:
 ******************************************************************************/

struct gini {
    dynabuf_t*   dynabuf;          ///< Dynamic buffer for accumulating data
    wmo_header_t wmo_header;       ///< WMO header: T1T2A1A2ii CCCC YYGGgg [BBB]
    filler_t     filler;           ///< Filler of missing records
    pdb_t        pdb;              ///< Product-definition block
    unsigned     wmo_header_len;   ///< Number of bytes in serialized WMO header
    unsigned     bytes_per_rec;    ///< Number of bytes in a record
    unsigned     recs_per_block;   ///< Number of records (i.e., scan-lines) in
                                   ///< a canonical data-block
    unsigned     num_blocks_actual;///< Number of received blocks
    unsigned     num_blocks_expected;///< Number of expected blocks in product.
                                   ///< Includes product-definition block and
                                   ///< end-of-product block.
    unsigned     num_recs_actual;  ///< Number of logical records (i.e.,
                                   ///< scan-lines) received or to send.
                                   ///< Includes end-of-product record.
    int          prod_type;        ///< Product transfer type
    bool         started;          ///< Has instance been initialized?
    bool         is_compressed;    ///< Is the data zlib(3)-compressed?
    bool         have_filler;      ///< Has `filler` been initialized?
};

#define BYTES_PER_REC(gini)    pdb_get_rec_len(&(gini)->pdb)
#define NUM_LOGICAL_RECS(gini) pdb_get_num_logical_recs(&(gini)->pdb)

static unsigned terminator_count = 0;

/**
 * Initializes GINI headers from a (possibly compressed) serialized form.
 *
 * @param[in,out] gini           GINI image
 * @param[in]     buf            Possibly compressed, serialized GINI headers
 * @param[in]     nbytes         Number of bytes in `buf`
 * @param[in]     is_compressed  Is `buf` compressed?
 * @param[out]    nscanned       Number of bytes scanned in `buf`
 * @retval 0                     Success. `gini->wmo_header`, `gini->pdb`,
 *                               `gini->is_compressed`, and `*nscanned` are set.
 * @retval GINI_STATUS_INVAL     Invalid header. log_add() called.
 * @retval GINI_STATUS_SYSTEM    System failure. log_add() called.
 */
static gini_status_t gini_headers_init_try(
        gini_t* const restrict   gini,
        const uint8_t* restrict  buf,
        unsigned                 nbytes,
        const bool               is_compressed,
        unsigned* const restrict nscanned)
{
    uint8_t  uncomp_headers[NBS_MAX_FRAME_SIZE];
    unsigned num_uncomp_bytes;
    unsigned num_comp_bytes_scanned;
    unsigned num_uncomp_bytes_scanned;
    int      status = 0;
    if (is_compressed)
        status = gini_unpack(buf, nbytes, uncomp_headers,
                sizeof(uncomp_headers), &num_uncomp_bytes,
                &num_comp_bytes_scanned);
    if (status == 0) {
        status = is_compressed
                ? gini_headers_decode(gini->wmo_header, &gini->pdb,
                    uncomp_headers, num_uncomp_bytes, &num_uncomp_bytes_scanned)
                : gini_headers_decode(gini->wmo_header, &gini->pdb, buf, nbytes,
                    &num_uncomp_bytes_scanned);
    }
    if (status) {
        log_add("Couldn't de-serialize GINI headers");
    }
    else {
        *nscanned = is_compressed
                ? num_comp_bytes_scanned
                : num_uncomp_bytes_scanned;
        gini->is_compressed = is_compressed;
    }
    return status;
}

/**
 * Initializes GINI headers from a (possibly compressed) serialized form.
 *
 * @param[in,out] gini         GINI image.
 * @param[in]     buf          Possibly compressed, serialized GINI headers
 * @param[in]     nbytes       Number of bytes in `buf`
 * @param[out]    nscanned     Number of bytes scanned in `buf`
 * @retval 0                   Success. `gini->wmo_header`, `gini->pdb`,
 *                             `gini->is_compressed`, and `*nscanned` are set.
 * @retval GINI_STATUS_INVAL   Invalid header. log_add() called.
 * @retval GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
static gini_status_t gini_headers_deserialize(
        gini_t* const restrict        gini,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int status = gini_headers_init_try(gini, buf, nbytes, true, nscanned);
    if (status) {
        log_clear();
        status = gini_headers_init_try(gini, buf, nbytes, false, nscanned);
    }
    if (status == 0) {
        unsigned creating_entity = pdb_get_creating_entity(&gini->pdb);
        switch (creating_entity) {
            case 18: // GOES-15
                gini->prod_type = PROD_TYPE_GOES_WEST;
                break;
            case 17: // GOES-14
            case 16: // GOES-13
                gini->prod_type = PROD_TYPE_GOES_EAST;
                break;
            default:
                log_add("Unsupported creating entity: %u", creating_entity);
                status = GINI_STATUS_INVAL;
        }
    }
    return status;
}

/**
 * Scans the serialized and possibly compressed data-blocks of a GINI image and
 * computes parameters related to block size and the number of blocks.
 *
 * @param[in,out] gini        GINI product
 * @param[in]     buf         Start of (possibly compressed) serialized
 *                            data-blocks
 * @param[in]     nbytes      Number of bytes in `buf`
 * @retval 0                  Success. `gini->recs_per_block`,
 *                            `gini->num_blocks_expected`, and
 *                            `gini->num_blocks_actual` are set.
 * @retval GINI_STATUS_INVAL  Invalid serialization
 */
static gini_status_t gini_scan_data_blocks(
        gini_t* const restrict        gini,
        const uint8_t* const restrict buf,
        const size_t                  nbytes)
{
    int            status;
    const unsigned num_logical_recs = NUM_LOGICAL_RECS(gini);
    const unsigned num_bytes_per_rec = pdb_get_rec_len(&gini->pdb);
    if (!gini->is_compressed) {
        const unsigned num_recs_from_size = nbytes / num_bytes_per_rec;
        if (num_recs_from_size < num_logical_recs ||
                num_recs_from_size != num_logical_recs + 1) {
            log_add("Product doesn't contain specified number of records: "
                    "num_recs_from_size=%u, num_logical_recs=%u",
                    num_recs_from_size, num_logical_recs);
            status = GINI_STATUS_INVAL;
        }
        else {
            const size_t   num_bytes_data =
                    (size_t)num_logical_recs * num_bytes_per_rec;
            /*
             * 5120 is the documented maximum number of bytes in the payload of
             * an NBS frame for a GINI data-block. See
             * <http://www.nws.noaa.gov/noaaport/html/GOES%20and%20Non%20Goes%20Compression.pdf>.
             */
            const unsigned num_data_blocks = (num_bytes_data + 5120 - 1) / 5120;
            gini->recs_per_block = num_bytes_data / num_data_blocks;
            gini->num_blocks_expected = num_data_blocks + 1;
            gini->num_blocks_actual = (num_recs_from_size == num_logical_recs)
                    ? num_data_blocks      // Doesn't have end-of-product block
                    : num_data_blocks + 1; // Has end-of-product block
            status = 0;
        }
    }
    else {
        const uint8_t* bp = buf;
        size_t         left = nbytes;
        unsigned       num_blocks;
        unsigned       num_uncomp_bytes_first_block = 0;

        for (num_blocks = 0; left; num_blocks++) {
            uint8_t uncomp_bytes[NBS_MAX_FRAME_SIZE];
            unsigned num_uncomp_bytes, num_comp_bytes_scanned;
            status = gini_unpack(bp, left, uncomp_bytes, sizeof(uncomp_bytes),
                    &num_uncomp_bytes, &num_comp_bytes_scanned);
            if (status) {
                log_warning_q("Couldn't uncompress data block %u", num_blocks);
                break;
            }
            if (num_blocks == 0)
                num_uncomp_bytes_first_block = num_uncomp_bytes;
            bp += num_comp_bytes_scanned;
            left -= num_comp_bytes_scanned;
        }
        gini->recs_per_block = num_uncomp_bytes_first_block / num_bytes_per_rec;
        gini->num_blocks_expected =
                (num_logical_recs + gini->recs_per_block - 1) /
                gini->recs_per_block + 1; // Plus end-of-product block
        gini->num_blocks_actual = num_blocks;
        if ((gini->num_blocks_actual < gini->num_blocks_expected - 1) &&
                (gini->num_blocks_actual != gini->num_blocks_expected)) {
            log_add("Product doesn't contain specified number of blocks: "
                    "num_blocks_actual=%u, num_blocks_expected=%u",
                    gini->num_blocks_actual, gini->num_blocks_expected);
            status = GINI_STATUS_INVAL;
        }
        else {
            status = 0;
        }
    }
    return status;
}

/**
 * Appends a block of data (i.e., scan-lines) to a GINI image. Compresses or
 * uncompresses the data as necessary.
 *
 * @param[in,out] gini           GINI image
 * @param[in]     block          Block of data to be appended. `nbytes` member
 *                               isn't used.
 * @retval GINI_STATUS_LOGIC     Block would exceed GINI product. log_add()
 *                               called.
 * @retval GINI_STATUS_NOMEM     Out of memory. log_add() called.
 */
static gini_status_t gini_append_block(
        gini_t* const restrict        gini,
        const block_t* const restrict block)
{
    int status;
    if (gini->num_blocks_actual == gini->num_blocks_expected) {
        log_add("Can't append beyond GINI product: num_blocks=%u",
                gini->num_blocks_expected);
        status = GINI_STATUS_LOGIC;
    }
    else if (gini->is_compressed == block->is_compressed) {
        status = dynabuf_add(gini->dynabuf, block->data, block->data_len);
        if (status) {
            log_add("Couldn't copy data-block to product-buffer");
            status = GINI_STATUS_NOMEM;
        }
    }
    else {
        const unsigned reserve = NBS_MAX_FRAME_SIZE;
        dynabuf_t*     dynabuf = gini->dynabuf;
        status = dynabuf_reserve(dynabuf, reserve);
        if (status) {
            log_add("Couldn't reserve space in dynamic-buffer");
            status = GINI_STATUS_NOMEM;
        }
        else {
            unsigned out_nbytes;
            unsigned nscanned;
            uint8_t* buf = dynabuf_get_buf(dynabuf);
            size_t   used = dynabuf_get_used(dynabuf);
            status = block->is_compressed
                    ? gini_unpack(block->data, block->data_len, buf, reserve,
                            &out_nbytes, &nscanned)
                    : gini_pack(block->data, block->data_len, buf, reserve,
                            &out_nbytes);
            if (status) {
                log_add("Couldn't %scompress %u-byte data-block into "
                        "product-buffer", block->data_len,
                        block->is_compressed ? "un" : "");
            }
            else {
                dynabuf_set_used(dynabuf, used + out_nbytes);
            }
        }
    }
    if (status == 0) {
        gini->num_blocks_actual++;
        unsigned num_recs;
        if (gini->num_blocks_actual == gini->num_blocks_expected) {
            num_recs = 1; // End-of-product record
        }
        else if (!block->is_compressed) {
            num_recs = block->data_len / BYTES_PER_REC(gini);
        }
        else {
            // Block is compressed and not the last block
            unsigned num_recs_left = NUM_LOGICAL_RECS(gini) -
                    gini->num_recs_actual;
            num_recs = GINI_MIN(gini->recs_per_block, num_recs_left);
        }
        gini->num_recs_actual += num_recs;
    }
    return status;
}

/**
 * Ensures that a GINI product contains an end-of-product block.
 *
 * @param[in,out] gini        GINI product
 * @retval 0                  Success
 * @retval GINI_STATUS_INVAL  `gini == NULL`. log_add() called.
 * @retval GINI_STATUS_LOGIC  `gini->num_blks_recvd + 1 < gini->num_blocks`.
 *                            log_add() called.
 */
static gini_status_t gini_ensure_eop_block(
        gini_t* const gini)
{
    int status;
    if (gini == NULL) {
        log_add("NULL argument: gini=%p");
        status = GINI_STATUS_INVAL;
    }
    else if (gini->num_blocks_actual < gini->num_blocks_expected - 1) {
        log_add("GINI product is missing data-blocks: num_blks_actual=%u, "
                "num_blocks=%u", gini->num_blocks_actual,
                gini->num_blocks_expected);
        status = GINI_STATUS_LOGIC;
    }
    else if (gini->num_blocks_actual == gini->num_blocks_expected) {
        status = 0; // Already has EOP block
    }
    else {
        uint8_t* data;
        unsigned data_len;
        status = giniTerm_get(BYTES_PER_REC(gini), gini->is_compressed, &data,
                &data_len);
        if (status) {
            log_add("Couldn't get end-of-product block");
        }
        else {
            block_t  block;
            block_init(&block, data, data_len, data_len, gini->is_compressed);
            status = gini_append_block(gini, &block);
            if (status)
                log_add("Couldn't append end-of-product block");
        }
    }
    return status;
}

/**
 * Initializes a GINI product from a serialized instance.
 *
 * @pre                           `!gini->started`
 * @param[in,out] gini            GINI image to be initialized
 * @param[in]     buf             Buffer from which the serialized instance
 *                                shall be read. Caller may free.
 * @param[in]     nbytes          Number of bytes in `buf`
 * @retval 0                      Success. `*gini` is intialized.
 * @retval GINI_STATUS_LOGIC      `*gini` has already been started. log_add()
 *                                called.
 * @retval GINI_STATUS_SYSTEM     System failure. log_add() called.
 * @retval GINI_STATUS_NOMEM      Out of memory. log_add() called.
 * @retval GINI_STATUS_INVAL      Serialized GINI image is invalid. log_add()
 *                                called.
 */
static gini_status_t gini_deserialize_unstarted(
        gini_t* const restrict        gini,
        const uint8_t* const restrict buf,
        const size_t                  nbytes)
{
    const uint8_t* starting_byte = buf;
    size_t         num_bytes_left = nbytes;
    unsigned       num_bytes_scanned;
    int            status = wmoheader_deserialize(gini->wmo_header,
            starting_byte, num_bytes_left, &num_bytes_scanned);
    if (status) {
        log_add("Couldn't de-serialize clear-text WMO header");
    }
    else {
        starting_byte += num_bytes_scanned;
        num_bytes_left -= num_bytes_scanned;
        gini->wmo_header_len = num_bytes_scanned;
        status = gini_headers_deserialize(gini, starting_byte, num_bytes_left,
                &num_bytes_scanned);
        if (status) {
            log_add("Couldn't de-serialized GINI headers");
        }
        else {
            starting_byte += num_bytes_scanned;
            num_bytes_left -= num_bytes_scanned;
            status = gini_scan_data_blocks(gini, starting_byte, num_bytes_left);
            if (status) {
                log_add("Couldn't scan data blocks");
            }
            else {
                dynabuf_clear(gini->dynabuf);
                status = dynabuf_add(gini->dynabuf, buf, nbytes);
                if (status) {
                    log_add("Couldn't copy GINI product to dynamic buffer");
                    status = GINI_STATUS_SYSTEM;
                }
                else {
                    status = gini_ensure_eop_block(gini);
                    if (status) {
                        log_add("Couldn't add end-of-product block");
                    }
                    else {
                        gini->num_recs_actual = NUM_LOGICAL_RECS(gini) + 1;
                    }
                }
            }
        }
    }
    return status;
}

/**
 * Adds missing records (i.e., scan lines) to a GINI image if appropriate. The
 * records are grouped into blocks and compressed if appropriate.
 *
 * @param[in] gini               GINI product
 * @param[in] nrecs              Number of records to add
 * @retval GINI_STATUS_LOGIC     Added records would exceed GINI product.
 *                               log_add() called.
 * @retval GINI_STATUS_NOMEM     Out of memory. log_add() called.
 * @retval GINI_STATUS_SYSTEM    System failure. log_add() called.
 */
static gini_status_t gini_add_missing_records(
        gini_t* const  gini,
        unsigned       nrecs)
{
    log_assert(gini != NULL);
    log_assert(gini->started);
    int            status = 0;
    unsigned       iblock = gini->num_blocks_actual;
    unsigned       gap_nrecs;
    for (; nrecs; nrecs -= gap_nrecs) {
        gap_nrecs = GINI_MIN(gini->recs_per_block, nrecs);
        gap_t gap;
        status = filler_get_gap(&gini->filler, gap_nrecs, &gap);
        if (status) {
            log_add("Couldn't get missing-block gap of %u records", gap_nrecs);
        }
        else {
            status = gini_append_block(gini, &gap);
            if (status) {
                log_add("Couldn't append missing block %u with %u bytes to "
                        "dynamic-buffer", iblock, gap.data_len);
                break;
            }
        }
        iblock++;
        gini->num_recs_actual += gap_nrecs;
    }
    return status;
}

/**
 * Scans a GINI image for the next block to return, which must exist.
 *
 * @param[in,out] iter         GINI iterator
 * @param[out] nbytes_uncomp   Size of uncompressed block in bytes
 * @param[out] nbytes_scanned  Number of input bytes scanned
 * @retval 0                   Success. `*nbytes_uncomp` and `nbytes_scanned`
 *                             are set.
 * @retval GINI_STATUS_SYSTEM  System failure. log_add() called.
 * @retval GINI_STATUS_INVAL   GINI image is invalid. log_add() called.
 */
static gini_status_t iter_scan(
        gini_iter_t* const iter,
        unsigned* const restrict nbytes_uncomp,
        unsigned* const restrict nbytes_scanned)
{
    int            status;
    const uint8_t* start = iter->block + iter->comp_offset;
    if (start >= iter->out) {
        log_add("Can't scan beyond product");
        status = GINI_STATUS_INVAL;
    }
    else if (!iter->is_compressed) {
        *nbytes_uncomp = (iter->iblk == 0)
                ? iter->comp_offset + iter->pdb_length
                : GINI_MIN(iter->bytes_per_block, iter->out - start);
        *nbytes_scanned = *nbytes_uncomp;
        status = 0;
    }
    else {
        uint8_t        buf[NBS_MAX_FRAME_SIZE]; // Should suffice
        status = gini_unpack(start, iter->out - start, buf, sizeof(buf),
                nbytes_uncomp, nbytes_scanned);
        if (status) {
            log_add("Couldn't uncompress block %u", iter->iblk);
        }
        else {
            *nbytes_scanned += iter->comp_offset;
            if (iter->iblk && *nbytes_uncomp > iter->bytes_per_block) {
                log_add("Uncompressed data-block %u has too many bytes: "
                        "actual=%u, expected=%u", iter->iblk,
                        *nbytes_uncomp, iter->bytes_per_block);
                status = GINI_STATUS_INVAL;
            }
        }
    }
    return status;
}

/**
 * Returns the next block of a GINI image.
 *
 * @pre                       `iter->iblk < iter->num_blocks`
 * @param[in,out] iter        Iterator
 * @param[out]    block       Start of data-block (might be compressed)
 * @param[out]    nbytes      Number of bytes in `block`
 * @retval 0                  Success
 * @retval GINI_STATUS_INVAL  GINI image is invalid. log_add() called.
 * @retval GINI_STATUS_SYSTEM System error. log_add() called.
 */
static gini_status_t iter_next(
        gini_iter_t* const                      iter,
        const uint8_t* restrict* const restrict block,
        unsigned* const restrict                nbytes)
{
    unsigned nbytes_scanned;
    unsigned nbytes_uncomp;
    int      status = iter_scan(iter, &nbytes_uncomp, &nbytes_scanned);
    if (status == 0) {
        unsigned recs_returned;
        if (iter->iblk == 0) {
            recs_returned = 0;
            iter->comp_offset = 0; // For next time
        }
        else {
            unsigned num_recs = (nbytes_uncomp + iter->bytes_per_rec - 1) /
                    iter->bytes_per_rec;
            recs_returned = iter->num_recs_returned + num_recs;
            if (recs_returned > iter->num_recs) {
                log_add("GINI image has too many records: actual=%u, "
                        "expected=%u", recs_returned, iter->num_recs);
                status = GINI_STATUS_INVAL;
            }
        }
        if (status == 0) {
            *block = iter->block;
            *nbytes = nbytes_scanned;
            iter->num_recs_returned = recs_returned;
            iter->block += nbytes_scanned;
            iter->iblk++;
        }
    }
    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new GINI product.
 *
 * @param[out] gini           Returned GINI product.
 * @param[in]  dynabuf        Dynamic buffer for accumulating data
 * @retval 0                  Success. `*gini` is set.
 * @retval GINI_STATUS_INVAL  `gini == NULL || dynabuf == NULL`. log_add()
 *                            called.
 * @retval GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
gini_status_t gini_new(
        gini_t** const restrict   gini,
        dynabuf_t* const restrict dynabuf)
{
    int status;
    if (gini == NULL || dynabuf == NULL) {
        log_add("NULL argument: gini=%p, dynabuf=%p", gini, dynabuf);
        status = GINI_STATUS_INVAL;
    }
    else {
        gini_t* obj = log_malloc(sizeof(gini_t), "GINI product");
        if (obj == NULL) {
            status = GINI_STATUS_NOMEM;
        }
        else {
            obj->dynabuf = dynabuf;
            obj->started = false;
            obj->have_filler = false;
            *gini = obj;
            if (terminator_count++ == 0)
                giniTerm_init();
            status = 0;
        }
    }
    return status;
}

/**
 * Frees a GINI product.
 *
 * @param[in] gini  GINI product to be freed or `NULL`.
 */
void gini_free(
        gini_t* const gini)
{
    if (gini) {
        if (--terminator_count == 0)
            giniTerm_fini();
        if (gini->have_filler)
            filler_fini(&gini->filler);
        free(gini);
    }
}

/**
 * Starts a GINI product.
 *
 * @param[in,out] gini           GINI image to be initialized
 * @param[in]     buf            Serialized product-definition block of GINI
 *                               product
 * @param[in]     nbytes         Number of bytes in `buf`
 * @param[in]     rec_len        Number of bytes in a record (i.e., scan line)
 * @param[in]     recs_per_block Canonical number of records in a block
 * @param[in]     is_compressed  Is `buf` zlib(3) compressed?
 * @param[in]     prod_type      NBS transport-layer product-specific header
 *                               product-type
 * @retval 0                     Success. `*gini` is intialized.
 * @retval GINI_STATUS_INVAL     `recs_per_block == 0` or serialized GINI
 *                               headers are invalid. log_add() called.
 * @retval GINI_STATUS_LOGIC     Logic error. log_add() called.
 * @retval GINI_STATUS_SYSTEM    System failure. log_add() called.
 * @retval GINI_STATUS_NOMEM     Out of memory. log_add() called.
 */
gini_status_t gini_start(
        gini_t* const restrict        gini,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                rec_len,
        const unsigned                recs_per_block,
        const bool                    is_compressed,
        const int                     prod_type)
{
    log_assert(gini);
    int status;
    if (gini->started) {
        log_add("GINI product already started");
        status = GINI_STATUS_LOGIC;
    }
    else {
        const uint8_t* bp = buf;
        unsigned       left = nbytes;
        unsigned       nscanned;
        status = wmoheader_deserialize(gini->wmo_header, bp, left, &nscanned);
        if (status) {
            log_add("Couldn't decode clear-text WMO header");
        }
        else {
            bp += nscanned;
            left -= nscanned;
            gini->wmo_header_len = nscanned;
            status = gini_headers_init_try(gini, bp, left, is_compressed,
                    &nscanned);
            if (status) {
                log_add("Couldn't initialize GINI headers");
            }
            else {
                status = 0;
                if (!gini->have_filler) {
                    status = filler_init(&gini->filler, rec_len, recs_per_block,
                            is_compressed);
                    if (status) {
                        log_add("Couldn't initialize gap-filler");
                    }
                }
                if (status == 0) {
                    dynabuf_clear(gini->dynabuf);
                    status = dynabuf_add(gini->dynabuf, buf, nbytes);
                    if (status) {
                        log_add("Couldn't add GINI headers to dynamic "
                                "buffer");
                    }
                    else {
                        /*
                         * Product-definition block + data-blocks +
                         * end-of-product block:
                         */
                        gini->num_blocks_expected = 1 +
                                (NUM_LOGICAL_RECS(gini) + recs_per_block - 1) /
                                recs_per_block + 1;
                        gini->num_blocks_actual = 1; // Product-definition block
                        gini->num_recs_actual = 0;  // No scan-lines yet
                        gini->bytes_per_rec = pdb_get_rec_len(&gini->pdb);
                        gini->recs_per_block = recs_per_block;
                        gini->prod_type = prod_type;
                        gini->started = true;
                    }
                }
            }
        }
    }
    return status;
}

/**
 * Initializes a GINI product from a serialized instance.
 *
 * @param[in,out] gini            GINI image to be initialized
 * @param[in]     buf             Buffer from which the serialized instance
 *                                shall be read. Caller may free.
 * @param[in]     nbytes          Number of bytes in `buf`
 * @retval 0                      Success. `*gini` is intialized.
 * @retval GINI_STATUS_LOGIC      `*gini` has already been started. log_add()
 *                                called.
 * @retval GINI_STATUS_SYSTEM     System failure. log_add() called.
 * @retval GINI_STATUS_NOMEM      Out of memory. log_add() called.
 * @retval GINI_STATUS_INVAL      Serialized GINI image is invalid. log_add()
 *                                called.
 */
gini_status_t gini_deserialize(
        gini_t* const restrict        gini,
        const uint8_t* const restrict buf,
        const size_t                  nbytes)
{
    log_assert(gini);
    int status;
    if (gini->started) {
        log_add("GINI product already started");
        status = GINI_STATUS_LOGIC;
    }
    else {
        status = gini_deserialize_unstarted(gini, buf, nbytes);
    }
    return status;
}

/**
 * Adds a data-block (not the product-definition block) to a GINI product. The
 * block is compressed or uncompressed as necessary to match previous blocks.
 * Any gap to the previous block is filled.
 *
 * @param[in,out] gini           GINI presentation-layer GINI product
 * @param[in]     block_index    Index of data-block (first data-block after
 *                               product-definition block is 1)
 * @param[in]     data           Data-block (i.e., scan-lines)
 * @param[in]     nbytes         Number of bytes in `data`
 * @param[in]     is_compressed  Is `data` zlib(3)-compressed?
 * @retval GINI_STATUS_INVAL     `gini == NULL` or `block_index` is invalid.
 *                               log_add() called.
 * @retval GINI_STATUS_LOGIC     `gini_start(gini)` not called. log_add()
 *                               called.
 * @retval GINI_STATUS_LOGIC     Added block would exceed GINI product.
 *                               log_add() called.
 * @retval GINI_STATUS_NOMEM     Out of memory. log_add() called.
 * @retval GINI_STATUS_SYSTEM    System failure. log_add() called.
 */
gini_status_t gini_add_block(
        gini_t* const restrict        gini,
        const unsigned                block_index,
        const uint8_t* const restrict data,
        const unsigned                nbytes,
        const bool                    is_compressed)
{
    int status;
    if (gini == NULL) {
        log_add("NULL argument: gini=%p", gini);
        status = GINI_STATUS_INVAL;
    }
    else if (!gini->started) {
        log_add("GINI product not started");
        status = GINI_STATUS_LOGIC;
    }
    else if (block_index >= gini->num_blocks_expected) {
        log_add("Invalid argument: block_index=%u, num_blocks=%u", block_index,
                gini->num_blocks_expected);
        status = GINI_STATUS_INVAL;
    }
    else {
        int num_blks_missed = block_index - gini->num_blocks_actual;
        if (num_blks_missed < 0) {
            log_add("Data-block %u already added", block_index);
            status = GINI_STATUS_INVAL;
        }
        else {
            unsigned num_recs_missed =
                    (block_index + 1 == gini->num_blocks_expected)
                        ? NUM_LOGICAL_RECS(gini) - gini->num_recs_actual
                        : num_blks_missed * gini->recs_per_block;
            status = gini_add_missing_records(gini, num_recs_missed);
            if (status) {
                log_add("Couldn't add missing records");
            }
            else {
                block_t block;
                block.data = (uint8_t*)data; // Safe cast
                block.nbytes = nbytes; // Valid only if `!is_compressed`
                block.data_len = nbytes;
                block.is_compressed = is_compressed;
                status = gini_append_block(gini, &block);
                if (status)
                    log_add("Couldn't append block %u", block_index);
            }
        }
    }
    return status;
}

/**
 * Finishes a GINI product. Pads the end of the image with blank scan-lines and
 * adds an end-of-product block if necessary. After this, gini_start() will have
 * to be called on the product before gini_add_block() or gini_finish() can be
 * called on it.
 *
 * @param[in,out] gini         GINI product to be finished
 * @retval 0                   Success. Image was padded with blank data-blocks
 *                             if necessary.
 * @retval GINI_STATUS_INVAL   `gini == NULL`. log_add() called.
 * @retval GINI_STATUS_LOGIC   `gini_start(gini)` not called. log_add() called.
 * @retval GINI_STATUS_LOGIC   Logic error. log_add() called.
 * @retval GINI_STATUS_NOMEM   Out of memory. log_add() called.
 * @retval GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
gini_status_t gini_finish(
        gini_t* const gini)
{
    int status;
    if (gini == NULL) {
        log_add("NULL argument; gini=%p", gini);
        status = GINI_STATUS_INVAL;
    }
    else if (!gini->started) {
        log_add("GINI product not started", gini);
        status = GINI_STATUS_LOGIC;
    }
    else {
        status = 0;
        if (gini->num_blocks_actual < gini->num_blocks_expected) {
            unsigned nrecs = NUM_LOGICAL_RECS(gini) - gini->num_recs_actual;
            status = gini_add_missing_records(gini, nrecs);
            if (status)
                log_add("Couldn't add missing records");
        }
        if (status == 0) {
            status = gini_ensure_eop_block(gini);
            if (status) {
                log_add("Couldn't add end-of-product block");
            }
            else {
                gini->started = false;
            }
        }
    }
    return status;
}

bool gini_is_compressed(
        const gini_t* const gini)
{
    return gini->is_compressed;
}

/**
 * Returns the type of the product, which is actually a field in the NBS
 * transport-layer's product-specific header (can you say "inappropriate
 * coupling"?).
 *
 * @param[in] gini  GINI image
 * @return          Product type. One of
 *                    - 1 GOES East
 *                    - 2 GOES West
 *                    - 3 Non-GOES Imagery/DCP
 */
int gini_get_prod_type(
        const gini_t* const gini)
{
    return gini->prod_type;
}

/**
 * Returns the number of records per block.
 *
 * @param[in] gini  GINI image
 * @return          Number of records per block
 */
unsigned gini_get_recs_per_block(
        const gini_t* const gini)
{
    return gini->recs_per_block;
}

/**
 * Returns the number of bytes in a record (i.e., scan line).
 *
 * @param[in] gini  GINI image
 * @return          Number of bytes in a record (i.e., scan line)
 */
unsigned gini_get_rec_len(
        const gini_t* const gini)
{
    return BYTES_PER_REC(gini);
}

unsigned gini_get_num_blocks(
        const gini_t* const gini)
{
    return gini->num_blocks_expected;
}

/**
 * Returns the creating entity of a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf> or
 * <http://weather.unisys.com/wxp/Appendices/Formats/GINI.html#create>.
 *
 * @param[in] gini  Gini image.
 * @return          Creating entity of `gini`. One of
 *                    - 2 Miscellaneous
 *                    - 3 JERS
 *                    - 4 ERS/QuikSCAT/Scatterometer
 *                    - 5 POES/NPOESS
 *                    - 6 Composite
 *                    - 7 DMSP
 *                    - 8 GMS
 *                    - 9 METEOSAT
 *                    - 10 GOES-7 (H) Reserved for future use.
 *                    - 11 GOES-8 (I)
 *                    - 12 GOES-9 (J)
 *                    - 13 GOES-10 (K)
 *                    - 14 GOES-11 (L)
 *                    - 15 GOES-12 (M)
 *                    - 16 GOES-13 (N)
 *                    - 17 GOES-14 (O)
 *                    - 18 GOES-15 (P)
 *                    - 19 GOES-16 (Q)
 */
uint8_t gini_get_creating_entity(
        const gini_t* const gini)
{
    return pdb_get_creating_entity(&gini->pdb);
}

/**
 * Returns the sector of a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf> or
 * <http://weather.unisys.com/wxp/Appendices/Formats/GINI.html#orig_centers>.
 *
 * @param[in] gini  GINI image
 * @return          Sector of `gini`. One of
 *                    - 0 Northern Hemisphere Composite
 *                    - 1 East CONUS
 *                    - 2 West CONUS
 *                    - 3 Alaska Regional
 *                    - 4 Alaska National
 *                    - 5 Hawaii Regional
 *                    - 6 Hawaii National
 *                    - 7 Puerto Rico Regional
 *                    - 8 Puerto Rico National
 *                    - 9 Supernational
 *                    - 10 NH Composite - Meteosat/GOES E/ GOES W/GMS
 *                    - 11 Central CONUS
 *                    - 12 East Floater
 *                    - 13 West Floater
 *                    - 14 Central Floater
 *                    - 15 Polar Floater
 */
uint8_t gini_get_sector(
        const gini_t* const gini)
{
    return pdb_get_sector(&gini->pdb);
}

/**
 * Returns the physical element of a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf> or
 * <http://weather.unisys.com/wxp/Appendices/Formats/GINI.html#element>.
 *
 * @param[in] gini  GINI image
 * @return          Physical element of `gini`. One of
 *                    - 1 = Imager Visible
 *                    - 2 = Imager 3.9 micron IR
 *                    - 3 = Imager 6.7/6.5 micron IR ("WV")
 *                    - 4 = Imager 11 micron IR
 *                    - 5 = Imager 12 micron IR
 *                    - 6 = Imager 13 micron (IR)
 *                    - 7 = Imager 1.3 micron (IR)
 *                    - 8-12 = Reserved for future use
 *                    - 13 = Imager Based Derived Lifted Index (LI)
 *                    - 14 = Imager Based Derived Precipitable Water (PW)
 *                    - 15 = Imager Based Derived Surface Skin Temp (SFC Skin)
 *                    - 16 = Sounder Based Derived Lifted Index (LI)
 *                    - 17 = Sounder Based Derived Precipitable Water (PW)
 *                    - 18 = Sounder Based Derived Surface Skin Temp (SFC Skin)
 *                    - 19 = Derived Convective Available Potential Energy (CAPE)
 *                    - 20 = Derived land-sea temp
 *                    - 21 = Derived Wind Index(WINDEX)
 *                    - 22 = Derived Dry Microburst Potential Index (DMPI)
 *                    - 23 = Derived Microburst Day Potential Index (MDPI)
 *                    - 24 = Derived Convective Inhibition
 *                    - 25 = Derived Volcano Imagery
 *                    - 26 = Scatterometer Data
 *                    - 27 = Gridded Cloud Top Pressure or Height
 *                    - 28 = Gridded Cloud Amount
 *                    - 29 = Rain fall rate
 *                    - 30 = Surface wind speeds over oceans and Great Lakes
 *                    - 31 = Surface wetness
 *                    - 32 = Ice concentrations
 *                    - 33 = Ice type
 *                    - 34 = Ice edge
 *                    - 35 = Cloud water content
 *                    - 36 = Surface type
 *                    - 37 = Snow indicator
 *                    - 38 = Snow/water content
 *                    - 39 = Derived volcano imagery
 *                    - 40 = Reserved for future use
 *                    - 41 = Sounder 14.71 micron imagery
 *                    - 42 = Sounder 14.37 micron imagery
 *                    - 43 = Sounder 14.06 micron imagery
 *                    - 44 = Sounder 13.64 micron imagery
 *                    - 45 = Sounder 13.37 micron imagery
 *                    - 46 = Sounder 12.66 micron imagery
 *                    - 47 = Sounder 12.02 micron imagery
 *                    - 48 = Sounder 11.03 micron imagery
 *                    - 49 = Sounder 9.71 micron imagery
 *                    - 50 = Sounder 7.43 micron imagery
 *                    - 51 = Sounder 7.02 micron imagery
 *                    - 52 = Sounder 6.51 micron imagery
 *                    - 53 = Sounder 4.57 micron imagery
 *                    - 54 = Sounder 4.52 micron imagery
 *                    - 55 = Sounder 4.45 micron imagery
 *                    - 56 = Sounder 4.13 micron imagery
 *                    - 57 = Sounder 3.98 micron imagery
 *                    - 58 = Sounder 3.74 micron imagery
 *                    - 59 = Sounder Visible imagery
 *                    - 60-99 = Reserved for future products
 */
uint8_t gini_get_physical_element(
        const gini_t* const gini)
{
    return pdb_get_physical_element(&gini->pdb);
}

/**
 * Returns the 4-digit year of a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf>.
 * <http://weather.unisys.com/wxp/Appendices/Formats/GINI.html>.
 *
 * @param[in] gini  GINI image
 * @return          Year of `gini`
 */
int gini_get_year(
        const gini_t* const gini)
{
    return pdb_get_year(&gini->pdb);
}

/**
 * Returns the 2-digit month of a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf> or
 * <http://weather.unisys.com/wxp/Appendices/Formats/GINI.htmlcreate>.
 *
 * @param[in] gini  GINI image
 * @return          Month of `gini`
 */
int gini_get_month(
        const gini_t* const gini)
{
    return pdb_get_month(&gini->pdb);
}

/**
 * Returns the 2-digit day-of-month of a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf> or
 * <http://weather.unisys.com/wxp/Appendices/Formats/GINI.htmlcreate>.
 *
 * @param[in] gini  GINI image
 * @return          Day-of-month of `gini`
 */
int gini_get_day(
        const gini_t* const gini)
{
    return pdb_get_day(&gini->pdb);
}
/**
 * Returns the 24-hour hour of a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf> or
 * <http://weather.unisys.com/wxp/Appendices/Formats/GINI.htmlcreate>.
 *
 * @param[in] gini  GINI image
 * @return          24-hour hour of `gini`
 */

int gini_get_hour(
        const gini_t* const gini)
{
    return pdb_get_hour(&gini->pdb);
}

/**
 * Returns the minute of a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf> or
 * <http://weather.unisys.com/wxp/Appendices/Formats/GINI.htmlcreate>.
 *
 * @param[in] gini  GINI image
 * @return          Minute of `gini`
 */
int gini_get_minute(
        const gini_t* const gini)
{
    return pdb_get_minute(&gini->pdb);
}
/**
 * Returns the resolution of a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/document/ICD%20CH5-2005-1.pdf>.
 *
 * @param[in] gini  GINI image
 * @return          Month of `gini`
 */

uint8_t gini_get_image_resolution(
        const gini_t* const gini)
{
    return pdb_get_image_resolution(&gini->pdb);
}

/**
 * Returns the WMO header associated with a GINI image. See
 * <http://www.nws.noaa.gov/noaaport/html/GOES%20and%20Non%20Goes%20Compression.pdf> or
 * <http://www.nws.noaa.gov/noaaport/html/presntn.shtml>.
 *
 * @param[in] gini  GINI image
 * @return          WMO header (without the <CR><CR><LF>) of `gini`
 */
const char* gini_get_wmo_header(
        const gini_t* const gini)
{
    return gini->wmo_header;
}

/**
 * Returns the serialized size of a GINI image in bytes.
 *
 * @param[in] gini  GINI image
 * @return          Serialized size of `gini`
 */
unsigned gini_get_serialized_size(
        const gini_t* const gini)
{
    return dynabuf_get_used(gini->dynabuf);
}

/**
 * Returns the serialization of a GINI image.
 *
 * @param[in] gini  GINI image
 * @return          Serialization of `gini`
 */
uint8_t* gini_get_serialized_image(
        const gini_t* const gini)
{
    return dynabuf_get_buf(gini->dynabuf);
}

/**
 * Initializes an iterator over the blocks of a GINI image.
 *
 * @param[out] iter  Iterator
 * @param[in]  gini  GINI image
 */
void gini_iter_init(
        gini_iter_t* const restrict  iter,
        const gini_t* const restrict gini)
{
    iter->comp_offset = gini->wmo_header_len;
    iter->block = dynabuf_get_buf(gini->dynabuf);
    iter->out = dynabuf_get_buf(gini->dynabuf) +
            dynabuf_get_used(gini->dynabuf);
    iter->bytes_per_block = BYTES_PER_REC(gini) * gini->recs_per_block;
    iter->pdb_length = pdb_get_length(&gini->pdb);
    iter->is_compressed = gini->is_compressed;
    iter->num_blocks = gini->num_blocks_expected;
    iter->iblk = 0;
    iter->bytes_per_rec = BYTES_PER_REC(gini);
    iter->num_recs = gini->num_recs_actual;
}

/**
 * Returns the next block of a GINI image.
 *
 * @param[in,out] iter        Iterator
 * @param[out]    block       Start of data-block (might be compressed)
 * @param[out]    nbytes      Number of bytes in `block`
 * @retval 0                  There's another block of data
 * @retval GINI_STATUS_END    No more data
 * @retval GINI_STATUS_INVAL  GINI image is invalid. log_add() called.
 * @retval GINI_STATUS_SYSTEM System error. log_add() called.
 */
gini_status_t gini_iter_next(
        gini_iter_t* const                      iter,
        const uint8_t* restrict* const restrict block,
        unsigned* const restrict                nbytes)
{
    return (iter->iblk >= iter->num_blocks)
        ? GINI_STATUS_END
        : iter_next(iter, block, nbytes);
}

/**
 * Finalizes an iterator over a GINI image.
 *
 * @param[in,out] iter  GINI iterator
 */
void gini_iter_fini(
        gini_iter_t* const iter)
{
}

/**
 * zlib(3) compresses data in one buffer into another.
 *
 * @param[in]  in_buf              Data to be compressed.
 * @param[in]  in_nbytes           Number of bytes in `in_buf`
 * @param[out] out_buf             Compressed data
 * @param[out] out_size            Size of `out_buf` in bytes
 * @param[out] out_nbytes          Number of compressed bytes
 * @retval     0                   Success. `out_buf` and `*out_nbytes` are set.
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
gini_status_t gini_pack(
        const uint8_t* const restrict in_buf,
        const unsigned                in_nbytes,
        uint8_t* const restrict       out_buf,
        const unsigned                out_size,
        unsigned* const restrict      out_nbytes)
{
    z_stream d_stream = {};
    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree  = (free_func)0;
    d_stream.opaque = (voidpf)0;

    /*
     * According to Raytheon's Sathya Sankarasubbu, `Z_BEST_COMPRESSION` is used
     * in the NOAAPort uplink. Steve Emmerson 2016-03-29
     */
    int status = deflateInit(&d_stream, Z_BEST_COMPRESSION);
    if (status) {
        log_add("zlib::deflateInit() failure");
        status = GINI_STATUS_SYSTEM;
    }
    else {
        d_stream.next_in   = (uint8_t*)in_buf;
        d_stream.avail_in  = in_nbytes;
        d_stream.next_out  = out_buf;
        d_stream.avail_out = out_size;

        status = deflate(&d_stream, Z_FINISH);
        if (status != Z_STREAM_END) {
            log_add("zlib::deflate() failure: insufficient compressed space");
            status = GINI_STATUS_SYSTEM;
        }
        else {
            *out_nbytes = d_stream.total_out;
            status = 0;
        }
        (void)deflateEnd(&d_stream);
    }
    return status;
}

/**
 * zlib(3)-de-compresses data in one buffer into another.
 *
 * @param[in]  in_buf              Data to be de-compressed.
 * @param[in]  in_nbytes           Number of bytes in `in_buf`
 * @param[out] out_buf             De-compressed data
 * @param[out] out_size            Size of `out_buf` in bytes
 * @param[out] out_nbytes          Number of de-compressed bytes
 * @param[out] nscanned            Number of bytes read from `in_buf`
 * @retval     0                   Success. `out_buf` and `*out_nbytes` are set.
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
gini_status_t gini_unpack(
        const uint8_t* const restrict in_buf,
        const size_t                  in_nbytes,
        uint8_t* const restrict       out_buf,
        const unsigned                out_size,
        unsigned* const restrict      out_nbytes,
        unsigned* const restrict      nscanned)
{
    z_stream d_stream = {};
    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree  = (free_func)0;
    d_stream.opaque = (voidpf)0;

    int status = inflateInit(&d_stream);
    if (status) {
        log_add("zlib::inflateInit() failure");
        status = GINI_STATUS_SYSTEM;
    }
    else {
        d_stream.next_in   = (uint8_t*)in_buf; // Safe cast
        d_stream.avail_in  = in_nbytes;
        d_stream.next_out  = out_buf;
        d_stream.avail_out = out_size;

        status = inflate(&d_stream, Z_FINISH);
        if (status != Z_STREAM_END) {
            log_add("zlib::inflate() failure: insufficient uncompressed space");
            status = GINI_STATUS_SYSTEM;
        }
        else {
            *out_nbytes = d_stream.total_out;
            *nscanned = d_stream.next_in - in_buf;
            log_debug("Inflated %u bytes to %u bytes", *nscanned, *out_nbytes);
            status = 0;
        }
        (void)inflateEnd(&d_stream);
    }
    return status;
}
