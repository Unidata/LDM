/*
 *   Copyright 2014, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#include "config.h"
#include "log.h"
#include "fifo.h"                   /* Eat own dog food */

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

struct fifo {
    unsigned char*      buf;        /**< Pointer to start of buffer */
    size_t              nextWrite;  /**< Offset to next byte to write */
    size_t              nbytes;     /**< Number of bytes in the buffer */
    size_t              size;       /**< Size of buffer in bytes */
    pthread_mutex_t     mutex;      /**< Concurrent access lock */
    pthread_cond_t      cond;       /**< Condition variable */
    bool                closeIfEmpty;   /**< Close the FIFO if empty? */
};

/**
 * Initializes a FIFO
 *
 * @retval 0    Success
 * @retval 1    Usage error. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
 */
static int
fifo_init(
    Fifo* const             fifo,   /**< [in/out] Pointer to the FIFO */
    unsigned char* const    buf,    /**< [in] The buffer */
    const size_t            size)   /**< [in] Size of the buffer in bytes */
{
    int                 status = 2; /* default failure */
    pthread_mutexattr_t mutexAttr;

    if ((status = pthread_mutexattr_init(&mutexAttr)) != 0) {
        LOG_ERRNUM0(status, "Couldn't initialize mutex attributes");
        status = 2;
    }
    else {
        // Recursive because of `fifo_closeWhenEmpty()`
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);

        if ((status = pthread_mutex_init(&fifo->mutex, &mutexAttr)) != 0) {
            LOG_ERRNUM0(status, "Couldn't initialize mutex");
            status = 2;
        }
        else {
            if ((status = pthread_cond_init(&fifo->cond, NULL)) !=
                    0) {
                LOG_ERRNUM0(status,
                    "Couldn't initialize condition variable");
                status = 2;
            }
            else {
                fifo->buf = buf;
                fifo->nextWrite = 0;
                fifo->nbytes = 0;  /* indicates startup */
                fifo->size = size;
                fifo->closeIfEmpty = false;
                status = 0; /* success */
            }                   /* `fifo->cond` initialized */

            if (0 != status)
                (void)pthread_mutex_destroy(&fifo->mutex);
        }                       /* "fifo->mutex" initialized */

        (void)pthread_mutexattr_destroy(&mutexAttr);
    }                                   /* "mutexAttr" initialized */

    return status;
}

static inline void
fifo_lock(
        Fifo* const restrict fifo)
{
    (void)pthread_mutex_lock(&fifo->mutex);
}

static inline void
fifo_unlock(
        Fifo* const restrict fifo)
{
    (void)pthread_mutex_unlock(&fifo->mutex);
}

static inline void
fifo_signal(
        Fifo* const restrict fifo)
{
    (void)pthread_cond_signal(&fifo->cond);
}

static inline void
fifo_wait(
        Fifo* const fifo)
{
    (void)pthread_cond_wait(&fifo->cond, &fifo->mutex);
}

static inline void
fifo_close(
        Fifo* const fifo)
{
    fifo->closeIfEmpty = true;
}

/**
 * Returns the capacity of a FIFO.
 *
 * @pre             {FIFO is locked.}
 * @param[in] fifo  FIFO.
 * @return          Capacity of FIFO in bytes.
 */
static inline size_t
fifo_capacity(
        const Fifo* const restrict fifo)
{
    return fifo->size;
}

/**
 * Returns the amount of space available for writing.
 *
 * @pre             {FIFO is locked.}
 * @param[in] fifo  FIFO.
 * @return          Amount of available space in bytes.
 */
static inline size_t
fifo_availableForWriting(
        const Fifo* const restrict fifo)
{
    return fifo->size - fifo->nbytes;
}

/**
 * Returns the amount of data available for reading.
 *
 * @pre             {FIFO is locked.}
 * @param[in] fifo  FIFO.
 * @return          Amount of available data in bytes.
 */
static inline size_t
fifo_availableForReading(
        const Fifo* const restrict fifo)
{
    return fifo->nbytes;
}

/**
 * Indicates if a FIFO is closed.
 *
 * @pre              {FIFO is locked}
 * @param[in] fifo   FIFO.
 * @retval    true   FIFO is closed.
 * @retval    false  FIFO isn't closed.
 */
static inline bool
fifo_isClosed(
        const Fifo* const fifo)
{
    return fifo->closeIfEmpty && fifo_availableForReading(fifo) == 0;
}

/**
 * Transfers bytes from a file to a FIFO.
 *
 * @pre                  {FIFO is locked and not closed}
 * @pre                  {`availableForWriting(fifo) >= maxBytes`}
 * @param[in]  fifo      FIFO.
 * @param[in]  fd        File descriptor.
 * @param[in]  maxBytes  Maximum number of bytes to transfer.
 * @param[out] nbytes    Actual number of bytes transferred.
 * @retval     0         Success.
 * @retval     2         O/S error. `log_start()` called.
 */
static inline ssize_t // `inline` because only called in one place
fifo_transferFromFd(
        Fifo* const restrict   fifo,
        const int              fd,
        const size_t           maxBytes,
        size_t* const restrict nbytes)
{
    ssize_t      nb;
    const size_t extent = fifo->size - fifo->nextWrite;
    void*        ptr = fifo->buf + fifo->nextWrite;

    if (maxBytes <= extent) {
        nb = read(fd, ptr, maxBytes);
    }
    else {
        struct iovec iov[2];

        iov[0].iov_base = ptr;
        iov[0].iov_len = extent;
        iov[1].iov_base = fifo->buf;
        iov[1].iov_len = maxBytes - extent;

        nb = readv(fd, iov, 2);
    }

    if (-1 == nb) {
        LOG_SERROR2("Couldn't read up to %lu bytes from file descriptor %d",
                (unsigned long)maxBytes, (unsigned long)fd);
        return 2;
    }

    fifo->nbytes += nb;
    *nbytes = nb;
    fifo->nextWrite = (fifo->nextWrite + nb) % fifo->size;

    return 0;
}

