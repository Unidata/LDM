/*
 *   Copyright 2014, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#include "config.h"
#include "log.h"
#include "fifo.h"
#include "reader.h"     /* Eat own dog food */

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct reader {
    /// Pointer to FIFO into which to put data
    Fifo*                 fifo;
    /// Object access lock
    pthread_mutex_t       mutex;
    /// Number of bytes received
    unsigned long         byteCount;
    /// Maximum amount to read in a single call in bytes
    size_t                maxSize;
};

/**
 * Initializes a new reader.
 *
 * @param[in] reader   The reader to be initialized
 * @param[in] fifo     Pointer to FIFO that will read input
 * @param[in] maxSize  Maximum amount to read in a single call in bytes
 * @retval    0        Success
 * @retval    2        O/S failure. `log_add()` called.
 */
static int
reader_init(
        Reader* const restrict reader,
        Fifo* const restrict   fifo,
        const size_t           maxSize)
{
    log_assert(reader);
    log_assert(fifo);
    log_assert(maxSize > 0);

    pthread_mutexattr_t attr;
    int                 status = pthread_mutexattr_init(&attr);

    if (status) {
        log_errno(status, "Couldn't initialize mutex attributes");
        status = 2;
    }
    else {
        // At most one lock per thread
        (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        // Prevent priority inversion
        (void)pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);

        status = pthread_mutex_init(&reader->mutex, &attr);

        if (status) {
            log_errno(status, "Couldn't initialize mutex");
            status = 2;
        }
        else {
            reader->byteCount = 0;
            reader->fifo = fifo;
            reader->maxSize = maxSize;
        }

        (void)pthread_mutexattr_destroy(&attr);
    } // `attr` initialized

    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new reader. The client should call `readerFree()` when the reader
 * is no longer needed.
 *
 * This function is thread-safe.
 *
 * @param[in]  fifo     Pointer to FIFO that will read input
 * @param[in]  maxSize  Maximum amount to read in a single call in bytes.
 * @retval     NULL     Failure. `log_add()` called.
 * @return              Reader
 */
Reader*
readerNew(
    Fifo* const         fifo,
    const size_t        maxSize)
{
    log_assert(fifo);
    log_assert(maxSize > 0);

    int       status;
    Reader*   reader = log_malloc(sizeof(Reader), "input reader");

    if (reader) {
        status = reader_init(reader, fifo, maxSize);

        if (status) {
            log_add("Couldn't initialize reader");
            free(reader);
            reader = NULL;
        }
    } // `reader` allocated

    return reader;
}

/**
 * Frees a reader. Closes the file descriptor given to `readerNew()`. Does not
 * free the FIFO given to `readerNew()`.
 *
 * @param[in] reader  Reader to be freed. May be NULL.
 */
void readerFree(
    Reader* const   reader)
{
    if (reader) {
        (void)pthread_mutex_destroy(&reader->mutex);
        free(reader);
    }
}

/**
 * Executes a reader. Returns when end-of-input is encountered, the FIFO queue
 * is explicitly closed, or an error occurs. Logs a message on error. May be
 * called by `pthread_create()`.
 *
 * This function is thread-safe.
 *
 * @param[in]  arg   Pointer to reader.
 * @retval     &0    Success. End of input encountered.
 * @retval     &1    FIFO was closed.
 * @retval     &2    O/S failure. `log_flush()` called.
 */
void*
readerStart(
        void* const arg)
{
    Reader* const reader = (Reader*)arg;
    int           status;

    for (;;) {
        size_t nbytes;

        status = fifo_readFd(reader->fifo, reader->maxSize, &nbytes);

        if (status) {
            if (3 == status) {
                // FIFO was closed
                log_debug("FIFO was closed");
                log_clear();
                status = 1;
            }
            else {
                log_warning("fifo_readFd() failure");
                status = 2;
            }
            break;
        }
        if (0 == nbytes) {
            log_notice("FIFO EOF");
            break; // EOF
        }

        (void)pthread_mutex_lock(&reader->mutex);
            reader->byteCount += nbytes;
        (void)pthread_mutex_unlock(&reader->mutex);
    }                       /* I/O loop */

    log_flush_error();
    log_free();  // could be end of thread

    static int returnPointer[] = {0, 1, 2};
    return returnPointer + status;
}

/**
 * Returns statistics since the last time this function was called or
 * `readerStart()` was called. This function is thread-safe.
 *
 * @param[in]  reader         The reader of input data.
 * @param[out] nbytes         Number of bytes read.
 * @param[out] fullFifoCount  Number of times the reader-thread had to wait on a
 *                            "full" FIFO.
 */
void readerGetStatistics(
    Reader* const           reader,
    unsigned long* const    nbytes,
    unsigned long* const    fullFifoCount)
{
    (void)pthread_mutex_lock(&reader->mutex);

    *nbytes = reader->byteCount;
    *fullFifoCount = fifo_getFullCount(reader->fifo);
    reader->byteCount = 0;

    (void)pthread_mutex_unlock(&reader->mutex);
}
