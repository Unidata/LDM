/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: gini.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for GINI-formated products.
 */

#ifndef NOAAPORT_GINI_H_
#define NOAAPORT_GINI_H_

#include "dynabuf.h"

#include <stdbool.h>

typedef enum {
    GINI_STATUS_SUCCESS = 0,
    GINI_STATUS_INVAL,
    GINI_STATUS_NOMEM,
    GINI_STATUS_LOGIC,
    GINI_STATUS_END,
    GINI_STATUS_SYSTEM
} gini_status_t;

typedef struct gini gini_t;

typedef struct {
    const uint8_t* block;           ///< Next block to return
    const uint8_t* out;             ///< Just beyond GINI data
    unsigned       comp_offset;     ///< Number of bytes to compressed data
                                    ///< (i.e., length of clear-text WMO header)
    unsigned       bytes_per_block; ///< Number of bytes per canonical,
                                    ///< uncompressed block
    unsigned       bytes_per_rec;         ///< Size of uncompressed record (i.e., scan
                                    ///< line) in bytes
    unsigned       num_recs_returned;   ///< Number of records returned
    unsigned       num_recs;        ///< Number of records in image -- including
                                    ///< end-of--product record
    unsigned       pdb_length;      ///< Length of serialized PDB in bytes
    unsigned       num_blocks;      ///< Number of blocks in GINI product
    unsigned       iblk;            ///< Origin-0 index of next block
    bool           is_compressed;   ///< Is the image zlib(3)-compressed?
} gini_iter_t;

#ifdef __cplusplus
    extern "C" {
#endif

gini_status_t gini_new(
        gini_t** const restrict   gini,
        dynabuf_t* const restrict dynabuf);

gini_status_t gini_start(
        gini_t* const restrict        gini,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                rec_len,
        const unsigned                recs_per_block,
        const bool                    is_compressed,
        const int                     prod_type);

gini_status_t gini_deserialize(
        gini_t* const restrict        gini,
        const uint8_t* const restrict buf,
        const size_t                  nbytes);

gini_status_t gini_add_block(
        gini_t* const restrict        gini,
        const unsigned                block_index,
        const uint8_t* const restrict data,
        const unsigned                nbytes,
        const bool                    is_compressed);

gini_status_t gini_add_missing_blocks(
        gini_t* const  gini,
        const unsigned valid_block_index);

gini_status_t gini_finish(
        gini_t* const gini);

bool gini_is_compressed(
        const gini_t* const gini);

int gini_get_prod_type(
        const gini_t* const gini);

unsigned gini_get_rec_len(
        const gini_t* const gini);

unsigned gini_get_num_blocks(
        const gini_t* const gini);

unsigned gini_get_recs_per_block(
        const gini_t* const gini);

uint8_t gini_get_creating_entity(
        const gini_t* const gini);

uint8_t gini_get_sector(
        const gini_t* const gini);

uint8_t gini_get_physical_element(
        const gini_t* const gini);

int gini_get_year(
        const gini_t* const gini);

int gini_get_month(
        const gini_t* const gini);

int gini_get_day(
        const gini_t* const gini);

int gini_get_hour(
        const gini_t* const gini);

int gini_get_minute(
        const gini_t* const gini);

uint8_t gini_get_image_resolution(
        const gini_t* const gini);

const char* gini_get_wmo_header(
        const gini_t* const gini);

unsigned gini_get_serialized_size(
        const gini_t* const gini);

uint8_t* gini_get_serialized_image(
        const gini_t* const gini);

void gini_free(
        gini_t* gini);

void gini_iter_init(
        gini_iter_t* const restrict  iter,
        const gini_t* const restrict gini);

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
        unsigned* const restrict                nbytes);

/**
 * Finalizes an iterator over a GINI image.
 *
 * @param[in,out] iter  GINI iterator
 */
void gini_iter_fini(
        gini_iter_t* const iter);

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
        unsigned* const restrict      out_nbytes);

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
        unsigned* const restrict      nscanned);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_GINI_H_ */
