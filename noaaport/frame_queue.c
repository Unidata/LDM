/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: frame_queue.c
 * @author: Steven R. Emmerson
 *
 * This file implements the API for a queue of frames. A frame is just a
 * contiguous array of bytes. Frames of zero bytes are ignored.
 *
 * This module supports a concurrent write to the queue with a concurrent read
 * from the queue. Multiple concurrent writes or reads, however, are not
 * supported.
 */

#include "config.h"

#include "frame_queue.h"
#include "ldm.h"
#include "log.h"

#include <pthread.h>
#include <stdbool.h>

typedef struct frame {
    struct frame* next;    ///< Pointer to next frame
    unsigned      size;    ///< Amount of frame data in bytes
    uint8_t       data[1]; ///< Frame data
} frame_t;

struct frame_queue {
    frame_t*        frames;   ///< Frame buffer
    frame_t*        write;    ///< Next frame to write
    frame_t*        read;     ///< Next frame to read
    frame_t*        out;      ///< One frame beyond the end of the queue
    size_t          max_data; ///< Maximum possible number of data bytes
    pthread_mutex_t mutex;    ///< Mutual exclusion lock
    pthread_cond_t  cond;     ///< Conditional variable
    uint16_t        reserved; ///< Reserved space in bytes
    bool            shutdown; ///< Whether or not the queue is shut down for input
};

#define FQ_MIN_ELT(total, size)   (((total) + ((size) - 1)) / (size))
#define FQ_ROUND_UP(total, size)  (FQ_MIN_ELT(total, size) * (size))

/**
 * Initializes a mutex to have attributes PTHREAD_MUTEX_ERRORCHECK and
 * PTHREAD_PRIO_INHERIT.
 *
 * @param[in] mutex             Mutex to be initialized
 * @retval    0                 Success
 * @retval    FQ_STATUS_SYSTEM  System error. log_add() called.
 */
