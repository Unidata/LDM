/**
 * Implements a fixed-size FIFO that's filled from a file descriptor. This
 * implementation is constrained to *one* writer thread and *one* reader thread.
 * Any other usage results in undefined behavior.
 *
 * Copyright 2019, University Corporation for Atmospheric Research. See file
 * COPYRIGHT in the top-level source-directory for copying and redistribution
 * conditions.
 */
#include "config.h"

#include "log.h"
#include "fifo.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

struct fifo {
    unsigned char*        buf;          ///< Pointer to start of buffer
    size_t                nextWrite;    ///< Offset to next byte to write
    size_t                nbytes;       ///< Number of bytes in the buffer
    size_t                size;         ///< Size of buffer in bytes
    /**
     * Number of times `fifo_readFd()` had to wait until sufficient space was
     * available.
     */
    size_t                fullCount;
    pthread_mutex_t       mutex;        ///< Mutual exclusion object
    pthread_cond_t        cond;         ///< Condition variable
    int                   fd;           ///< Input file descriptor
    volatile sig_atomic_t isClosed;     ///< FIFO is closed?
};

/**
 * Initializes a FIFO
 *
 * @param[out] fifo     FIFO to be initialized
 * @param[in]  fd       Input file descriptor
 * @param[in]  npages   FIFO size in pages
 * @retval     `true`   Success
 * @retval     `false`  Failure. `log_add()` called.
 */
static bool
fifo_init(
        Fifo* const  fifo,
        const int    fd,
        const size_t npages)
{
    log_assert(fifo);
    log_assert(fd >= 0);
    log_assert(npages);

    int                  status;
    const long           pagesize = sysconf(_SC_PAGESIZE);
    const size_t         size = npages*pagesize;
    unsigned char* const buf = log_malloc(size, "FIFO buffer");

    if (buf == NULL) {
        status = 1;
    }
    else {
        pthread_mutexattr_t mutexAttr;

        if ((status = pthread_mutexattr_init(&mutexAttr)) != 0) {
            log_add_errno(status, "Couldn't initialize mutex attributes");
        }
        else {
            (void)pthread_mutexattr_settype(&mutexAttr,
                    PTHREAD_MUTEX_ERRORCHECK);
            (void)pthread_mutexattr_setprotocol(&mutexAttr,
                    PTHREAD_PRIO_INHERIT);

            status = pthread_mutex_init(&fifo->mutex, &mutexAttr);

            if (status) {
                log_add_errno(status, "Couldn't initialize mutex");
            }
            else {
                status = pthread_cond_init(&fifo->cond, NULL);

                if (status) {
                    log_add_errno(status,
                            "Couldn't initialize condition variable");
                }
                else {
                    fifo->fd = fd;
                    fifo->buf = buf;
                    fifo->nextWrite = 0;
                    fifo->nbytes = 0;  // indicates startup
                    fifo->size = size;
                    fifo->isClosed = false;
                    fifo->fullCount = 0;
                    status = 0; // success
                } // `fifo->cond` initialized

                if (status)
                    (void)pthread_mutex_destroy(&fifo->mutex);
            } // "fifo->mutex" initialized

            (void)pthread_mutexattr_destroy(&mutexAttr);
        } // "mutexAttr" initialized

        if (status)
            free(buf);
    } // `buf` allocated

    return status == 0;
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
    free(fifo->buf);
}

/**
 * Locks a FIFO.
 *
 * @param[in] fifo  The FIFO to be locked.
 */
