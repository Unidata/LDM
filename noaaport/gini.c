/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: gini.c
 * @author: Steven R. Emmerson
 *
 * This file implements a GINI object.
 */

#include "config.h"

#include "decode.h"
#include "dynabuf.h"
#include "gini.h"
#include "ldm.h"
#include "log.h"

#include <search.h>
#include <zlib.h>

/*
 * Maximum number of characters in a WMO header:
 *     T1T2A1A2ii (sp) CCCC (sp) YYGGgg [(sp)BBB] (cr)(cr)(lf)
 */
#define WMO_HEADER_MAX_ENCODED_LEN 25
typedef char wmo_header_t[WMO_HEADER_MAX_ENCODED_LEN-3+1]; // minus \r\r\n plus \0

#define GINI_MIN(a, b) ((a) <= (b) ? (a) : (b))

/******************************************************************************
 * Utilities:
 ******************************************************************************/

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
static gini_status_t pack(
        const uint8_t* const restrict in_buf,
        const unsigned                in_nbytes,
        uint8_t* const restrict       out_buf,
        const unsigned                out_size,
        unsigned* const restrict      out_nbytes)
{
    z_stream d_stream;
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
 * @retval     0                   Success. `out_buf` and `*out_nbytes` are set.
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
static gini_status_t unpack(
        const uint8_t* const restrict in_buf,
        const unsigned                in_nbytes,
        uint8_t* const restrict       out_buf,
        const unsigned                out_size,
        unsigned* const restrict      out_nbytes)
{
    z_stream d_stream;
    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree  = (free_func)0;
    d_stream.opaque = (voidpf)0;

    int status = inflateInit(&d_stream);
    if (status) {
        log_add("zlib::inflateInit() failure");
        status = GINI_STATUS_SYSTEM;
    }
    else {
        d_stream.next_in   = (uint8_t*)in_buf;
        d_stream.avail_in  = in_nbytes;
        d_stream.next_out  = out_buf;
        d_stream.avail_out = out_size;

        status = inflate(&d_stream, Z_FINISH);
        if (status != Z_STREAM_END) {
            log_add("zlib::inflate() failure: insufficient decompressed space");
            status = GINI_STATUS_SYSTEM;
        }
        else {
            *out_nbytes = d_stream.total_out;
            status = 0;
        }
        (void)inflateEnd(&d_stream);
    }
    return status;
}

/******************************************************************************
 * Blank Space:
 *
 * Used for missing scan-lines in a GINI object.
 ******************************************************************************/

typedef struct {
    uint8_t* data;   ///< Blank space
    unsigned nbytes; ///< Number of bytes in `data`
} blank_space_t;

/**
 * Returns a new blank space.
 *
 * @param[out] space               New blank space
 * @param[in]  nbytes              Number of uncompressed bytes in `space`
 * @param[in]  compressed          Should the space be zlib(3) compressed?
 * @retval     0                   Success
 * @retval     GINI_STATUS_NOMEM   Out of memory. log_add() called.
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
static gini_status_t blank_space_new(
        blank_space_t** const space,
        const unsigned        nbytes,
        const bool            compressed)
{
    int status;
    blank_space_t* obj = log_malloc(sizeof(blank_space_t), "blank space");
    if (obj == NULL) {
        status = GINI_STATUS_NOMEM;
    }
    else {
        obj->data = log_malloc(nbytes, "blank space");
        if (obj->data) {
            if (compressed) {
                uint8_t buf[nbytes];
                memset(buf, 0, nbytes);
                status = pack(buf, nbytes, obj->data, nbytes, &obj->nbytes);
                if (status)
                    log_add("Couldn't compress %u-byte blank space", nbytes);
            }
            if (status)
                free(obj->data);
        }
        if (status) {
            free(obj);
        }
        else {
            *space = obj;
        }
    }
    return status;
}

/**
 * Frees a blank space.
 *
 * @param[in] space Blank space
 */
static void blank_space_free(
        blank_space_t* const space)
{
    if (space) {
        free(space->data);
        free(space);
    }
}

/******************************************************************************
 * Blank Spaces (note the plural):
 *
 * Contains an array of blank spaces -- one for each possible number of records
 * (i.e., scan lines) to replace (excluding 0).
 ******************************************************************************/

typedef struct {
    blank_space_t** spaces;
    unsigned        rec_len;
    unsigned        max_recs;
    bool            compressed;
} blank_spaces_t;

/**
 * Initializes a blank spaces object.
 *
 * @param[in,out] blank_spaces       Object to initialize
 * @param[in]     rec_len            Number of uncompressed bytes in a record
 * @param[in]     max_recs           Maximum number of records (i.e., scan
 *                                   lines)
 * @param[in]     compressed         Should the blank lines be compressed?
 * @retval        0                  Success
 * @retval        GINI_STATUS_INVAL  `max_recs == 0`. log_add() called.
 */
static gini_status_t blank_spaces_init(
        blank_spaces_t* const blank_spaces,
        const unsigned        rec_len,
        const unsigned        max_recs,
        const bool            compressed)
{
    int status;
    if (max_recs == 0) {
        status = GINI_STATUS_INVAL;
    }
    else {
        blank_spaces->spaces = NULL; // Set on-demand
        blank_spaces->rec_len = rec_len;
        blank_spaces->max_recs = max_recs;
        blank_spaces->compressed = compressed;
        status = 0;
    }
    return status;
}

/**
 * Returns a new blank spaces object.
 *
 * @param[out] blank_spaces       New object
 * @param[in]  rec_len            Number of uncompressed bytes in a record
 *                                (i.e., scan line)
 * @param[in]  max_recs           Maximum number of records (i.e., scan
 *                                lines)
 * @param[in]  compressed         Should the blank lines be compressed?
 * @retval     0                  Success
 * @retval     GINI_STATUS_INVAL  `max_recs == 0`. log_add() called.
 * @retval     GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
static gini_status_t blank_spaces_new(
        blank_spaces_t** const blank_spaces,
        const unsigned         rec_len,
        const unsigned         max_recs,
        const bool             compressed)
{
    int status;
    blank_spaces_t* obj = log_malloc(sizeof(blank_spaces_t), "blank spaces");
    if (obj == NULL) {
        status = GINI_STATUS_NOMEM;
    }
    else {
        status = blank_spaces_init(obj, rec_len, max_recs, compressed);
        if (status) {
            log_add("Couldn't initialize blank spaces");
            free(obj);
        }
        else {
            *blank_spaces = obj;
        }
    }
    return status;
}

/**
 * Compares two blank spaces objects.
 *
 * @param[in] o1  First object
 * @param[in] o2  Second object
 * @return        A value less than, equal to, or greater than zero as the first
 *                object is considered less than, equal to, or greater than the
 *                second object, respectively
 */
static int blank_spaces_compare(
        const void* o1,
        const void* o2)
{
    const blank_spaces_t* br1 = o1;
    const blank_spaces_t* br2 = o2;
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
 * Ensures that the array of pointers to blank spaces exists. The array is
 * initialized to `NULL` pointers if it's created.
 *
 * @param[in,out] blank_spaces       Blank spaces object
 * @retval        0                  Success. `blank_spaces->spaces != NULL`.
 * @retval        GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
static gini_status_t blank_spaces_ensure_pointer_array(
        blank_spaces_t* const blank_spaces)
{
    int status;
    if (blank_spaces->spaces == NULL) {
        blank_space_t** spaces = log_malloc(
                blank_spaces->max_recs*sizeof(blank_space_t*),
                "array of pointers to blank spaces");
        if (spaces == NULL) {
            status = GINI_STATUS_NOMEM;
        }
        else {
            for (unsigned i = 0; i < blank_spaces->max_recs; i++)
                spaces[i] = NULL;
        }
    }
    return status;
}

/**
 * Ensures that a blank space of a given size exists in a blank spaces object.
 *
 * @param[in,out] blank_spaces        Blank spaces object
 * @param[in]     nrecs               Number of records (i.e., scan lines) in
 *                                    the blank space
 * @retval        0                   Success. `blank_spaces->spaces[nrec-1] !=
 *                                    NULL`.
 * @retval        GINI_STATUS_NOMEM   Out of memory. log_add() called.
 * @retval        GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
static gini_status_t blank_spaces_ensure_blank_space(
        blank_spaces_t* const blank_spaces,
        const unsigned        nrecs)
{
    int             status;
    blank_space_t** space = blank_spaces->spaces + nrecs - 1;
    if (*space == NULL) {
        status = blank_space_new(space, nrecs*blank_spaces->rec_len,
                blank_spaces->compressed);
        if (status)
            log_add("Couldn't create new blank space: nrecs=%u", nrecs);
    }
    return status;
}

/**
 * Returns the blank space corresponding to a given number of records (i.e.,
 * scan lines) in a blank spaces object.
 *
 * @param[in]  blank_spaces        Blank spaces object
 * @param[in]  nrecs               Number of records (i.e., scan lines)
 * @param[out] data                Bytes of the blank space
 * @param[out] nbytes              Number of bytes in `data`
 * @retval     0                   Success. `*data` and `*nbytes` are set.
 * @retval     GINI_STATUS_INVAL   `nrecs > blank_spaces->max_recs`. log_add()
 *                                 called.
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
static gini_status_t blank_spaces_get(
        blank_spaces_t* const                   blank_spaces,
        const unsigned                          nrecs,
        const uint8_t* restrict* const restrict data,
        size_t* const restrict                  nbytes)
{
    int status;
    log_assert(blank_spaces);
    log_assert(nrecs);
    if (nrecs > blank_spaces->max_recs) {
        log_add("Number of records (%u) > maximum possible (%u)", nrecs,
                blank_spaces->max_recs);
        status = GINI_STATUS_INVAL;
    }
    else {
        status = blank_spaces_ensure_pointer_array(blank_spaces);
        if (status == 0) {
            status = blank_spaces_ensure_blank_space(blank_spaces, nrecs);
            if (status == 0) {
                *data = blank_spaces->spaces[nrecs-1]->data;
                *nbytes = blank_spaces->spaces[nrecs-1]->nbytes;
            }
        }
    }
    return status;
}

/**
 * Frees a blank spaces object.
 *
 * @param[in,out] blank_spaces  The object to be freed.
 */
static void blank_spaces_free(
        blank_spaces_t* blank_spaces)
{
    if (blank_spaces) {
        if (blank_spaces->spaces) {
            for (unsigned i = 0; i < blank_spaces->max_recs; i++)
                blank_space_free(blank_spaces->spaces[i]);
            free(blank_spaces->spaces);
        }
    }
}

/******************************************************************************
 * Missing Records Database:
 *
 * A database of missing records (i.e., scan lines).
 ******************************************************************************/

typedef struct {
    void* root;
} mrdb_t;

/**
 * Initializes a missing records database.
 *
 * @param[in,out] mrdb  Database to be initialized
 */
static void mrdb_init(
        mrdb_t* const mrdb)
{
    log_assert(mrdb);
    mrdb->root = NULL;
}

/**
 * Returns the blank spaces object corresponding to given record lengths,
 * maximum number of records, and compression in a blank space.
 *
 * @param[in]  mrdb               Missing records database
 * @param[in]  rec_len            Number of bytes in a record
 * @param[in]  max_recs           Maximum number of records
 * @param[in]  compressed         Should the blank space be compressed?
 * @param[out] blank_spaces       Returned blank spaces object.
 * @retval     0                  Success. `*blank_spaces` set.
 * @retval     GINI_STATUS_INVAL  `max_recs == 0`. log_add() called.
 * @retval     GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
static gini_status_t mrdb_get(
        mrdb_t* const restrict                   mrdb,
        const unsigned                           rec_len,
        const unsigned                           max_recs,
        const bool                               compressed,
        blank_spaces_t* restrict* const restrict blank_spaces)
{
    blank_spaces_t* key;
    int             status = blank_spaces_new(&key, rec_len, max_recs,
            compressed);
    if (status == 0) {
        blank_spaces_t** spaces = tsearch(key, &mrdb->root,
                blank_spaces_compare);
        if (spaces == NULL) {
            log_add("Couldn't add blank spaces to database");
            status = GINI_STATUS_NOMEM;
        }
        else {
            *blank_spaces = *spaces;
            status = 0;
        }
        if (key != *spaces)
            blank_spaces_free(key);
    }
    return status;
}

/**
 * Finalizes a missing records database.
 *
 * @param[in,out] mrdb  Missing records data to be finalized
 */
static void mrdb_fini(
        mrdb_t* mrdb)
{
    while (mrdb->root) {
        blank_spaces_t* blank_spaces = *(blank_spaces_t**)mrdb->root;
        (void)tdelete(blank_spaces, &mrdb->root, blank_spaces_compare);
        blank_spaces_free(blank_spaces);
    }
}

/******************************************************************************
 * Filler of Missing Records:
 *
 * Adds missing scan-lines to a GINI object.
 ******************************************************************************/

typedef struct {
    mrdb_t           mrdb;
    blank_spaces_t*  blank_spaces; ///< Current blank space from `mrdb`
} filler_t;

/**
 * Initializes a filler object.
 *
 * @param[in,out] filler  Object to be initialized
 */
static void filler_init(
        filler_t* const filler)
{
    mrdb_init(&filler->mrdb);
    filler->blank_spaces = NULL;
}

/**
 * Configures a filler object for a particular GINI object
 *
 * @param[in,out] filler             Filler object to be configured
 * @param[in]     rec_len            Number of bytes in a record
 * @param[in]     max_recs           Maximum number of records
 * @param[in]     compressed         Should the records be compressed?
 * @retval        0                  Success. `filler_fill()` will use the above
 *                                   parameters in filling a GINI object.
 * @retval        GINI_STATUS_INVAL  `max_recs == 0`. log_add() called.
 * @retval        GINI_STATUS_NOMEM  Out of memory. log_add() called.
 */
static gini_status_t filler_config(
        filler_t* const filler,
        const unsigned  rec_len,
        const unsigned  max_recs,
        const bool      compressed)
{
    int status = mrdb_get(&filler->mrdb, rec_len, max_recs, compressed,
            &filler->blank_spaces);
    return status;
}

/**
 * Fills a dynamic buffer with the requested number of blank records.
 *
 * @param[in]  filler              Filler object
 * @param[out] dynabuf             Dynamic buffer to append to
 * @param[in]  nrecs               Number of blank records to add
 * @retval     0                   Success. Requested number of blank records
 *                                 were added to `dynabuf`.
 * @retval     GINI_STATUS_INVAL   `nrecs > blank_spaces->max_recs`. log_add()
 *                                 called.
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 * @retval     GINI_STATUS_NOMEM   Out of memory. log_add() called.
 */
static gini_status_t filler_fill(
        filler_t* const restrict  filler,
        dynabuf_t* const restrict dynabuf,
        const unsigned            nrecs)
{
    log_assert(filler);
    log_assert(dynabuf);
    log_assert(filler->blank_spaces);
    const uint8_t* space;
    size_t         nbytes;
    int status = blank_spaces_get(filler->blank_spaces, nrecs, &space, &nbytes);
    if (status) {
        log_add("Couldn't get blank space: nrecs=%u", nrecs);
    }
    else {
        status = dynabuf_add(dynabuf, space, nbytes);
        if (status)
            log_add("Couldn't add blank space to dynamic buffer: nbytes=%zu",
                    nbytes);
    }
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
    mrdb_fini(&filler->mrdb);
    filler->blank_spaces = NULL;
}

/******************************************************************************
 * WMO Header:
 ******************************************************************************/

/**
 * Decodes an encoded WMO header.
 *
 * @param[out] wmo_header     Decoded WMO header
 * @param[in]  buf            Encoded WMO header
 * @param[in]  nbytes         Number of bytes in `buf`
 * @param[out] nscanned       Number of bytes scanned
 * @retval 0                  Success. `*wmo_header` is set.
 * @retval GINI_STATUS_INVAL  Invalid WMO header
 */
static gini_status_t wmoheader_decode(
        char* const restrict          wmo_header,
        const uint8_t* restrict const buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned)
{
    int               status;
    const char*       cp = buf;
    const char* const out = buf + GINI_MIN(WMO_HEADER_MAX_ENCODED_LEN, nbytes);
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
    uint_fast32_t nx;               ///< Number of pixels per scan line
    uint_fast32_t ny;               ///< Number of scan lines (i.e., records)
    uint_fast32_t image_res;
    uint_fast32_t is_compressed;
    uint_fast32_t version;          ///< Creating entity's PDB version
    uint_fast32_t length;           ///< Length of PDB in bytes
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

static inline unsigned pdb_get_num_logical_recs(
        pdb_t* const pdb)
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
 * @retval    GINI_STATUS_INVAL  Invalid header
 */
static gini_status_t gini_headers_decode(
        char* const restrict     wmo_header,
        pdb_t* const restrict    pdb,
        const uint8_t* restrict  buf,
        unsigned                 nbytes,
        unsigned* const restrict nscanned)
{
    unsigned n;
    int      status = wmoheader_decode(wmo_header, buf, nbytes, &n);
    if (status) {
        log_add("Couldn't decode clear-text WMO header");
    }
    else {
        *nscanned = n;
        buf += n;
        nbytes -= n;
        status = pdb_decode(pdb, buf, nbytes, &n);
        if (status) {
            log_add("Couldn't decode encoded product-definition block");
        }
        else {
            *nscanned += n;
        }
    }
    return status;
}

/******************************************************************************
 * GINI Object:
 ******************************************************************************/

struct gini {
    dynabuf_t*     dynabuf;            ///< Dynamic buffer for accumulating data
    pdb_t          pdb;                ///< Product-definition block
    uint8_t        buf[6000];          ///< De-compression buffer
    wmo_header_t   wmo_header;         ///< WMO header: T1T2A1A2ii CCCC YYGGgg
                                       ///< [BBB]
    filler_t       filler;             ///< Filler of missing records
    unsigned       rec_len;            ///< Number of bytes in a record
    unsigned       recs_per_block;     ///< Number of records (i.e., scan lines)
                                       ///< in a data-block
    unsigned       num_added_blocks;   ///< Number of added blocks -- including
                                       ///< product-start block with PDB
    int            prod_type; /// Product transfer type
    bool           started;            ///< Has instance been started?
    bool           compress;           ///< Should compression be ensured?
};

/**
 * Initializes a GINI object.
 *
 * @param[in,out] gini     GINI object to be initialized
 * @param[in]     dynabuf  Dynamic buffer to be used to accumulate data
 */
static void gini_init(
        gini_t* const restrict    gini,
        dynabuf_t* const restrict dynabuf)
{
    log_assert(gini != NULL);
    log_assert(dynabuf != NULL);
    gini->dynabuf = dynabuf;
    gini->started = false;
}

/**
 * @retval     GINI_STATUS_SYSTEM  System failure. log_add() called.
 */
static gini_status_t gini_ensure_uncompressed_headers(
        gini_t* const restrict                  gini,
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
        gini->compress = false;
        status = 0;
    }
    else {
        status = unpack(buf, GINI_MIN(nbytes, 540), gini->buf,
                sizeof(gini->buf), info_nbytes);
        if (status) {
            log_add("Couldn't uncompress start of encoded GINI image");
        }
        else {
            *info_buf = gini->buf;
            gini->compress = true;
        }
    }
    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

static filler_t filler;     ///< Adds blank records (scan lines) to GINI object
static size_t   gini_count; ///< Number of extant GINI objects

/**
 * Returns a new GINI object.
 *
 * @param[out] gini           Returned GINI object.
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
        gini_t* obj = log_malloc(sizeof(gini_t), "GINI object");
        if (obj == NULL) {
            status = GINI_STATUS_NOMEM;
        }
        else {
            gini_init(obj, dynabuf);
            if (gini_count++ == 0)
                filler_init(&filler);
            *gini = obj;
        }
    }
    return status;
}

/**
 * Starts a GINI object.
 *
 * @param[in,out] gini           GINI object to be started
 * @param[in]     prod_type      NBS transport-layer product-specific header
 *                               product-type
 * @retval 0                     Success. `*gini` started.
 * @retval GINI_STATUS_INVAL     `recs_per_block == 0`. log_add() called.
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
        log_add("GINI object already started");
        status = GINI_STATUS_LOGIC;
    }
    else {
        const uint8_t* uncompressed_headers;
        unsigned       uncompressed_nbytes;
        status = gini_ensure_uncompressed_headers(gini, buf, nbytes,
                is_compressed, &uncompressed_headers, &uncompressed_nbytes);
        if (status == 0) {
            unsigned n;
            status = gini_headers_decode(gini->wmo_header, &gini->pdb,
                    uncompressed_headers, uncompressed_nbytes, &n);
            if (status == 0) {
                status = filler_config(&filler, rec_len, recs_per_block,
                        is_compressed);
                if (status) {
                    log_add("Couldn't configure filler of missing-records");
                }
                else {
                    dynabuf_clear(gini->dynabuf);
                    status = dynabuf_add(gini->dynabuf, buf, nbytes);
                    if (status) {
                        log_add("Couldn't add GINI headers to dynamic buffer");
                    }
                    else {
                        gini->num_added_blocks = 1; // Start-block
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
 * Adds a data-block to a GINI object. The block is compressed or uncompressed
 * as necessary to match previous blocks.
 *
 * @param[in,out] gini           GINI presentation-layer GINI object
 * @param[in]     data           Data-block
 * @param[in]     nbytes         Number of bytes in `data`
 * @param[in]     is_compressed  Is `data` zlib(3)-compressed?
 * @retval GINI_STATUS_INVAL     `gini == NULL`. log_add() called.
 * @retval GINI_STATUS_LOGIC     `gini_start(gini)` not called. log_add()
 *                               called.
 * @retval GINI_STATUS_NOMEM     Out of memory. log_add() called.
 * @retval GINI_STATUS_SYSTEM    System failure. log_add() called.
 */
gini_status_t gini_add_block(
        gini_t* const restrict        gini,
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
        log_add("GINI object not started");
        status = GINI_STATUS_LOGIC;
    }
    else if (gini->compress == is_compressed) {
        status = dynabuf_add(gini->dynabuf, data, nbytes);
        if (status) {
            log_add("Couldn't copy data-block to product-buffer");
        }
    }
    else {
        /*
         * Because the uncompressed data blocks are <= 5120 bytes, 10000 should
         * do it.
         */
        const unsigned reserve = 10000;
        dynabuf_t*     dynabuf = gini->dynabuf;
        status = dynabuf_reserve(dynabuf, reserve);
        if (status) {
            log_add("Couldn't reserve space in product-buffer");
        }
        else {
            unsigned n;
            uint8_t* buf = dynabuf_get_buf(dynabuf);
            status = is_compressed
                    ? unpack(data, nbytes, buf, reserve, &n)
                    : pack(data, nbytes, buf, reserve, &n);
            if (status) {
                log_add("Couldn't %scompress data-block into product-buffer",
                        is_compressed ? "un" : "");
            }
        }
    }
    if (status == 0)
        gini->num_added_blocks++;
    return status;
}

/**
 * Adds missing data-blocks to a GINI object if appropriate. The blocks are
 * compressed or uncompressed as necessary to match previous blocks.
 *
 * @param[in] gini               GINI object
 * @param[in] valid_block_index  Index of next valid data-block. Start-block
 *                               with product-definition header is block 0.
 * @retval 0                     Success
 * @retval GINI_STATUS_INVAL     `gini == NULL` or `valid_block_index` is
 *                               invalid. log_add() called.
 * @retval GINI_STATUS_LOGIC     `gini_start(gini)` not called. log_add()
 *                               called.
 * @retval GINI_STATUS_NOMEM      Out of memory. log_add() called.
 * @retval GINI_STATUS_SYSTEM     System failure. log_add() called.
 */
gini_status_t gini_add_missing_blocks(
        gini_t* const  gini,
        const unsigned valid_block_index)
{
    int status;
    if (gini == NULL) {
        log_add("NULL argument; gini=%p", gini);
        status = GINI_STATUS_INVAL;
    }
    else if (!gini->started) {
        log_add("GINI object not started", gini);
        status = GINI_STATUS_LOGIC;
    }
    else if (valid_block_index < gini->num_added_blocks) {
        log_add("Index of data-block being processed (%u) < number of already "
                "processed blocks (%u)", valid_block_index,
                gini->num_added_blocks);
        status = GINI_STATUS_INVAL;
    }
    else {
        status = 0;
        for (unsigned i = gini->num_added_blocks; i <  valid_block_index;
                i++) {
            status = filler_fill(&gini->filler, gini->dynabuf,
                    gini->recs_per_block);
            if (status) {
                log_add("Couldn't add missing block %u", i);
                break;
            }
        }
    }
    return status;
}

/**
 * Finishes a GINI object. Pads the end of the image with blank scan-lines if
 * necessary. After this, gini_start() will have to be called on the object
 * before gini_add_block(), gini_add_missing_blocks(), or gini_finish() can be
 * called on it.
 *
 * @param[in,out] gini        GINI object to be finished
 * @retval 0                  Success. Image was padded with blank data-blocks
 *                            if necessary.
 * @retval GINI_STATUS_INVAL  `gini == NULL`. log_add() called.
 * @retval GINI_STATUS_LOGIC  `gini_start(gini)` not called. log_add() called.
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
        log_add("GINI object not started", gini);
        status = GINI_STATUS_LOGIC;
    }
    else {
        unsigned max_processed_recs = gini->recs_per_block *
                (gini->num_added_blocks - 1); // Exclude product-start block
        unsigned num_recs;
        status = 0;
        unsigned block_index = gini->num_added_blocks;
        for (int num_missing_recs = pdb_get_num_logical_recs(&gini->pdb) -
                    max_processed_recs;
                num_missing_recs > 0;
                num_missing_recs -= num_recs) {
            unsigned num_recs =
                    GINI_MIN(gini->recs_per_block, num_missing_recs);
            status = filler_fill(&gini->filler, gini->dynabuf, num_recs);
            if (status) {
                log_add("Couldn't append missing block %u", block_index);
                break;
            }
            block_index++;
        }
        if (status == 0)
            gini->started = false;
    }
    return status;
}

/**
 * Finalizes a GINI object.
 *
 * @param[in,out] gini  GINI object to be finalized.
 */
static void gini_fini(
        gini_t* const gini)
{
    log_assert(gini);
}

bool gini_is_compressed(
        const gini_t* const gini)
{
    return gini->compress;
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
 * Frees a GINI object.
 *
 * @param[in] gini  GINI object to be freed or `NULL`.
 */
void gini_free(
        gini_t* const gini)
{
    if (gini) {
        if (--gini_count == 0)
            filler_fini(&filler);
        gini_fini(gini);
        free(gini);
    }
}
