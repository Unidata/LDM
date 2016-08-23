/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: frame_queue.c
 * @author: Steven R. Emmerson
 *
 * This file declares the API for a queue of frames.
 */

#ifndef NOAAPORT_FRAME_QUEUE_C_
#define NOAAPORT_FRAME_QUEUE_C_

#include <time.h>
#include <stddef.h>
#include <stdint.h>

typedef struct frame_queue fq_t;

typedef struct {
    struct timespec first_release;  ///< First time fq_release() was called
    struct timespec last_release;   ///< Last time fq_release() was called
    uint_least64_t  total_bytes;    ///< Total number of data-bytes seen
    /*
     * The unbiased estimate of the variance of frame sizes can be computed from
     * the following three members via the formula
     *   var = (sum_sqr_dev-(sum_dev*sum_dev)/total_frames)/(total_frames-1)
     */
    uint_least64_t  total_frames;   ///< Total number of frames seen
    double          sum_dev;        ///< Sum of frame-size deviations from first
                                    ///< frame
    double          sum_sqr_dev;    ///< Sum of square of frame-size deviations
                                    ///< from first frame
    size_t          capacity;       ///< Queue capacity in bytes
    size_t          max_bytes;      ///< Largest number of data-bytes held
    unsigned        first_frame;    ///< Size of first frame seen in bytes
    unsigned        smallest_frame; ///< Size of smallest frame seen in bytes
    unsigned        largest_frame;  ///< Size of largest frame seen in bytes
} fq_stats_t;

typedef enum {
    FQ_STATUS_INVAL = 1,
    FQ_STATUS_TOO_BIG,
    FQ_STATUS_SYSTEM,
    FQ_STATUS_UNRESERVED,
    FQ_STATUS_AGAIN,
    FQ_STATUS_SHUTDOWN
} fq_status_t;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Returns a new frame queue.
 *
 * @param[out] fq           New frame queue.
 * @param[in]  size         Amount of data the queue should hold in bytes.
 * @retval     0            Success. `*fq` is set. Call `fq_free(*fq)` when it's
 *                          no longer needed.
 * @retval FQ_STATUS_INVAL  `fq == NULL || size == 0`. log_add() called.
 * @retval FQ_STATUS_SYSTEM Out of memory. `log_add()` called.
 */
fq_status_t fq_new(
        fq_t** const fq,
        const size_t size);

/**
 * Reserves space for a frame. Blocks until space is available.
 *
 * @param[in]  fq                 Frame queue
 * @param[out] data               Pointer to reserved space
 * @param[in]  nbytes             Space to reserve in bytes. If `0`, then the
 *                                return is immediate and nothing is reserved.
 * @retval     0                  Success. `*data` is set; will be NULL if
 *                                `nbytes == 0`. Call fq_release() after writing
 *                                the data.
 * @retval     FQ_STATUS_INVAL    `fq == NULL || data == NULL`
 * @retval     FQ_STATUS_TOO_BIG  `nbytes` is greater than the queue capacity
 */
fq_status_t fq_reserve(
        fq_t* const restrict              fq,
        uint8_t* restrict* const restrict data,
        const unsigned                    nbytes);

/**
 * Attempts to reserve space for a frame. Doesn't block.
 *
 * @param[in]  fq                 Frame queue
 * @param[out] data               Pointer to reserved space
 * @param[in]  nbytes             Space to reserve in bytes. If `0`, then the
 *                                return is immediate and nothing is reserved.
 * @retval     0                  Success. `*data` is set; will be NULL if
 *                                `nbytes == 0`. Call fq_release() after writing
 *                                the data.
 * @retval     FQ_STATUS_INVAL    `fq == NULL || data == NULL`
 * @retval     FQ_STATUS_TOO_BIG  `nbytes` is greater than the queue capacity
 * @retval     FQ_STATUS_NO_SPACE No space available for the frame
 */
fq_status_t fq_try_reserve(
        fq_t* const restrict              fq,
        uint8_t* restrict* const restrict data,
        const unsigned                    nbytes);

/**
 * Releases the space reserved by a prior call to fq_reserve().
 *
 * @param[in] fq                    Frame queue
 * @param[in] nbytes                Number of bytes actually written. `0`
 *                                  cancels the operation -- even if the
 *                                  reserved space was `0`.
 * @retval    0                     Success
 * @retval    FQ_STATUS_UNRESERVED  `nbytes` is greater than the `nbytes` given
 *                                  to fq_reserve(). If that much data was
 *                                  actually written into the queue, then the
 *                                  queue has likely been corrupted.
 */
fq_status_t fq_release(
        fq_t* const restrict fq,
        const unsigned       nbytes);

/***
 * Returns (but does not remove) the frame at the head of the frame queue.
 * Blocks if the queue is empty.
 *
 * @param[in]  fq                  Frame queue
 * @param[out] data                Data in the frame
 * @param[out] nbytes              Amount of data in the frame in bytes. Will
 *                                 not be `0`.
 * @retval     0                   Success. `*data` and `*nbytes` are set.
 * @retval     FQ_STATUS_SHUTDOWN  Queue is empty and shut down for input
 */
fq_status_t fq_peek(
        fq_t* const restrict     fq,
        uint8_t** const restrict data,
        unsigned* restrict       nbytes);

/***
 * Removes the frame at the head of the frame queue, which must have been
 * returned by a prior call to fq_peek().
 *
 * @param[in]  fq      Frame queue
 * @retval     0       Success
 */
fq_status_t fq_remove(
        fq_t* const fq);

/**
 * Shuts down a frame queue for input.
 *
 * @param[in] fq               Frame queue to be shut down
 * @retval    0                Success. fq_peek() will now return
 *                             FQ_STATUS_SHUTDOWN when the queue is empty.
 * @retval    FQ_STATUS_INVAL  `fq == NULL`
 */
fq_status_t fq_shutdown(
        fq_t* const fq);

/**
 * Returns statistics for a frame-queue.
 *
 * @param[in,out]  fq       Frame-queue
 * @param[out]     stats    Returned statistics
 * @retval 0                Success. `*status` is set.
 * @retval FQ_STATUS_INVAL  `fq == NULL || status == NULL`. log_add() called.
 */
fq_status_t fq_get_stats(
        fq_t* const restrict fq,
        fq_stats_t* const restrict stats);

/**
 * Frees a frame queue.
 *
 * @param[in] fq  Frame queue to be freed or NULL.
 */
void fq_free(
        fq_t* const fq);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_FRAME_QUEUE_C_ */
