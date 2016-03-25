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

#include "pq.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct nbsp nbsp_t;

typedef enum {
    NBSP_STATUS_SUCCESS = 0,
    NBSP_STATUS_INVAL,
    NBSP_STATUS_NOMEM,
    NBSP_STATUS_SYSTEM,
    NBSP_STATUS_STATE
} nbsp_status_t;

#ifdef __cplusplus
    extern "C" {
#endif

nbsp_status_t nbsp_new(
        nbsp_t* restrict* const restrict nbsp);

/**
 * Ends the current product. Does nothing if there's no current product.
 * Idempotent.
 */
void nbsp_end_product(
        nbsp_t* const nbsp);

nbsp_status_t nbsp_nesdis_start(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                recs_per_block,
        const bool                    is_compressed,
        size_t                        size_estimate);

nbsp_status_t nbsp_nesdis_block(
        nbsp_t* const restrict  nbsp,
        const uint8_t* restrict buf,
        const unsigned          nbytes,
        const unsigned          block_num,
        const bool              is_compressed);

nbsp_status_t nbsp_nongoes(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end,
        const bool                    is_compressed);

nbsp_status_t nbsp_nwstg(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end);

nbsp_status_t nbsp_nexrad(
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