static inline void
fifo_lock(
        Fifo* const fifo)
{
    (void)pthread_mutex_lock(&fifo->mutex);
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
    (void)pthread_mutex_unlock(&fifo->mutex);
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
 * Blocks while a FIFO is in a particular state.
 *
 * @pre                   The FIFO is locked.
 * @param[in]  fifo       The FIFO.
 * @param[in]  whilePred  The predicate that defines the state.
 * @param[in]  nbytes     Number of bytes.
 * @retval     0          No waiting occurred.
 * @retval     1          Waiting occurred.
 * @post                  The FIFO is locked.
 */
static int
fifo_waitWhile(
        Fifo* const restrict fifo,
        bool         (*const whilePred)(Fifo* fifo, size_t nbytes),
        const size_t         nbytes)
{
    int didWait = 0;

    while (whilePred(fifo, nbytes)) {
        /*
         * Failure isn't possible because `fifo->cond` and `fifo->mutex` are valid
         * and the mutex is locked by the current thread.
         */
        (void)pthread_cond_wait(&fifo->cond, &fifo->mutex);
       didWait = 1;
    }

    return didWait;
}

/**
 * Returns the capacity of a FIFO.
 *
 * @pre             FIFO is locked.
 * @param[in] fifo  FIFO.
 * @return          Capacity of FIFO in bytes.
 * @post            FIFO is locked.
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
 * @pre             FIFO is locked.
 * @param[in] fifo  FIFO.
 * @return          Amount of available space in bytes.
 * @post            FIFO is locked.
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
 * @pre             FIFO is locked.
 * @param[in] fifo  FIFO.
 * @return          Amount of available data in bytes.
 * @post            FIFO is locked.
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
 * @pre               FIFO is locked.
 * @param[in] fifo    The FIFO.
 * @param[in] nbytes  The number of bytes to write.
 * @retval    false   The FIFO is closed or sufficient space exists.
 * @retval    true    The FIFO is open and insufficient space exists.
 * @post              FIFO is locked.
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
 * @pre               FIFO is locked.
 * @param[in] fifo    The FIFO.
 * @param[in] nbytes  The desired number of bytes to read.
 * @retval    false   The FIFO is closed or sufficient data exists.
 * @retval    true    The FIFO is open and insufficient data exists.
 * @post              FIFO is locked.
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
 * @pre               FIFO is locked.
 * @param[in] fifo    The FIFO.
 * @param[in] nbytes  The number of bytes.
 * @retval    true    The number of bytes is theoretically impossible.
 *                    `log_add()` called.
 * @retval    false   The number of bytes is theoretically possible.
 * @pre               The FIFO is locked.
 * @post              FIFO is locked.
 */
static bool
fifo_isInvalidSize(
        const Fifo* const fifo,
        const size_t      nbytes)
{
    if (nbytes <= fifo_capacity(fifo))
        return false;

    log_add("Request-amount is greater than FIFO capacity: %lu > %lu",
            (unsigned long)nbytes, (unsigned long)fifo_capacity(fifo));
    return true;
}

/**
 * Reads bytes from a FIFO's file descriptor into the FIFO.
 *
 * @pre                  FIFO is locked and not closed
 * @pre                  `availableForWriting(fifo) >= maxBytes`
 * @param[in]  fifo      FIFO.
 * @param[in]  maxBytes  Maximum number of bytes to transfer.
 * @param[out] nbytes    Actual number of bytes transferred.
 * @retval     0         Success.
 * @retval     2         O/S error. `log_add()` called.
 * @post                 FIFO is locked.
 */
static inline int // `inline` because only called in one place
fifo_transferFromFd(
        Fifo* const restrict   fifo,
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
        nb = read(fifo->fd, ptr, maxBytes);
    }
    else {
        struct iovec iov[2];

        iov[0].iov_base = ptr;
        iov[0].iov_len = extent;
        iov[1].iov_base = buf;
        iov[1].iov_len = maxBytes - extent;

        nb = readv(fifo->fd, iov, 2);
    }

    if (-1 == nb) {
        if (fifo->isClosed) {
            log_info("FIFO is closed");
            *nbytes = 0;
            status = 0;
        }
        else {
            log_syserr("Couldn't read up to %lu bytes from file descriptor %d",
                    (unsigned long)maxBytes, fifo->fd);
            status = 2;
        }

        fifo_lock(fifo); // was unlocked => deadlock not possible
    }
    else {
        *nbytes = nb;
        status = 0;
        fifo_lock(fifo); // was unlocked => deadlock not possible
        fifo->nbytes += nb;
        fifo->nextWrite = (fifo->nextWrite + nb) % fifo->size;
    }

    return status;
}

