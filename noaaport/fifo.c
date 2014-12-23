/**
 * Copyright 2014, University Corporation for Atmospheric Research. See file
 * COPYRIGHT in the top-level source-directory for copying and redistribution
 * conditions.
 *
 * This module implements a FIFO queue that is designed for *one* writer thread
 * and *one* reader thread. Any other usage results in undefined behavior.
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

#define USE_RWLOCK 1

struct fifo {
    unsigned char*   buf;          ///< Pointer to start of buffer
    size_t           nextWrite;    ///< Offset to next byte to write
    size_t           nbytes;       ///< Number of bytes in the buffer
    size_t           size;         ///< Size of buffer in bytes
    /**
     * Number of times `fifo_readFd()` had to wait until sufficient space was
     * available.
     */
    size_t           fullCount;
#if USE_RWLOCK
    pthread_rwlock_t rwlock;       ///< Read/Write lock
#endif
    pthread_mutex_t  mutex;        ///< Mutual exclusion object
    pthread_cond_t   cond;         ///< Condition variable
    bool             isClosed;     ///< FIFO is closed?
};

/**
 * Initializes a FIFO
 *
 * @retval 0    Success
 * @retval 1    Usage error. `log_start()` called.
 * @retval 2    O/S failure. `log_start()` called.
 */
static int
fifo_init(
    Fifo* const             fifo,   /**< [in/out] Pointer to the FIFO */
    unsigned char* const    buf,    /**< [in] The buffer */
    const size_t            size)   /**< [in] Size of the buffer in bytes */
{
    int                 status;
    pthread_mutexattr_t mutexAttr;

    if ((status = pthread_mutexattr_init(&mutexAttr)) != 0) {
        LOG_ERRNUM0(status, "Couldn't initialize mutex attributes");
        status = 2;
    }
    else {
        // At most one lock per thread.
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_ERRORCHECK);
        // Prevent priority inversion
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);

        if ((status = pthread_mutex_init(&fifo->mutex, &mutexAttr)) != 0) {
            LOG_ERRNUM0(status, "Couldn't initialize mutex");
            status = 2;
        }
        else {
            if ((status = pthread_cond_init(&fifo->cond, NULL)) != 0) {
                LOG_ERRNUM0(status, "Couldn't initialize condition variable");
                status = 2;
            }
            else {
#if USE_RWLOCK
                if ((status = pthread_rwlock_init(&fifo->rwlock, NULL))) {
                    LOG_ERRNUM0(status, "Couldn't initialize read/write lock");
                    status = 2;
                }
                else {
#endif
                    fifo->buf = buf;
                    fifo->nextWrite = 0;
                    fifo->nbytes = 0;  /* indicates startup */
                    fifo->size = size;
                    fifo->isClosed = false;
                    fifo->fullCount = 0;
                    status = 0; /* success */
#if USE_RWLOCK
                }
#endif
            }                   /* `fifo->cond` initialized */

            if (status)
                (void)pthread_mutex_destroy(&fifo->mutex);
        }                       /* "fifo->mutex" initialized */

        (void)pthread_mutexattr_destroy(&mutexAttr);
    }                                   /* "mutexAttr" initialized */

    return status;
}

/**
 * Destroys (i.e., releases the resources of) a FIFO.
 *
 * @param[in] fifo  Pointer to FIFO to be destroyed.
 */
static void
fifo_destroy(
        Fifo* const fifo)
{
    (void)pthread_cond_destroy(&fifo->cond);
    (void)pthread_mutex_destroy(&fifo->mutex);
#if USE_RWLOCK
    (void)pthread_rwlock_destroy(&fifo->rwlock);
#endif
    free(fifo->buf);
}

/**
 * Read-locks a FIFO.
 *
 * @param[in] fifo  The FIFO to be read-locked.
 */
