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

#ifndef NOAAPORT_NBS_LINK_FILEDES_H
#define NOAAPORT_NBS_LINK_FILEDES_H

typedef struct nbsl nbsl_t;

#include "nbs.h"
#include "nbs_transport.h"

#include <sys/uio.h>

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
 * Sets the file-descriptor for upward processing (i.e., towards the transport
 * layer) by an NBS link-layer object.
 *
 * @param[out] nbsl          NBS link-layer object
 * @param[in]  fd            File-descriptor. A read() must return no more than
 *                           a single frame.
 * @retval 0                 Success
 * @retval NBS_STATUS_INVAL  `nbsl = NULL || fd < 0`. log_add() called.
 */
nbs_status_t nbsl_set_recv_file_descriptor(
        nbsl_t* const restrict nbsl,
        const int              fd);

/**
 * Sets the file-descriptor for downward processing (i.e., towards the data-link
 * layer) by an NBS link-layer object.
 *
 * @param[out] nbsl          NBS link-layer object
 * @param[in]  fd            File-descriptor
 * @retval 0                 Success
 * @retval NBS_STATUS_INVAL  `nbsl = NULL || fd < 0`. log_add() called.
 */
nbs_status_t nbsl_set_send_file_descriptor(
        nbsl_t* const restrict nbsl,
        const int              fd);

nbs_status_t nbsl_execute(
        nbsl_t* const nbsl);

/**
 * Sends an NBS frame.
 *
 * @param[in] nbsl            NBS link-layer object
 * @param[in] iovec           Gather-I/O-vector
 * @param[in] iocnt           Number of elements in gather-I/O-vector
 * @retval 0                  Success
 * @retval NBS_STATUS_INVAL   `nbsl == NULL || iovec == NULL || iocnt <= 0`.
 *                            log_add() called.
 * @retval NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
nbs_status_t nbsl_send(
        nbsl_t* const restrict             nbsl,
        const struct iovec* const restrict iovec,
        const int                          iocnt);

void nbsl_free(
        nbsl_t* nbsl);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_LINK_FILEDES_H */