/**
 * Removes bytes from a FIFO.
 *
 * @pre               FIFO is locked
 * @pre               `fifo_availableForReading(fifo) >= nbytes`
 * @param[in] fifo    FIFO.
 * @param[in] buf     Buffer into which to put bytes.
 * @param[in] nbytes  Number of bytes to remove.
 * @post              FIFO is locked
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

    fifo_lock(fifo);
    fifo->nbytes -= nbytes;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a FIFO.
 *
 * @param[in] fd      Input file descriptor
 * @param[in] npages  FIFO size in pages
 * @retval    NULL    Failure. `log_add()` called.
 * @return            FIFO queue
 * @threadsafety      Safe
 */
Fifo*
fifo_new(
        const int    fd,
        const size_t npages)
{
    log_assert(fd >= 0);
    log_assert(npages > 0);

    Fifo* fifo = log_malloc(sizeof(Fifo), "FIFO");

    if (fifo) {
        if (!fifo_init(fifo, fd, npages)) {
            log_add("Couldn't initialize FIFO");
            free(fifo);
            fifo = NULL;
        }
    } // `fifo` allocated

    return fifo;
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
 * Reads bytes from a FIFO's file descriptor into the FIFO. Blocks until space
 * is available. This function is thread-safe with respect to `fifo_getBytes()`
 * and should continue to be called after `fifo_close()` has been called.
 *
 * @param[in]  fifo      FIFO.
 * @param[in]  maxBytes  Maximum number of bytes to transfer.
 * @param[out] nbytes    Actual number of bytes transferred.
 * @retval     0         Success. `*nbytes` is set.
 * @retval     1         Usage error. `log_add()` called.
 * @retval     2         System error. `log_add()` called.
 * @retval     3         `fifo_close()` was called.
 * @asyncsignalsafety    Unsafe
 * @threadsafety         Safe
 * @see                  fifo_getBytes()
 * @see                  fifo_close()
 */
int
fifo_readFd(
        Fifo* const restrict   fifo,
        const size_t           maxBytes,
        size_t* const restrict nbytes)
{
    int status;

    fifo_lock(fifo);
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
                status = fifo_transferFromFd(fifo, maxBytes, nbytes);

                if (status || *nbytes == 0)
                    fifo->isClosed = 1;
            }

            fifo_signal(fifo);
        }
    fifo_unlock(fifo);

    return status;
}

/**
 * Removes bytes from a FIFO. Blocks while insufficient data exists and
 * `fifo_close()` hasn't been called. Returns data if possible -- even if
 * `fifo_close()` has been called. This function is thread-safe with respect to
 * `fifo_readFd()` and should continue to be called after `fifo_close()` has
 * been called.
 *
 * @param[in] fifo     FIFO.
 * @param[in] buf      Buffer into which to put bytes.
 * @param[in[ nbytes   Number of bytes to remove.
 * @retval    0        Success.
 * @retval    1        Usage error. `log_add()` called.
 * @retval    3        `fifo_close()` was called and insufficient data exists.
 * @asyncsignalsafety  Unsafe
 * @threadsafety       Safe
 * @see                fifo_readFd()
 * @see                fifo_close()
 */
int
fifo_getBytes(
    Fifo* const             fifo,
    unsigned char* const    buf,
    const size_t            nbytes)
{
    int status;

    fifo_lock(fifo);

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

    fifo_lock(fifo);
    count = fifo->fullCount;
    fifo->fullCount = 0;
    fifo_unlock(fifo);

    return count;
}

/**
 * Closes a FIFO. Idempotent. The caller should continue to call
 * `fifo_getBytes()` to avoid `fifo_readFd()` waiting indefinitely.
 *
 * @param[in] fifo     FIFO to be closed.
 * @threadsafety       Safe
 * @asyncsignalsafety  Safe
 * @see                fifo_getBytes()
 * @see                fifo_readFd()
 */
void
fifo_close(
    Fifo* const fifo)
{
    fifo->isClosed = true;
    (void)shutdown(fifo->fd, SHUT_RDWR);
    (void)close(fifo->fd);
}