static inline void
fifo_readLock(
        Fifo* const fifo)
{
#if USE_RWLOCK
    (void)pthread_rwlock_rdlock(&fifo->rwlock);
#else
    (void)pthread_mutex_unlock(&fifo->mutex);
#endif
}

/**
 * Write-locks a FIFO.
 *
 * @param[in] fifo  The FIFO to be write-locked.
 */
static inline void
fifo_writeLock(
        Fifo* const fifo)
{
#if USE_RWLOCK
    (void)pthread_rwlock_wrlock(&fifo->rwlock);
#else
    (void)pthread_mutex_lock(&fifo->mutex);
#endif
}

/**
 * Unlocks a FIFO.
 *
 * @param[in] fifo  The FIFO to be unlocked.
 */
static inline void
fifo_unlock(
        Fifo* const fifo)
{
#if USE_RWLOCK
    (void)pthread_rwlock_unlock(&fifo->rwlock);
#else
    (void)pthread_mutex_unlock(&fifo->mutex);
#endif
}

/**
 * Signals a change in a FIFO's state.
 *
 * @param[in] fifo  The FIFO.
 */
static inline void
fifo_signal(
        Fifo* const restrict fifo)
{
    /*
     * Failure isn't possible because `fifo->cond` is valid.
     */
    (void)pthread_cond_signal(&fifo->cond);
}

/**
 * Waits for a change in a FIFO's state.
 *
 * @param[in] fifo  The FIFO.
 * @pre             {The FIFO's mutex is locked.}
 */
static inline void
fifo_wait(
        Fifo* const fifo)
{
    /*
     * Failure isn't possible because `fifo->cond` and `fifo->mutex` are valid
     * and the mutex is locked by the current thread.
     */
    (void)pthread_cond_wait(&fifo->cond, &fifo->mutex);
}

/**
 * Blocks while a FIFO is in a particular state.
 *
 * @param[in]  fifo       The FIFO.
 * @param[in]  whilePred  The predicate that defines the state.
 * @param[in]  nbytes     Number of bytes.
 * @retval     0          No waiting occurred.
 * @retval     1          Waiting occurred.
 * @pre                   {The FIFO is read-locked.}
 * @post                  {The FIFO is read-locked.}
 */