static fq_status_t init_mutex(
        pthread_mutex_t* const mutex)
{
    log_assert(mutex != NULL);
    pthread_mutexattr_t attr;
    int                 status = pthread_mutexattr_init(&attr);
    if (status) {
        log_errno(status, "Couldn't initialize mutex attributes");
        status = FQ_STATUS_SYSTEM;
    }
    else {
        (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        (void)pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
        status = pthread_mutex_init(mutex, &attr);
        if (status) {
            log_errno(status, "Couldn't initialize mutex");
            status = FQ_STATUS_SYSTEM;
        }
        (void)pthread_mutexattr_destroy(&attr);
    }
    return status;
}

/**
 * Locks a frame queue.
 *
 * @param[in] fq  Frame queue
 */
static void lock(
        fq_t* const fq)
{
    int status = pthread_mutex_lock(&fq->mutex);
    log_assert(status == 0);
}

/**
 * Unlocks a frame queue.
 *
 * @param[in] fq  Frame queue
 */
static void unlock(
        fq_t* const fq)
{
    int status = pthread_mutex_unlock(&fq->mutex);
    log_assert(status == 0);
}

/**
 * Initializes the lock of a frame queue.
 *
 * @param[in] fq                 Frame queue
 * @retval     0                 Success.
 * @retval     FQ_STATUS_SYSTEM  System error occurred. log_add() called.
 */
static fq_status_t lock_init(
        fq_t* const fq)
{
    log_assert(fq != NULL);
    int status = init_mutex(&fq->mutex);
    if (status == 0) {
        status = pthread_cond_init(&fq->cond, NULL);
        if (status) {
            log_errno(status, "Couldn't initialize condition variable");
            (void)pthread_mutex_destroy(&fq->mutex);
            status = FQ_STATUS_SYSTEM;
        }
    }
    return status;
}

/**
 * Finalizes the lock of a frame queue.
 *
 * @param[in] fq  Frame queue
 */
static void lock_fini(
        fq_t* const fq)
{
    log_assert(fq != NULL);
    (void)pthread_mutex_destroy(&fq->mutex);
    (void)pthread_cond_destroy(&fq->cond);
}

/**
 * Initializes a frame queue.
 *
 * @param[in] fq                Frame queue to be initialized
 * @param[in] size              Amount of frame data the queue should hold in
 *                              bytes
 * @retval    0                 Success. `*fq` is initialized.
 * @retval    FQ_STATUS_SYSTEM  System error occurred. log_add() called.
 */
static fq_status_t init(
        fq_t* const  fq,
        const size_t size)
{
    log_assert(fq != NULL);
    int      status;
    /*
     * The total space allocated for the queue must be an integral multiple
     * of `sizeof(frame_t)` for the algorithms to work.
     */
    const unsigned nframes = FQ_MIN_ELT(sizeof(frame_t) - 1 + size, sizeof(frame_t));
    size_t         nbytes = nframes * sizeof(frame_t);
    frame_t*       frames = log_malloc(nbytes, "frame buffer");
    if (frames == NULL) {
        status = FQ_STATUS_SYSTEM;
    }
    else {
        status = lock_init(fq);
        if (status) {
            log_errno(status, "Couldn't initialize reader/writer lock");
            free(frames);
        }
        else {
            frames->size = 0;
            frames->next = NULL;
            fq->frames = frames;
            fq->write = frames;
            fq->read = frames;
            fq->out = frames + nframes;
            fq->reserved = 0;
            fq->max_data = nbytes - sizeof(frame_t) + 1;
            fq->shutdown = false;
            status = 0;
        } // Lock initialized
    } // `frames` allocated
    return status;
}

/**
 * Finalizes a frame queue. Should be called sometime after init() when the
 * queue is no longer needed.
 *
 * @param[in] fq  Frame queue to be finalized
 */
static void fini(
        fq_t* const fq)
{
    log_assert(fq);
    lock_fini(fq);
    free(fq->frames);
}

/**
 * Indicates if a frame is too large to be even theoretically added to the
 * queue.
 *
 * @param[in] fq      Frame queue
 * @param[in] nbytes  Size of the frame in bytes
 * @retval    true    iff the frame is to large to add to even an empty queue
 */
static bool is_too_big(
        fq_t* const    fq,
        const unsigned nbytes)
{
    return nbytes > fq->max_data;
}

/**
 * Indicates if a frame of a given size can be added to the tail of the queue.
 *
 * @pre             The frame queue is locked
 * @param[in] fq    Frame queue
 * @retval    true  iff a frame of the given size can be added to the tail of
 *                  the queue.
 */
static bool is_writable(
        fq_t* const    fq,
        const unsigned nbytes)
{
    if (fq->write < fq->read) {
        // Can only write to read position
        return ((uint8_t*)fq->read - fq->write->data) >= nbytes;
    }

    if (fq->write == fq->read && fq->read->size) {
        // There's a frame here that hasn't yet been read
        return false;
    }

    // Can write to end of buffer

    if (nbytes <= ((uint8_t*)fq->out - fq->write->data)) {
        // There's sufficient room
        return true;
    }

    // There's insufficient room
    fq->write->next = NULL; // Causes read pointer to reset
    fq->write = fq->frames;
    return is_writable(fq, nbytes);
}

/**
 * Reserves, or attempts to reserve, space for a frame.
 *
 * @pre                           Queue isn't locked
 * @param[in]  fq                 Frame queue
 * @param[out] data               Pointer to reserved space
 * @param[in]  nbytes             Space to reserve in bytes. If `0`, then the
 *                                return is immediate and nothing is reserved.
 * @param[in]  block              Whether or not to block until space is
 *                                available.
 * @retval     0                  Success. `*data` is set; will be NULL if
 *                                `nbytes == 0`. Call fq_release() after writing
 *                                the data.
 * @retval     FQ_STATUS_INVAL    `fq == NULL || data == NULL`
 * @retval     FQ_STATUS_TOO_BIG  `nbytes` is greater than the queue capacity
 * @retval     FQ_STATUS_NO_SPACE `block == false` and there's insufficient
 *                                space for the frame
 * @retval     FQ_STATUS_SHUTDOWN Queue is shut down for input
 * @post                          Queue isn't locked; space is allocated
 */
static fq_status_t reserve(
        fq_t* const restrict     fq,
        uint8_t** const restrict data,
        const unsigned           nbytes,
        const bool               block)
{
    int status;
    if (nbytes == 0) {
        *data = NULL;
        status = 0;
    }
    else if (fq == NULL || data == NULL) {
        log_add("Invalid argument: fq=%p, data=%p", fq, data);
        status = FQ_STATUS_INVAL;
    }
    else {
        lock(fq);
        if (fq->shutdown) {
            status = FQ_STATUS_SHUTDOWN;
        }
        else if (is_too_big(fq, nbytes)) {
            status = FQ_STATUS_TOO_BIG;
        }
        else {
            while (block && !is_writable(fq, nbytes))
                (void)pthread_cond_wait(&fq->cond, &fq->mutex);
            if (!is_writable(fq, nbytes)) {
                status = FQ_STATUS_NO_SPACE;
            }
            else {
                fq->write->size = 0; // Prevents reading
                *data = fq->write->data;
                fq->reserved = nbytes;
                status = 0;
            }
        }
        unlock(fq);
    }
    return status;
}

/**
 * Indicates if the queue is empty.
 *
 * @pre             The frame queue is locked
 * @param[in] fq    Frame queue
 * @retval    true  iff the queue is empty
 * @post            The frame queue is locked
 */
static bool is_empty(
        fq_t* const fq)
{
    if (fq->read != fq->write && fq->read->next == NULL) {
        // The last write at this position signals a reset
        fq->read = fq->frames;
    }
    return fq->read == fq->write && fq->read->size == 0;
}

/******************************************************************************
 * Public API
 ******************************************************************************/

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
        const size_t size)
{
    int   status;
    if (fq == NULL || size == 0) {
        log_add("Invalid argument: fq=%p, size=%zu", fq, size);
        status = FQ_STATUS_INVAL;
    }
    else {
        fq_t* ptr = log_malloc(sizeof(fq_t), "frame queue");
        if (ptr == NULL) {
            status = FQ_STATUS_SYSTEM;
        }
        else {
            status = init(ptr, size);
            if (status) {
                log_add("Couldn't initialize frame queue");
                free(fq);
            }
            else {
                *fq = ptr;
                status = 0;
            }
        }
    }
    return status;
}

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
        fq_t* const restrict     fq,
        uint8_t** const restrict data,
        const unsigned           nbytes)
{
    return reserve(fq, data, nbytes, true);
}

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
        fq_t* const restrict     fq,
        uint8_t** const restrict data,
        const unsigned           nbytes)
{
    return reserve(fq, data, nbytes, false);
}

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
        const unsigned       nbytes)
{
    int status;
    if (fq == NULL) {
        log_add("NULL frame queue");
        status = FQ_STATUS_INVAL;
    }
    else {
        lock(fq);
        if (nbytes == 0) {
            fq->reserved = 0;
            status = 0;
        }
        else if (nbytes > fq->reserved) {
            log_add("Insufficient space reserved: nbytes=%u, fq->reserved=%u",
                    (unsigned)nbytes, (unsigned)fq->reserved);
            status = FQ_STATUS_UNRESERVED;
        }
        else {
            fq->write->size = nbytes; // Indicates readable; prevents writing
            /*
             * The following depends on the total amount allocated for the queue
             * being an integral multiple of `sizeof(frame_t)`.
             */
            frame_t* next = fq->write +
                    FQ_MIN_ELT((sizeof(frame_t) - 1 + nbytes), sizeof(frame_t));
            if (next >= fq->out)
                next = fq->frames;
            if (next != fq->read)
                next->size = 0; // Allows writing
            fq->write->next = next;
            fq->write = next;
            fq->reserved = 0;
            (void)pthread_cond_signal(&fq->cond);
            status = 0;
        }
        unlock(fq);
    }
    return status;
}

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
        unsigned* restrict       nbytes)
{
    int status;
    lock(fq);
    while (!fq->shutdown && is_empty(fq))
        (void)pthread_cond_wait(&fq->cond, &fq->mutex);
    if (fq->shutdown && is_empty(fq)) {
        status = FQ_STATUS_SHUTDOWN;
    }
    else {
        log_assert(fq->read->data != NULL);
        *data = fq->read->data;

        log_assert(fq->read->size != 0);
        *nbytes = fq->read->size;

        status = 0;
    }

    unlock(fq);
    return status;
}

