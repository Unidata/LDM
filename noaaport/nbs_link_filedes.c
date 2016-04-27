/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_link.c
 * @author: Steven R. Emmerson
 *
 * This file implements the link-layer of the NOAAPort Broadcast System (NBS).
 * This layer transfers NBS frames between a transport-layer and a file
 * descriptor. An instance may be used to send frames or to received frames
 * -- but not both.
 */

#include "config.h"

#include "log.h"
#include "timestamp.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <nbs_link.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/uio.h>
#include <unistd.h>

// NBS link-layer structure:
struct nbsl {
    nbsl_stats_t stats;      ///< Statistics
    int          fd_recv;    ///< File descriptor for receiving products
    int          fd_send;    ///< File descriptor for sending products
    uint8_t*     frame_buf;  ///< Buffer for receiving frames
    unsigned     frame_size; ///< Capacity of `frame` in bytes
    nbst_t*      nbst;       ///< NBS transport-layer object
};

/**
 * Initializes an NBS link-layer statistics object.
 *
 * @param[in,out] stats  Statistics object
 */
static void stats_init(
        nbsl_stats_t* const stats)
{
    stats->total_bytes = 0;
    stats->total_frames = 0;
    stats->largest_frame = 0;
    stats->smallest_frame = ~stats->largest_frame;
    stats->sum_dev = 0;
    stats->sum_sqr_dev = 0;
}

/**
 * Handles a successful I/O operation on the file descriptor of an NBS
 * link-layer object.
 *
 * @param[in,out] stats   Statistics object
 * @param[in]     nbytes  Number of bytes read
 */
static void stats_io_returned(
        nbsl_stats_t* const stats,
        const unsigned      nbytes)
{
    (void)clock_gettime(CLOCK_REALTIME, &stats->last_io);
    if (stats->total_frames++ == 0) {
        stats->first_frame = nbytes;
        stats->first_io = stats->last_io;
    }
    stats->total_bytes += nbytes;
    if (nbytes > stats->largest_frame)
        stats->largest_frame = nbytes;
    if (nbytes < stats->smallest_frame)
        stats->smallest_frame = nbytes;
    unsigned long dev = nbytes - stats->first_frame;
    stats->sum_dev += dev;
    stats->sum_sqr_dev += dev*dev;
}

/**
 * Receives an NBS frame from the file-descriptor of an NBS link-layer object
 * and transfers it to the associated transport-layer object.
 *
 * @pre                       `nbsl->fd_recv >= 0 && nbsl->nbst != NULL`
 * @param[in] nbsl            NBS link-layer object
 * @retval 0                  Success
 * @retval NBS_STATUS_END     Input is shut down
 * @retval NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
nbs_status_t nbsl_recv(
        nbsl_t* const nbsl)
{
    int     status;
    ssize_t nbytes = read(nbsl->fd_recv, nbsl->frame_buf, nbsl->frame_size);
    if (nbytes == 0 || (nbytes < 0 && errno == EBADF)) {
        status = NBS_STATUS_END;
    }
    else if (nbytes < 0) {
        log_add_syserr("Couldn't read frame");
        status = NBS_STATUS_SYSTEM;
    }
    else {
        stats_io_returned(&nbsl->stats, nbytes);
        static unsigned long iframe;
        log_debug("Read %zd-byte frame %lu", nbytes, iframe++);
        status = nbst_recv(nbsl->nbst, nbsl->frame_buf, nbytes);
        if (status == NBS_STATUS_INVAL || status == NBS_STATUS_UNSUPP ||
                status == NBS_STATUS_NOSTART) {
            log_notice("Discarding frame");
            status = 0;
        }
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
            stats_init(&obj->stats);
            obj->nbst = NULL;
            obj->fd_recv = -1;
            obj->fd_send = -1;
            obj->frame_buf = NULL;
            obj->frame_size = 0;
            *nbsl = obj;
            status = 0;
        }
    }
    return status;
}

/**
 * Sets the NBS transport-layer object for forwarding received frames to.
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
        (void)nbst_get_recv_frame_buf(nbst, &nbsl->frame_buf, &nbsl->frame_size);
        if (nbsl->frame_buf == NULL || nbsl->frame_size == 0) {
            log_add("Invalid frame buffer: frame_buf=%p, frame_size=%u",
                    nbsl->frame_buf, nbsl->frame_size);
            status = NBS_STATUS_INVAL;
        }
        else {
            nbsl->nbst = nbst;
            status = 0;
        }
    }
    return status;
}

/**
 * Sets the file-descriptor on which an NBS link-layer object receives products.
 *
 * @param[out] nbsl          NBS link-layer object
 * @param[in]  fd            File-descriptor. Every successful read() on it must
 *                           return exactly one frame. `socketpair()` can be
 *                           used when necessary.
 * @retval 0                 Success
 * @retval NBS_STATUS_INVAL  `nbsl = NULL || fd < 0`. log_add() called.
 */
