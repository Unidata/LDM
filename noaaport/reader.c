/*
 *   Copyright 2014, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#include "config.h"
#include "mylog.h"
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
    /// File-descriptor to read from
    int                   fd;
};

/**
 * Initializes a new reader.
 *
 * @param[in] reader   The reader to be initialized.
 * @param[in] fd       File-descriptor to read from.
 * @param[in] fifo     Pointer to FIFO into which to put data.
 * @param[in] maxSize  Maximum amount to read in a single call in bytes.
 * @retval    0        Success. `*reader` is set.
 * @retval    2        O/S failure. `mylog_add()` called.
 */
static int reader_init(
        Reader* const restrict reader,
        const int              fd,
        Fifo* const restrict   fifo,
        const size_t           maxSize)
{
    pthread_mutexattr_t attr;
    int                 status = pthread_mutexattr_init(&attr);

    if (status) {
        mylog_errno(status, "Couldn't initialize mutex attributes");
        status = 2;
    }
    else {
        // At most one lock per thread
        (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
        // Prevent priority inversion
        (void)pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);

        status = pthread_mutex_init(&reader->mutex, &attr);

        if (status) {
            mylog_errno(status, "Couldn't initialize mutex");
            status = 2;
        }
        else {
            reader->byteCount = 0;
            reader->fifo = fifo;
            reader->fd = fd;
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
 * @param[in]  fd       File-descriptor to read from. Will be closed by
 *                      `readerFree()`.
 * @param[in]  fifo     Pointer to FIFO into which to put data.
 * @param[in]  maxSize  Maximum amount to read in a single call in bytes.
 * @param[out] reader   Returned reader.
 * @retval     0        Success. `*reader` is set.
 * @retval     1        Precondition failure. `mylog_add()` called.
 * @retval     2        O/S failure. `mylog_add()` called.
 */
int readerNew(
    const int           fd,
    Fifo* const         fifo,
    const size_t        maxSize,
    Reader** const      reader)
{
    int       status;
    Reader*   r = (Reader*)malloc(sizeof(Reader));

    if (NULL == r) {
        mylog_syserr("Couldn't allocate new reader");
        status = 2;
    }
    else {
        status = reader_init(r, fd, fifo, maxSize);

        if (status) {
            mylog_add("Couldn't initialize reader");
            free(r);
        }
        else {
            *reader = r;
        }
    } // `r` allocated

    return status;
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
        (void)close(reader->fd);
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
 * @retval     &2    O/S failure. `mylog_flush()` called.
 */
void*
readerStart(
        void* const arg)
{
    Reader* const reader = (Reader*)arg;
    int           status;

    for (;;) {
        size_t nbytes;

        status = fifo_readFd(reader->fifo, reader->fd, reader->maxSize,
                &nbytes);

        if (status) {
            if (3 == status) {
                // FIFO was closed
                mylog_debug("FIFO was closed");
                mylog_clear();
                status = 1;
            }
            else {
                mylog_debug("fifo_readFd() failure");
                status = 2;
            }
            break;
        }
        if (0 == nbytes) {
            mylog_debug("FIFO EOF");
            break; // EOF
        }

        (void)pthread_mutex_lock(&reader->mutex);
        reader->byteCount += nbytes;
        (void)pthread_mutex_unlock(&reader->mutex);
    }                       /* I/O loop */

    mylog_flush_error();
    mylog_free();  // could be end of thread

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
