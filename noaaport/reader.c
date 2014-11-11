/*
 *   Copyright 2014, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#include "config.h"
#include "log.h"
#include "fifo.h"
#include "reader.h"     /* Eat own dog food */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct reader {
    Fifo*           fifo;           /**< Pointer to FIFO into which to put data
                                      */
    unsigned char*  buf;            /**< Internal read buffer */
    pthread_mutex_t mutex;          /**< Object access lock */
    unsigned long   byteCount;      /**< Number of bytes received */
    size_t          maxSize;        /**< Maximum amount to read in a single
                                      *  call in bytes */
    int             fd;             /**< File-descriptor to read from */
    volatile int    status;         /**< Termination status */
};

/**
 * Returns a new reader. The client should call \link \c readerFree() \endlink
 * when the reader is no longer needed.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Precondition failure. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
 */
int readerNew(
    const int           fd,         /**< [in] File-descriptor to read from */
    Fifo* const         fifo,       /**< [in] Pointer to FIFO into which to put
                                     *   data */
    const size_t        maxSize,    /**< [in] Maximum amount to read in a single
                                     *   call in bytes */
    Reader** const      reader)     /**< [out] Pointer to pointer to address of
                                     *   reader */
{
    int       status = 2;       /* default failure */
    Reader*   r = (Reader*)malloc(sizeof(Reader));

    if (NULL == r) {
        LOG_SERROR0("Couldn't allocate new reader");
    }
    else {
        unsigned char*    buf = (unsigned char*)malloc(maxSize);

        if (NULL == buf) {
            LOG_SERROR1("Couldn't allocate %lu bytes for buffer", 
                    (unsigned long)maxSize);
        }
        else {
            if ((status = pthread_mutex_init(&r->mutex, NULL)) != 0) {
                LOG_ERRNUM0(status, "Couldn't initialize product-maker mutex");
                status = 2;
            }
            else {
                r->byteCount = 0;
                r->fifo = fifo;
                r->fd = fd;
                r->maxSize = maxSize;
                r->buf = buf;
                r->status = 0;
                *reader = r;
            }
        }
    }

    return status;
}

/**
 * Frees a reader.
 */
void readerFree(
    Reader* const   reader)     /**< Pointer to the reader to be freed */
{
    if (NULL != reader) {
        free(reader->buf);
        free(reader);
    }
}

/**
 * Executes a reader. Returns when end-of-input is encountered or an error
 * occurs.
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @return NULL
 * @see \link readerStatus() \endlink
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
        else if (0 == nbytes) {
            break;
        }
        else {
            (void)pthread_mutex_lock(&reader->mutex);
            reader->byteCount += nbytes;
            (void)pthread_mutex_unlock(&reader->mutex);
        }
    }                                       /* I/O loop */

    (void)pthread_mutex_lock(&reader->mutex);
    reader->status = status;
    (void)pthread_mutex_unlock(&reader->mutex);

    return NULL;
}

/**
 * Returns statistics since the last time this function was called or \link
 * readerStart() \endlink was called.
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

/**
 * Returns the termination status of a data-reader.
 *
 * @retval 0    Success. End-of-file encountered.
 * @retval 1    Precondition failure. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
 */
int readerStatus(
    Reader* const   reader) /**< [in] Pointer to the reader */
{
    return reader->status;
}
