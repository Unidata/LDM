/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_presentation.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the presentation layer of the NOAAPort
 * Broadcast System (NBS).
 */

#ifndef NOAAPORT_NBS_PRESENTATION_H_
#define NOAAPORT_NBS_PRESENTATION_H_

#include "nbs_application.h"
#include "nbs_status.h"
#include "pq.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct nbsp nbsp_t;

#ifdef __cplusplus
    extern "C" {
#endif

nbs_status_t nbsp_new(
        nbsp_t* restrict* const restrict nbsp,
        nbsa_t* const restrict           nbsa);

/**
 * Ends the current product. Does nothing if there's no current product.
 * Idempotent.
 */
nbs_status_t nbsp_end_product(
        nbsp_t* const nbsp);

nbs_status_t nbsp_gini_start(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                rec_len,
        const unsigned                recs_per_block,
        const bool                    is_compressed,
        const int                     prod_transfer_type,
        size_t                        size_estimate);

nbs_status_t nbsp_gini_block(
        nbsp_t* const restrict  nbsp,
        const uint8_t* restrict buf,
        const unsigned          nbytes,
        const unsigned          block_num,
        const bool              is_compressed);

nbs_status_t nbsp_nongoes(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end,
        const bool                    is_compressed);

nbs_status_t nbsp_nwstg(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end);

nbs_status_t nbsp_nexrad(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end);

void nbsp_free(
        nbsp_t* const nbsp);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_PRESENTATION_H_ */
