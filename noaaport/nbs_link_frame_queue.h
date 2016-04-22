/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_link.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the link-layer of the NOAAPort Broadcast
 * System (NBS).
 */

#ifndef NOAAPORT_NBS_LINK_FRAME_QUEUE_H_
#define NOAAPORT_NBS_LINK_FRAME_QUEUE_H_

typedef struct nbsl nbsl_t;

#include "frame_queue.h"
#include "nbs_status.h"
#include "nbs_transport.h"

#ifdef __cplusplus
    extern "C" {
#endif

nbs_status_t nbsl_new(
        nbsl_t** const restrict nbsl);

/**
 * Sets the NBS transport-layer object for upward processing by an NBS
 * link-layer object.
 *
 * @param[out] nbsl          NBS link-layer object
 * @param[in]  nbst          NBS transport-layer object
 * @retval 0                 Success
 * @retval NBS_STATUS_INVAL  `nbsl = NULL || nbst = NULL`. log_add() called.
 */
nbs_status_t nbsl_set_transport_layer(
        nbsl_t* const restrict nbsl,
        nbst_t* const restrict nbst);

/**
 * Sets the frame-queue for upward processing (i.e., towards the transport
 * layer) by an NBS link-layer object.
 *
 * @param[out] nbsl          NBS link-layer object
 * @param[in]  fq            Frame-queue
 * @retval 0                 Success
 * @retval NBS_STATUS_INVAL  `nbsl = NULL || fq = NULL`. log_add() called.
 */
nbs_status_t nbsl_set_up_frame_queue(
        nbsl_t* const restrict nbsl,
        fq_t* const restrict   fq);

/**
 * Sets the frame-queue for downward processing (i.e., towards the data-link
 * layer) by an NBS link-layer object.
 *
 * @param[out] nbsl          NBS link-layer object
 * @param[in]  fq            Frame-queue
 * @retval 0                 Success
 * @retval NBS_STATUS_INVAL  `nbsl = NULL || fq = NULL`. log_add() called.
 */
nbs_status_t nbsl_set_down_frame_queue(
        nbsl_t* const restrict nbsl,
        fq_t* const restrict   fq);

/**
 * Transfers NBS frames from the frame-queue to the NBS transport-layer. Doesn't
 * return unless the input or output is shut down or an unrecoverable error
 * occurs.
 *
 * @param[in] nbsl             NBS link-layer object
 * @retval 0                   Input was shut down
 * @retval NBS_STATUS_LOGIC    Logic error. log_add() called.
 * @retval NBS_STATUS_INVAL   `nbsl == NULL` or frame queue can't handle frame
 *                             size. log_add() called.
 * @retval NBS_STATUS_SYSTEM   System failure. log_add() called.
 */
nbs_status_t nbsl_execute(
        nbsl_t* const nbsl);

/**
 * Transfers a frame from the NSB transport-layer to the frame-queue. Used for
 * testing.
 *
 * @param[in] nbsl           NBS link-layer object
 * @param[in] frame          Frame to write
 * @param[in] nbytes         Number of bytes in `frame`
 * @retval 0                 Success
 * @retval NBS_STATUS_INVAL  `frame == NULL` or frame queue can't handle frame
 *                           size. log_add() called.
 */
nbs_status_t nbsl_send(
        nbsl_t* const restrict        nbsl,
        const uint8_t* const restrict frame,
        const unsigned                nbytes);

void nbsl_free(
        nbsl_t* nbsl);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_LINK_FRAME_QUEUE_H_ */
