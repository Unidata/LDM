/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: pdb.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the product-definition block of a NESDIS-
 * formatted product.
 */

#ifndef NOAAPORT_PDB_H_
#define NOAAPORT_PDB_H_

#include "nbs_status.h"

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

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * @retval    NBS_STATUS_INVAL  Invalid header
 */
nbs_status_t pdb_decode(
        pdb_t* const restrict         pdb,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        unsigned* const restrict      nscanned);

static inline unsigned pdb_get_num_logical_recs(
        pdb_t* const pdb)
{
    return pdb->num_logical_recs;
}

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_PDB_H_ */
