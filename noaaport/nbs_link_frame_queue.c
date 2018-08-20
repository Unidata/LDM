/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_link.c
 * @author: Steven R. Emmerson
 *
 * This file implements the link-layer of the NOAAPort Broadcast System (NBS).
 * This layer transfers NBS frames between a transport-layer and a frame-queue.
 */

#include <nbs_link_frame_queue.h>
#include "config.h"

#include "frame_queue.h"
#include "log.h"

#include <stdint.h>
#include <unistd.h>

// NBS link-layer structure:
struct nbsl {
    fq_t*   fq_up;   ///< Frame queue for up-layer processing
    fq_t*   fq_down; ///< Frame queue for down-layer processing
    nbst_t* nbst;    ///< NBS transport-layer object
};

/**
 * Transfers a frame from the frame-queue to the transport-layer.
 *
 * @param[in] nbsl            NBS link-layer object
 * @retval 0                  Success
 * @retval NBS_STATUS_END     Input is shut down
 * @retval NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval NBS_STATUS_INVAL   Frame queue can't handle frame size. log_add()
 *                            called.
 * @retval NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
nbs_status_t nbsl_recv(
        nbsl_t* const nbsl)
{
    uint8_t* buf;
    unsigned nbytes;
    int      status = fq_peek(nbsl->fq_up, &buf, &nbytes);
    if (status) {
        status = NBS_STATUS_END;
    }
    else {
        status = nbst_recv(nbsl->nbst, buf, nbytes);
        if (status == NBS_STATUS_INVAL || status == NBS_STATUS_UNSUPP) {
            log_debug("Discarding frame");
            status = 0;
        }
        (void)fq_remove(nbsl->fq_up);
    }
    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new NBS link-layer object.
 *
 * @param[out] nbsl           Returned NBS link-layer object
 * @retval 0                  Success. `*nbsl` is set.
 * @retval NBS_STATUS_INVAL  `nbsl == NULL`. log_add() called.
 * @retval NBS_STATUS_NOMEM   Out of memory. log_add() called.
 */
nbs_status_t nbsl_new(
        nbsl_t** const restrict nbsl)
{
    int status;
    if (nbsl == NULL) {
        log_add("Invalid argument: nbsl=%p", nbsl);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsl_t* obj = log_malloc(sizeof(nbsl_t), "NBS link-layer object");
        if (obj == NULL) {
            status = NBS_STATUS_NOMEM;
        }
        else {
            obj->nbst = NULL;
            obj->fq_up = NULL;
            obj->fq_down = NULL;
            *nbsl = obj;
        }
    }
    return status;
}

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
        nbst_t* const restrict nbst)
{
    int status;
    if (nbsl == NULL || nbst == NULL) {
        log_add("Invalid argument: nbsl=%p, nbst=%p", nbsl, nbst);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsl->nbst = nbst;
        status = 0;
    }
    return status;
}

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
        fq_t* const restrict   fq)
{
    int status;
    if (nbsl == NULL || fq == NULL) {
        log_add("Invalid argument: nbsl=%p, fq=%p", nbsl, fq);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsl->fq_up = fq;
    }
    return status;
}

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
        fq_t* const restrict   fq)
{
    int status;
    if (nbsl == NULL || fq == NULL) {
        log_add("Invalid argument: nbsl=%p, fq=%p", nbsl, fq);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsl->fq_down = fq;
    }
    return status;
}

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
        nbsl_t* const nbsl)
{
    int status;
    if (nbsl == NULL) {
        log_add("Invalid argument: nbsl=%p", nbsl);
        status = NBS_STATUS_INVAL;
    }
    do {
        status = nbsl_recv(nbsl);
    } while (status == 0);
    nbst_recv_end(nbsl->nbst);
    return status == NBS_STATUS_END ? 0 : status;
}

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
        const unsigned                nbytes)
{
    int status;
    if (frame == NULL) {
        log_add("Invalid argument: frame=%p");
        status = NBS_STATUS_INVAL;
    }
    else {
        uint8_t* reserve;
        status = fq_reserve(nbsl->fq_down, &reserve, nbytes);
        if (status) {
            status = NBS_STATUS_INVAL;
        }
        else {
            (void)memcpy(reserve, frame, nbytes);
            (void)fq_release(nbsl->fq_down, nbytes);
        }
    }
    return status;
}

/**
 * Frees an NBS link-layer object.
 * 
 * @param[in] nbsl  NBS link-layer object to be freed or `NULL`.
 */
void nbsl_free(
        nbsl_t* const nbsl)
{
    free(nbsl);
}