nbs_status_t nbsl_set_recv_file_descriptor(
        nbsl_t* const restrict nbsl,
        const int              fd)
{
    int status;
    if (nbsl == NULL || fd < 0) {
        log_add("Invalid argument: nbsl=%p, fd=%d", nbsl, fd);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsl->fd_recv = fd;
        status = 0;
    }
    return status;
}

/**
 * Sets the file-descriptor for sending NBS frames.
 *
 * @param[out] nbsl          NBS link-layer object
 * @param[in]  fd            File-descriptor
 * @retval 0                 Success
 * @retval NBS_STATUS_INVAL  `nbsl = NULL || fd < 0`. log_add() called.
 */
nbs_status_t nbsl_set_send_file_descriptor(
        nbsl_t* const restrict nbsl,
        const int              fd)
{
    int status;
    if (nbsl == NULL || fd < 0) {
        log_add("Invalid argument: nbsl=%p, fd=%d", nbsl, fd);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsl->fd_send = fd;
        status = 0;
    }
    return status;
}

/**
 * Transfers NBS frames from the input to the NBS transport-layer. Doesn't
 * return unless the input or output is shut down or an unrecoverable error
 * occurs.
 *
 * @param[in] nbsl             NBS link-layer object
 * @retval 0                   Input was shut down
 * @retval NBS_STATUS_LOGIC    Logic error. log_add() called.
 * @retval NBS_STATUS_INVAL   `nbsl == NULL`. log_add() called.
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
    else if (nbsl->fd_recv < 0) {
        log_add("nbsl_set_recv_file_descriptor() not called");
        status = NBS_STATUS_LOGIC;
    }
    else if (nbsl->nbst == NULL) {
        log_add("nbsl_set_transport_layer() not called");
        status = NBS_STATUS_LOGIC;
    }
    else {
        do {
            status = nbsl_recv(nbsl);
        } while (status == 0);
        nbst_recv_end(nbsl->nbst);
    }
    return status == NBS_STATUS_END ? 0 : status;
}

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
        const int                          iocnt)
{
    ssize_t status;
    if (nbsl == NULL || iovec == NULL || iocnt <= 0) {
        log_add("Invalid argument: nbsl=%p, iovec=%p, iocnt=%d", nbsl, iovec,
                iocnt);
        status = NBS_STATUS_INVAL;
    }
    else if (nbsl->fd_send < 0) {
        log_add("Sending file-descriptor not set");
        status = NBS_STATUS_LOGIC;
    }
    else {
        ssize_t nbytes = 0;
        static unsigned long iframe = 0;
        for (int i = 0; i < iocnt; i++)
            nbytes += iovec[i].iov_len;
        log_debug("Writing %zd-byte frame %lu", nbytes, iframe++);
        status = writev(nbsl->fd_send, iovec, iocnt);
        if (status == nbytes) {
            stats_io_returned(&nbsl->stats, nbytes);
            status = 0;
        }
        else {
            if (status < 0) {
                log_add_syserr("Couldn't write %zd-byte frame to output",
                        nbytes);
            }
            else {
                log_add("Could only write %zd bytes of %zd-byte frame to output",
                        status, nbytes);
            }
            status = NBS_STATUS_SYSTEM;
        }
    }
    return status;
}

/**
 * Returns statistics.
 *
 * @param[in]  nbsl   NBS link-layer object
 * @param[out] stats  Statistics
 */
void nbsl_get_stats(
        const nbsl_t* const restrict nbsl,
        nbsl_stats_t* const restrict stats)
{
    *stats = nbsl->stats;
}

/**
 * Logs statistics via the `log` module at a given level.
 *
 * @param[in] nbsl   NBS link-layer
 * @param[in] level  Logging level
 */
