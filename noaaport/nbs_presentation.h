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

typedef struct nbsp nbsp_t;

#include "nbs_application.h"
#include "nbs_transport.h"
#include "nbs.h"
#include "pq.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
    extern "C" {
#endif

nbs_status_t nbsp_new(
        nbsp_t* restrict* const restrict nbsp);

/**
 *
 * @param[out] nbsp          NBS presentation-layer
 * @param[in]  nbsa          NBS application-layer
 * @retval NBS_STATUS_INVAL  `nbsp == NULL || nbsa == NULL`. log_add() called.
 * @retval NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbsp_set_application_layer(
        nbsp_t* const restrict nbsp,
        nbsa_t* const restrict nbsa);

/**
 *
 * @param[out] nbsp          NBS presentation-layer
 * @param[in]  nbsa          NBS application-layer
 * @retval NBS_STATUS_INVAL  `nbsp == NULL || nbsa == NULL`. log_add() called.
 * @retval NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbsp_set_transport_layer(
        nbsp_t* const restrict nbsp,
        nbst_t* const restrict nbst);

/**
 * Ends the current product. Does nothing if there's no current product.
 * Idempotent.
 */
nbs_status_t nbsp_recv_end(
        nbsp_t* const nbsp);

nbs_status_t nbsp_recv_gini_start(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                rec_len,
        const unsigned                recs_per_block,
        const bool                    is_compressed,
        const int                     prod_transfer_type,
        size_t                        size_estimate);

nbs_status_t nbsp_recv_gini_block(
        nbsp_t* const restrict  nbsp,
        const uint8_t* restrict buf,
        const unsigned          nbytes,
        const unsigned          block_num,
        const bool              is_compressed);

/**
 * Transfers a GINI image from the application-layer to the transport-layer.
 *
 * @param[in] nbsp  NBS presentation-layer
 * @param[in] gini  GINI image
 * @retval 0        Success
 */
nbs_status_t nbsp_send_gini(
        nbsp_t* const restrict       nbsp,
        const gini_t* const restrict gini);

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
