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
    Fifo*                 fifo;           /**< Pointer to FIFO into which to put data
                                            */
    pthread_mutex_t       mutex;          /**< Object access lock */
    unsigned long         byteCount;      /**< Number of bytes received */
    size_t                maxSize;        /**< Maximum amount to read in a single
                                            *  call in bytes */
    int                   fd;             /**< File-descriptor to read from */
};

/**
 * Returns a new reader. The client should call `readerFree()` when the reader
 * is no longer needed.
 *
 * This function is thread-safe.
 *
 * @param[in]  fd       File-descriptor to read from.
 * @param[in]  fifo     Pointer to FIFO into which to put data.
 * @param[in]  maxSize  Maximum amount to read in a single call in bytes.
 * @param[out] reader   Returned reader.
 * @retval     0        Success. `*reader` is set.
 * @retval     1        Precondition failure. `log_start()` called.
 * @retval     2        O/S failure. `log_start()` called.
 */
int readerNew(
    const int           fd,
    Fifo* const         fifo,
    const size_t        maxSize,
    Reader** const      reader)
{
    int       status = 2;       /* default failure */
    Reader*   r = (Reader*)malloc(sizeof(Reader));

    if (NULL == r) {
        LOG_SERROR0("Couldn't allocate new reader");
    }
    else {
        if ((status = pthread_mutex_init(&r->mutex, NULL)) != 0) {
            LOG_ERRNUM0(status, "Couldn't initialize reader mutex");
            status = 2;
        }
        else {
            r->byteCount = 0;
            r->fifo = fifo;
            r->fd = fd;
            r->maxSize = maxSize;
            *reader = r;
        }
    }

    return status;
}

/**
 * Frees a reader. Does not close the file descriptor or free the FIFO which
 * were passed to `readerNew()`.
 *
 * @param[in] reader  Reader to be freed. May be NULL.
 */
void readerFree(
    Reader* const   reader)
{
    free(reader);
}

/**
 * Executes a reader. Returns when end-of-input is encountered or an error
 * occurs. Logs a message on error. Called by `pthread_create()`.
 *
 * This function is thread-safe.
 *
 * @param[in]  arg   Pointer to reader.
 * @retval     &0    Success.
 * @retval     &2    O/S failure. `log_log()` called.
 */
void*
readerStart(
        void* const arg)        /**< Pointer to the reader to be executed */
{
    Reader* const reader = (Reader*)arg;
    int           status;

    for (;;) {
        size_t nbytes;

        status = fifo_readFd(reader->fifo, reader->fd, reader->maxSize,
                &nbytes);

        if (status) {
            status = 2;
            break;
        }
        if (0 == nbytes) {
            status = 0;
            break;
        }

        (void)pthread_mutex_lock(&reader->mutex);
        reader->byteCount += nbytes;
        (void)pthread_mutex_unlock(&reader->mutex);
    }                       /* I/O loop */

    static int returnPointer[] = {0, 1, 2};

    return returnPointer + status;
}

/**
 * Returns statistics since the last time this function was called or
 * `readerStart()` was called.
 */
void readerGetStatistics(
    Reader* const           reader, /**< [in] Pointer to the reader */
    unsigned long* const    nbytes) /**< [out] Number of bytes received */
{
    (void)pthread_mutex_lock(&reader->mutex);

    *nbytes = reader->byteCount;
    reader->byteCount = 0;

    (void)pthread_mutex_unlock(&reader->mutex);
}