/***
 * Removes the frame at the head of the frame queue, which must have been
 * returned by a prior call to fq_peek().
 *
 * @param[in]  fq      Frame queue
 * @retval     0       Success
 */
fq_status_t fq_remove(
        fq_t* const fq)
{
    lock(fq);

    log_assert(fq->read->size != 0);
    fq->read->size = 0; // Indicates writable

    log_assert(fq->read->next != NULL);
    fq->read = fq->read->next;

    (void)pthread_cond_signal(&fq->cond);

    unlock(fq);
    return 0;
}

/**
 * Shuts down a frame queue for input.
 *
 * @param[in] fq               Frame queue to be shut down
 * @retval    0                Success. fq_peek() will now return
 *                             FQ_STATUS_SHUTDOWN when the queue is empty.
 * @retval    FQ_STATUS_INVAL  `fq == NULL`
 */
fq_status_t fq_shutdown(
        fq_t* const fq)
{
    int status;
    if (fq == NULL) {
        status = FQ_STATUS_INVAL;
    }
    else {
        lock(fq);
        fq->shutdown = true;
        (void)pthread_cond_signal(&fq->cond);
        unlock(fq);
        status = 0;
    }
    return status;
}

/**
 * Frees a frame queue.
 *
 * @param[in] fq  Frame queue to be freed or NULL.
 */
void fq_free(
        fq_t* const fq)
{
    if (fq) {
        fini(fq);
        free(fq);
    }
}
