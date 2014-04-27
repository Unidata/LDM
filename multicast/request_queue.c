/**
 * Copyright 2014 University Corporation for Atmospheric Research.
 * All rights reserved. See file COPYRIGHT in the top-level source-directory
 * for legal conditions.
 *
 *   @file request_queue.c
 * @author Steven R. Emmerson
 *
 * This file implements a queue of requests for files missed by the VCMTP layer.
 */

#include "config.h"

#include "log.h"
#include "request_queue.h"

#include <errno.h>

/**
 * The definition of a request-queue object.
 */
struct request_queue {
    // TODO
};

/**
 * Returns a new request-queue.
 *
 * @retval NULL  Failure. \c log_add() called.
 * @return       Pointer to a new request-queue. The client should call \c
 *               rq_free() when it is no longer needed.
 */
RequestQueue* rq_new(void)
{
    RequestQueue* rq = LOG_MALLOC(sizeof(RequestQueue),
            "missed-file request-queue");
    return rq;
}

/**
 * Frees a request-queue.
 *
 * @param[in] rq  Pointer to the request-queue to be freed or NULL.
 */
void rq_free(
    RequestQueue* const rq)
{
    free(rq);
}

/**
 * Adds a request to a queue.
 *
 * @param[in,out] rq   Pointer to the request-queue to which to add a request.
 * @param[in]     sig  Pointer to the LDM signature of the data-product to be
 *                     requested.
 */
void rq_add(
    RequestQueue* const     rq,
    const signaturet* const sig)
{
    // TODO
}
