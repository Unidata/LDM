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

#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>
#include <sys/time.h>

typedef struct {
    struct timeval first_io;       ///< Time first I/O returned
    struct timeval last_io;        ///< Time last I/O returned
    uint_least64_t total_bytes;    ///< Total number of data-bytes seen
    /*
     * The unbiased estimate of the variance of frame sizes can be computed from
     * the following three members via the formula
     *     var = (sum_sqr_dev-(sum_dev*sum_dev)/total_frames)/(total_frames-1)
     * Obviously, this is valid only if `total_frames > 1`.
     */
    uint_least64_t  total_frames;   ///< Total number of frames seen
    double          sum_dev;        ///< Sum of frame-size deviations from first
                                    ///< frame
    double          sum_sqr_dev;    ///< Sum of square of frame-size deviations
                                    ///< from first frame
    unsigned        first_frame;    ///< Size of first frame seen in bytes
    unsigned        smallest_frame; ///< Size of smallest frame seen in bytes
    unsigned        largest_frame;  ///< Size of largest frame seen in bytes
} nbsl_stats_t;

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

/**
 * Returns statistics.
 *
 * @param[in]  nbsl   NBS link-layer object
 * @param[out] stats  Statistics
 */
void nbsl_get_stats(
        const nbsl_t* const restrict nbsl,
        nbsl_stats_t* const restrict stats);

/**
 * Logs statistics via the `log` module at level `INFO`.
 *
 * @param[in] nbsl   NBS link-layer
 * @param[in] level  Level at which to log
 */
void nbsl_log_stats(
        const nbsl_t* const nbsl,
        const log_level_t   level);

void nbsl_free(
        nbsl_t* nbsl);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_LINK_FILEDES_H */