/**
 * Removes bytes from a FIFO.
 *
 * @pre               {FIFO is locked}
 * @pre               {`fifo_availableForReading(fifo) >= nbytes`}
 * @param[in] fifo    FIFO.
 * @param[in] buf     Buffer into which to put bytes.
 * @param[in] nbytes  Number of bytes to remove.
 * @retval    0       Success. `nbytes` bytes were removed.
 */
static inline void // `inline` because only called in one place
fifo_removeBytes(
        Fifo* const restrict          fifo,
        unsigned char* const restrict buf,
        const size_t                  nbytes)
{
    const size_t nextRead = (fifo->nextWrite + fifo->size - fifo->nbytes) %
            fifo->size;
    const size_t extent = fifo->size - nextRead;

    if (extent >= nbytes) {
        (void)memcpy(buf, fifo->buf + nextRead, nbytes);
    }
    else {
        (void)memcpy(buf, fifo->buf + nextRead, extent);
        (void)memcpy(buf + extent, fifo->buf, nbytes - extent);
    }

    fifo->nbytes -= nbytes;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a FIFO.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Usage error. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
 */
int
fifo_new(
    const size_t        npages,         /**< [in] FIFO size in pages */
    Fifo** const        fifo)           /**< [out] Pointer to pointer to be set
                                         *   to address of FIFO */
{
    int                 status = 2;     /* default failure */
    Fifo*               f = (Fifo*)malloc(sizeof(Fifo));

    if (NULL == f) {
        LOG_SERROR0("Couldn't allocate FIFO");
    }
    else {
        const long              pagesize = sysconf(_SC_PAGESIZE);
        const size_t            size = npages*pagesize;
        unsigned char* const    buf = (unsigned char*)malloc(size);

        if (NULL == buf) {
            LOG_SERROR1("Couldn't allocate %lu bytes for FIFO buffer",
                    (unsigned long)size);
        }
        else {
            if ((status = fifo_init(f, buf, size)) == 0)
                *fifo = f;

            if (0 != status)
                free(buf);
        }                               /* "buf" allocated */

        if (0 != status)
            free(f);
    }                                   /* "f" allocated */

    return status;
}

/**
 * Transfers bytes from a file to a FIFO. Blocks until space is available.
 *
 * @param[in]  fifo      FIFO.
 * @param[in]  fd        File descriptor from which to obtain bytes.
 * @param[in]  maxBytes  Maximum number of bytes to transfer from `fd` to `fifo`.
 * @param[out] nbytes    Actual number of bytes transferred.
 * @retval     0         Success.
 * @retval     1         Usage error. `log_start()` called.
 * @retval     2         System error. `log_start()` called.
 * @retval     3         FIFO is closed.
 */
int
fifo_readFd(
        Fifo* const restrict   fifo,
        const int              fd,
        const size_t           maxBytes,
        size_t* const restrict nbytes)
{
    int status;

    fifo_lock(fifo);

    if (maxBytes > fifo_capacity(fifo)) {
        LOG_START2("Request-amount is greater than FIFO-capacity: %lu > %lu",
                (unsigned long)maxBytes, (unsigned long)fifo_capacity(fifo));
        status = 1;
    }
    else {
        while (!fifo_isClosed(fifo) &&
                fifo_availableForWriting(fifo) < maxBytes)
            fifo_wait(fifo);

        if (fifo_isClosed(fifo)) {
            status = 3;
        }
        else {
            status = fifo_transferFromFd(fifo, fd, maxBytes, nbytes);
            fifo_signal(fifo);
        }
    }

    fifo_unlock(fifo);

    return status;
}

/**
 * Removes bytes from a FIFO. Blocks until the amount of requested data exists.
 *
 * This function is thread-safe.
 *
 * @param[in] fifo    FIFO.
 * @param[in] buf     Buffer into which to put bytes.
 * @param[in[ nbytes  Number of bytes to remove.
 * @retval    0       Success.
 * @retval    1       Usage error. \c log_start() called.
 * @retval    3       FIFO is closed.
 */
int
fifo_getBytes(
    Fifo* const             fifo,
    unsigned char* const    buf,
    const size_t            nbytes)
{
    int status;

    fifo_lock(fifo);

    if (nbytes > fifo_capacity(fifo)) {
        LOG_START2("Request-amount is greater than FIFO capacity: %lu > %lu",
                (unsigned long)nbytes, (unsigned long)fifo_capacity(fifo));
        status = 1;
    }
    else {
        while (!fifo_isClosed(fifo) && fifo_availableForReading(fifo) < nbytes)
            fifo_wait(fifo);

        if (fifo_isClosed(fifo)) {
            status = 3;
        }
        else {
            fifo_removeBytes(fifo, buf, nbytes);
            fifo_signal(fifo);
            status = 0;
        }
    } // `nbytes` < fifo-capacity

    fifo_unlock(fifo);

    return status;
}

/**
 * Causes a FIFO to close when it becomes empty. Attempting to write to or read
 * from a closed FIFO will result in an error.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success
 * @retval 1    Usage error. `log_start()` called.
 * @retval 2    O/S error. `log_start()` called.
 */
void
fifo_closeWhenEmpty(
    Fifo* const fifo)       /**< [in/out] Pointer to FIFO */
{
    fifo_lock(fifo);
    fifo_close(fifo);
    fifo_signal(fifo);
    fifo_unlock(fifo);
}