void nbsl_log_stats(
        const nbsl_t* const nbsl,
        const log_level_t   level)
{
    nbsl_stats_t stats;
    char         msg[512];

    stats = nbsl->stats;
    if (stats.total_frames == 0) {
        // Format no-observation statistics
        snprintf(msg, sizeof(msg),
                "Link-Layer Statistics:\n"
                "    Times:\n"
                "        First I/O: N/A\n"
                "        Last I/O:  N/A\n"
                "        Duration:  N/A\n"
                "    Frames:\n"
                "        Count:     0\n"
                "        Sizes in Bytes:\n"
                "            Smallest: N/A\n"
                "            Mean:     N/A\n"
                "            Largest:  N/A\n"
                "            S.D.:     N/A\n"
                "        Rate:      N/A\n"
                "    Bytes:\n"
                "        Count:     0\n"
                "        Rate:      N/A");
        msg[sizeof(msg)-1] = 0;
    }
    else {
        struct timeval first;
        (void)timeval_init_from_timespec(&first, &stats.first_io);
        struct timeval duration;
        char           first_string[TIMEVAL_FORMAT_TIME];
        timeval_format_time(first_string, &first);
        char           duration_string[TIMEVAL_FORMAT_DURATION];

        if (stats.total_frames == 1) {
            // Format single-observation statistics
            (void)timeval_init_from_difference(&duration, &first, &first);
            snprintf(msg, sizeof(msg),
                    "Link-Layer Statistics:\n"
                    "    Times:\n"
                    "        First I/O: %s\n"
                    "        Last I/O:  %s\n"
                    "        Duration:  %s\n"
                    "    Frames:\n"
                    "        Count:     1\n"
                    "        Sizes in Bytes:\n"
                    "            Smallest: %5u\n"
                    "            Mean:     %5u.1\n"
                    "            Largest:  %5u\n"
                    "            S.D.:     N/A\n"
                    "        Rate:      N/A\n"
                    "    Bytes:\n"
                    "        Count:     %"PRIuLEAST64"\n"
                    "        Rate:      N/A",
                    first_string,
                    first_string,
                    timeval_format_duration(duration_string, &duration),
                    stats.smallest_frame,
                    stats.smallest_frame,
                    stats.largest_frame,
                    stats.total_bytes);
            msg[sizeof(msg)-1] = 0;
        }
        else {
            // Format multiple-observation statistics
            struct timeval last;
            (void)timeval_init_from_timespec(&last, &stats.last_io);
            (void)timeval_init_from_difference(&duration, &last, &first);
            char           last_string[TIMEVAL_FORMAT_TIME];
            double         mean_frame_size = (double)stats.total_bytes /
                    stats.total_frames;
            double         variance_frame_size = (stats.sum_sqr_dev -
                    (stats.sum_dev*stats.sum_dev)/stats.total_frames) /
                            (stats.total_frames-1);
            double         stddev_frame_size = sqrt(variance_frame_size);
            double         stddev_mean_frame_size = sqrt(variance_frame_size /
                    stats.total_frames);
            double         seconds_duration = timeval_as_seconds(&duration);
            (void)snprintf(msg, sizeof(msg),
                    "Link-Layer Statistics:\n"
                    "    Times:\n"
                    "        First I/O: %s\n"
                    "        Last I/O:  %s\n"
                    "        Duration:  %s\n"
                    "    Frames:\n"
                    "        Count:     %"PRIuLEAST64"\n"
                    "        Sizes in Bytes:\n"
                    "            Smallest: %5u\n"
                    "            Mean:     %7.1f(%.1f)\n"
                    "            Largest:  %5u\n"
                    "            S.D.:     %7.1f\n"
                    "        Rate:      %g/s\n"
                    "    Bytes:\n"
                    "        Count:     %"PRIuLEAST64"\n"
                    "        Rate:      %g/s",
                    first_string,
                    timeval_format_time(last_string, &last),
                    timeval_format_duration(duration_string, &duration),
                    stats.total_frames,
                    stats.smallest_frame,
                    mean_frame_size,
                    stddev_mean_frame_size,
                    stats.largest_frame,
                    stddev_frame_size,
                    stats.total_frames / seconds_duration,
                    stats.total_bytes,
                    stats.total_bytes / seconds_duration);
            msg[sizeof(msg)-1] = 0;
        }
    }

    log_log(level, msg);
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