static int
fifo_waitWhile(
        Fifo* const restrict fifo,
        bool         (*const whilePred)(Fifo* fifo, size_t nbytes),
        const size_t         nbytes)
{
    int didWait = 0;

#if USE_RWLOCK
    if (whilePred(fifo, nbytes)) {
        (void)pthread_mutex_lock(&fifo->mutex);
        (void)pthread_rwlock_unlock(&fifo->rwlock);
        didWait = 1;
#endif
        while (whilePred(fifo, nbytes)) {
           fifo_wait(fifo);
#if !USE_RWLOCK
           didWait = 1;
#endif
        }

#if USE_RWLOCK
        (void)pthread_rwlock_rdlock(&fifo->rwlock);
        (void)pthread_mutex_unlock(&fifo->mutex);
    }
#endif

    return didWait;
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
 * Indicates if a FIFO is open and not ready to be written.
 *
 * @param[in] fifo    The FIFO.
 * @param[in] nbytes  The number of bytes to write.
 * @retval    false   The FIFO is closed or sufficient space exists.
 * @retval    true    The FIFO is open and insufficient space exists.
 */
static inline bool
fifo_isOpenAndNotWritable(
        Fifo* const  fifo,
        const size_t nbytes)
{
    return !fifo->isClosed && fifo_availableForWriting(fifo) < nbytes;
}

/**
 * Indicates if a FIFO is open and not ready to be read.
 *
 * @param[in] fifo    The FIFO.
 * @param[in] nbytes  The desired number of bytes to read.
 * @retval    false   The FIFO is closed or sufficient data exists.
 * @retval    true    The FIFO is open and insufficient data exists.
 */
static inline bool
fifo_isOpenAndNotReadable(
        Fifo* const  fifo,
        const size_t nbytes)
{
    return !fifo->isClosed && fifo_availableForReading(fifo) < nbytes;
}

/**
 * Indicates if a requested number of bytes is invalid (i.e., theoretically
 * impossible) for a FIFO.
 *
 * @param[in] fifo    The FIFO.
 * @param[in] nbytes  The number of bytes.
 * @retval    true    The number of bytes is theoretically impossible.
 *                    `log_add()` called.
 * @retval    false   The number of bytes is theoretically possible.
 * @pre               {The FIFO is locked.}
 */
static bool
fifo_isInvalidSize(
        const Fifo* const fifo,
        const size_t      nbytes)
{
    if (nbytes <= fifo_capacity(fifo))
        return false;

    LOG_START2("Request-amount is greater than FIFO capacity: %lu > %lu",
            (unsigned long)nbytes, (unsigned long)fifo_capacity(fifo));
    return true;
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
static inline int // `inline` because only called in one place
fifo_transferFromFd(
        Fifo* const restrict   fifo,
        const int              fd,
        const size_t           maxBytes,
        size_t* const restrict nbytes)
{
    ssize_t              nb;
    const size_t         extent = fifo->size - fifo->nextWrite;
    unsigned char* const buf = fifo->buf;
    void*                ptr = buf + fifo->nextWrite;
    int                  status;

    fifo_unlock(fifo);  // allow concurrent reading from FIFO

    if (maxBytes <= extent) {
        nb = read(fd, ptr, maxBytes);
    }
    else {
        struct iovec iov[2];

        iov[0].iov_base = ptr;
        iov[0].iov_len = extent;
        iov[1].iov_base = buf;
        iov[1].iov_len = maxBytes - extent;

        nb = readv(fd, iov, 2);
    }

    if (-1 == nb) {
        LOG_SERROR2("Couldn't read up to %lu bytes from file descriptor %d",
                (unsigned long)maxBytes, (unsigned long)fd);
        status = 2;
        fifo_writeLock(fifo); // was locked => deadlock not possible
    }
    else {
        *nbytes = nb;
        status = 0;
        fifo_writeLock(fifo); // was locked => deadlock not possible
        fifo->nbytes += nb;
        fifo->nextWrite = (fifo->nextWrite + nb) % fifo->size;
    }

    return status;
}

/**
 * Removes bytes from a FIFO.
 *
 * @pre               {FIFO is locked}
 * @pre               {`fifo_availableForReading(fifo) >= nbytes`}
 * @param[in] fifo    FIFO.
 * @param[in] buf     Buffer into which to put bytes.
 * @param[in] nbytes  Number of bytes to remove.
 */
static inline void // `inline` because only called in one place
fifo_removeBytes(
        Fifo* const restrict          fifo,
        unsigned char* const restrict buf,
        const size_t                  nbytes)
{
    const size_t         nextRead = (fifo->nextWrite + fifo->size -
            fifo->nbytes) % fifo->size;
    const size_t         extent = fifo->size - nextRead;
    unsigned char* const fifoBuf = fifo->buf;

    fifo_unlock(fifo); // allow concurrent writing to FIFO

    if (extent >= nbytes) {
        (void)memcpy(buf, fifoBuf + nextRead, nbytes);
    }
    else {
        (void)memcpy(buf, fifoBuf + nextRead, extent);
        (void)memcpy(buf + extent, fifoBuf, nbytes - extent);
    }

    fifo_writeLock(fifo);
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
    int   status;
    Fifo* f = (Fifo*)malloc(sizeof(Fifo));

    if (NULL == f) {
        LOG_SERROR0("Couldn't allocate FIFO object");
        status = 2;
    }
    else {
        const long              pagesize = sysconf(_SC_PAGESIZE);
        const size_t            size = npages*pagesize;
        unsigned char* const    buf = (unsigned char*)malloc(size);

        if (NULL == buf) {
            LOG_SERROR1("Couldn't allocate %lu bytes for FIFO buffer",
                    (unsigned long)size);
            status = 2;
        }
        else {
            if ((status = fifo_init(f, buf, size)) == 0)
                *fifo = f;

            if (status)
                free(buf);
        }                               /* "buf" allocated */

        if (status)
            free(f);
    }                                   /* "f" allocated */

    return status;
}

/**
 * Frees a FIFO.
 *
 * @param[in] fifo  Pointer to FIFO to be freed.
 */
void
fifo_free(
        Fifo* const fifo)
{
    if (fifo) {
        fifo_destroy(fifo);
        free(fifo);
    }
}

/**
 * Transfers bytes from a file to a FIFO. Blocks until space is available. This
 * function is thread-safe with respect to `fifo_getBytes()`.
 *
 * @param[in]  fifo      FIFO.
 * @param[in]  fd        File descriptor from which to obtain bytes.
 * @param[in]  maxBytes  Maximum number of bytes to transfer.
 * @param[out] nbytes    Actual number of bytes transferred.
 * @retval     0         Success. `*nbytes` is set.
 * @retval     1         Usage error. `log_start()` called.
 * @retval     2         System error. `log_start()` called.
 * @retval     3         `fifo_close()` was called.
 */
int
fifo_readFd(
        Fifo* const restrict   fifo,
        const int              fd,
        const size_t           maxBytes,
        size_t* const restrict nbytes)
{
    int status;

    fifo_readLock(fifo);

    if (fifo_isInvalidSize(fifo, maxBytes)) {
        status = 1;
    }
    else {
        fifo->fullCount += fifo_waitWhile(fifo, fifo_isOpenAndNotWritable,
                maxBytes);

        if (fifo->isClosed) {
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
 * Removes bytes from a FIFO. Blocks while insufficient data exists and
 * `fifo_close()` hasn't been called. Returns data if possible -- even if
 * `fifo_close()` has been called. This function is thread-safe with respect to
 * `fifo_readFd()`.
 *
 * @param[in] fifo    FIFO.
 * @param[in] buf     Buffer into which to put bytes.
 * @param[in[ nbytes  Number of bytes to remove.
 * @retval    0       Success.
 * @retval    1       Usage error. `log_start()` called.
 * @retval    3       `fifo_close()` was called and insufficient data exists.
 */
int
fifo_getBytes(
    Fifo* const             fifo,
    unsigned char* const    buf,
    const size_t            nbytes)
{
    int status;

    fifo_readLock(fifo);

    if (fifo_isInvalidSize(fifo, nbytes)) {
        status = 1;
    }
    else {
        (void)fifo_waitWhile(fifo, fifo_isOpenAndNotReadable, nbytes);

        if (fifo_availableForReading(fifo) < nbytes) {
            status = 3;
        }
        else {
            fifo_removeBytes(fifo, buf, nbytes);
            fifo_signal(fifo);
            status = 0;
        }
    } // `nbytes` <= fifo-capacity

    fifo_unlock(fifo);

    return status;
}

/**
 * Returns the number of times `fifo_readFd()` had to wait until sufficient
 * space was available in a FIFO and resets the counter.
 *
 * @param[in] fifo  The FIFO.
 * @return          The number of times `fifo_readFd()` had to wait.
 */
size_t
fifo_getFullCount(
        Fifo* const fifo)
{
    size_t count;

    fifo_writeLock(fifo);
    count = fifo->fullCount;
    fifo->fullCount = 0;
    fifo_unlock(fifo);

    return count;
}

/**
 * Closes a FIFO.
 *
 * This function is thread-safe and idempotent.
 *
 * @param[in] fifo  FIFO to be closed.
 */
void
fifo_close(
    Fifo* const fifo)       /**< [in/out] Pointer to FIFO */
{
    fifo_writeLock(fifo);
    fifo->isClosed = true;
    fifo_signal(fifo);
    fifo_unlock(fifo);
}
