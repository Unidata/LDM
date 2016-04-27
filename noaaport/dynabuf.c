/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: dynabuf.c
 * @author: Steven R. Emmerson
 *
 * This file implements a dynamic buffer.
 */

#include "config.h"

#include "dynabuf.h"
#include "log.h"

#include <stddef.h>
#include <string.h>

struct dynabuf {
    size_t   max;   ///< Current size of `buf`
    size_t   used;  ///< Number of bytes used
    uint8_t* buf;   ///< Buffer
};

/**
 * Initializes a dynamic buffer
 *
 * @param[out] dynabuf            Dynamic buffer to be initialized
 * @param[in]  nbytes             Initial capacity of buffer in bytes
 * @retval     0                  Success
 * @retval     DYNABUF_STATUS_NOMEM  Out of memory. log_add() called.
 */
static dynabuf_status_t dynabuf_init(
        dynabuf_t* const dynabuf,
        const size_t     nbytes)
{
    int   status;
    dynabuf->buf = log_malloc(nbytes, "dynamic buffer's buffer");
    if (dynabuf->buf == NULL) {
        status = DYNABUF_STATUS_NOMEM;
    }
    else {
        (void)memset(dynabuf->buf, 0, nbytes); // To shut-up valgrind(1)
        dynabuf->max = nbytes;
        dynabuf->used = 0;
        status = 0;
    }
    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 *
 * @param dynabuf
 * @param nbytes
 * @retval 0                     Success. `*dyabuf` is set.
 * @retval DYNABUF_STATUS_INVAL  `dynabuf == NULL`. log_add(called).
 * @retval DYNABUF_STATUS_NOMEM  Out of memory. log_add(called).
 */
dynabuf_status_t dynabuf_new(
        dynabuf_t** const dynabuf,
        const size_t      nbytes)
{
    int status;
    if (dynabuf == NULL) {
        log_add("NULL argument: dynabuf=%p", dynabuf);
        status = DYNABUF_STATUS_INVAL;
    }
    else {
        dynabuf_t* obj = log_malloc(sizeof(dynabuf_t), "dynamic-buffer object");
        if (obj == NULL) {
            status = DYNABUF_STATUS_NOMEM;
        }
        else {
            status = dynabuf_init(obj, nbytes);
            if (status) {
                log_add("Couldn't initialize dynamic-buffer object");
                free(obj);
            }
            else {
                *dynabuf = obj;
            }
        }
    }
    return status;
}

/**
 * Frees a DYNABUF object.
 *
 * @param[in] dynabuf  Dynamic-buffer object to be freed or `NULL`.
 */
void dynabuf_free(
        dynabuf_t* const dynabuf)
{
    if (dynabuf) {
        dynabuf_fini(dynabuf);
        free(dynabuf);
    }
}

/**
 * Ensures that a given number of bytes can be added to a dynamic buffer.
 *
 * @param[in,out] dynabuf            Dynamic buffer
 * @param[in]     nbytes             Number of bytes
 * @retval        0                  Success
 * @retval        DYNABUF_STATUS_NOMEM  Out of memory. log_add() called.
 */
dynabuf_status_t dynabuf_reserve(
        dynabuf_t* const dynabuf,
        const size_t     nbytes)
{
    int          status;
    const size_t max = dynabuf->used + nbytes;
    if (max <= dynabuf->max) {
        status = 0;
    }
    else {
        uint8_t* const buf = realloc(dynabuf->buf, max);
        if (buf == NULL) {
            log_add("Couldn't re-allocate %zu bytes for dynamic buffer's "
                    "buffer");
            status = DYNABUF_STATUS_NOMEM;
        }
        else {
            dynabuf->buf = buf;
            dynabuf->max = max;
            status = 0;
        }
    }
    return status;
}

/**
 * Appends to a dynamic buffer. The buffer will grow if necessary.
 *
 * @param[in,out] dynabuf            Dynamic buffer
 * @param[in]     buf                Bytes to be added
 * @param[in]     nbytes             Number of bytes
 * @retval        0                  Success
 * @retval        DYNABUF_STATUS_NOMEM  Out of memory. log_add() called.
 */
dynabuf_status_t dynabuf_add(
        dynabuf_t* const restrict     dynabuf,
        const uint8_t* const restrict buf,
        const size_t                  nbytes)
{
    int status = dynabuf_reserve(dynabuf, 2 * (dynabuf->used + nbytes));
    if (status == 0) {
        (void)memcpy(dynabuf->buf + dynabuf->used, buf, nbytes);
        dynabuf->used += nbytes;
    }
    return status;
}

/**
 * Appends a variable number of a given byte to a dynamic buffer.
 *
 * @param[in,out] dynabuf            Dynamic buffer
 * @param[in]     byte               Value to be appended
 * @param[in]     nbytes             Number of times to append `byte`
 * @retval        0                  Success
 * @retval        DYNABUF_STATUS_NOMEM  Out of memory. log_add() called.
 */
dynabuf_status_t dynabuf_set(
        dynabuf_t* const restrict dynabuf,
        const int                 byte,
        const size_t              nbytes)
{
    int status = dynabuf_reserve(dynabuf, nbytes);
    if (status == 0) {
        (void)memset(dynabuf->buf+dynabuf->used, byte, nbytes);
        dynabuf->used += nbytes;
    }
    return status;
}

/**
 * Returns the bytes of a dynamic buffer.
 *
 * @param[in] dynabuf  Dynamic buffer
 * @return             Bytes of `dynabuf`
 */
uint8_t* dynabuf_get_buf(
        dynabuf_t* const dynabuf)
{
    return dynabuf->buf;
}

/**
 * Returns the number of bytes used in a dynamic buffer.
 *
 * @param[in] dynabuf  Dynamic buffer
 * @return             Number of bytes used in `dynabuf`
 */
size_t dynabuf_get_used(
        const dynabuf_t* const dynabuf)
{
    return dynabuf->used;
}

/**
 * Sets the number of bytes used in a dynamic buffer.
 *
 * @param[out] dynabuf  Dynamic buffer
 * @param[in]  nbytes   Number of bytes used in `dynabuf`
 */
void dynabuf_set_used(
        dynabuf_t* const dynabuf,
        const size_t     nbytes)
{
    dynabuf->used = nbytes;
}

/**
 * Clears a dynamic buffer.
 *
 * @param[in,out] dynabuf  Dynamic buffer to be cleared
 */
void dynabuf_clear(
        dynabuf_t* const dynabuf)
{
    dynabuf->used = 0;
}

/**
 * Finalizes a dynamic buffer.
 *
 * @param[in,out] dynabuf  Dynamic buffer
 */
void dynabuf_fini(
        dynabuf_t* dynabuf)
{
    free(dynabuf->buf);
}
