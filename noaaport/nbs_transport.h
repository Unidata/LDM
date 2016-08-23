/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_transport.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the transport layer of the NOAAPort Broadcast
 * System (NBS).
 */

#ifndef NOAAPORT_NBS_TRANSPORT_H_
#define NOAAPORT_NBS_TRANSPORT_H_

typedef struct nbst nbst_t;

#include "nbs.h"
#include "nbs_presentation.h"
#include "nbs_link.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
    extern "C" {
#endif

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
        nbst_t** const restrict nbst);

/**
 * Sets the associated NBS link-layer object of an NBS transport-layer object.
 *
 * @param[out] nbst               NBS transport-layer object
 * @param[in]  nbsl               NBS link-layer object
 * @retval     0                  Success
 * @retval     NBS_STATUS_INVAL  `nbst == NULL || nbsl == NULL`. log_add()
 *                                called.
 */
nbs_status_t nbst_set_link_layer(
        nbst_t* const restrict nbst,
        nbsl_t* const restrict nbsl);

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
        nbsp_t* const restrict nbsp);

/**
 * Frees the resources associated with an NBS transport-layer object. Does _not_
 * free the associated frame-queue or NBS presentation-layer object.
 *
 * @param[in]  nbst  NBS transport-layer object or `NULL`.
 */
void nbst_free(
        nbst_t* const nbst);

nbs_status_t nbst_recv(
        nbst_t* const restrict  nbst,
        const uint8_t* restrict buf,
        unsigned                nbytes);

/**
 * Returns the space for a frame to be processed towards the presentation-layer.
 *
 * @param[in,out] nbst       NBS transport-layer object
 * @param[out]    frame      Frame buffer
 * @param[out]    nbytes     Number of bytes in `frame
 * @retval 0                 Success. `*frame` and `*nbytes` are set.
 * @retval NBS_STATUS_INVAL  `nbst == NULL || frame == NULL || nbytes == NULL`.
 *                           log_add() called.
 */
nbs_status_t nbst_get_recv_frame_buf(
        nbst_t* const restrict            nbst,
        uint8_t* restrict* const restrict frame,
        unsigned* const restrict          nbytes);

nbs_status_t nbst_recv_end(
        nbst_t* const nbst);

void nbst_send_start(
    nbst_t* const restrict nbst,
    const unsigned         recs_per_block,
    const unsigned         bytes_per_record,
    const int              prod_type,
    const int              num_blocks,
    const bool             is_compressed);

nbs_status_t nbst_send_block(
        nbst_t* const restrict        nbst,
        const uint8_t* const restrict block,
        const unsigned                nbytes);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_TRANSPORT_H_ */
