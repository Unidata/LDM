/* DO NOT EDIT THIS FILE. It was created by extractDecls */
/*
 *   Copyright 2011, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */

#ifndef FIFO_H
#define FIFO_H

#include <stddef.h>

typedef struct fifo     Fifo;

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
        const size_t npages);

/**
 * Frees a FIFO.
 *
 * @param[in] fifo  Pointer to FIFO to be freed.
 */
void
fifo_free(
        Fifo* const fifo);

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
        size_t* const restrict nbytes);

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
    const size_t            nbytes);

/**
 * Returns the number of times `fifo_readFd()` had to wait until sufficient
 * space was available in a FIFO and resets the counter.
 *
 * @param[in] fifo  The FIFO.
 * @return          The number of times `fifo_readFd()` had to wait.
 */
size_t
fifo_getFullCount(
        Fifo* const fifo);

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
    Fifo* const fifo);

#endif
