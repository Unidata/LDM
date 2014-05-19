/**
 * Copyright 2014 University Corporation for Atmospheric Research.
 * All rights reserved. See file COPYRIGHT in the top-level source-directory
 * for legal conditions.
 *
 *   @file request_queue.c
 * @author Steven R. Emmerson
 *
 * This file implements a queue of requests for files missed by the VCMTP layer.
 *
 * The implementation is thread-safe.
 */

#include "config.h"

#include "ldm.h"
#include "log.h"
#include "request_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

/**
 * The type of an entry in a request-queue.
 */
typedef struct entry {
    struct entry* next;   /* points to the next entry towards the tail */
    VcmtpFileId        fileId; /* VCMTP file identifier of the file to be requested */
} Entry;

/**
 * Returns a new entry.
 *
 * @param[in] fileId  VCMTP file identifier of the file to be requested.
 * @retval    NULL    Error. \c log_add() called.
 * @return            Pointer to the new entry. The client should call \c
 *                    entry_fin() when it is no longer needed.
 */
static Entry*
entry_new(
    const VcmtpFileId fileId)
{
    Entry* entry = LOG_MALLOC(sizeof(Entry), "request-queue entry");

    if (entry) {
        entry->fileId = fileId;
        entry->next = NULL;
    }

    return entry;
}

/**
 * Frees an entry.
 *
 * @param[in,out] entry  Pointer to the entry to be freed.
 */
static void
entry_free(
    Entry* const entry)
{
    free(entry);
}

/**
 * Finalizes an entry.
 *
 * @param[in,out] entry   Pointer to the entry to be finalized. Not checked.
 * @param[out]    fileId  Pointer to the VCMTP file identifier to be set to the
 *                        value of the entry. Not checked.
 */
static void
entry_fin(
    Entry* const    entry,
    VcmtpFileId* const   fileId)
{
    *fileId = entry->fileId;
    entry_free(entry);
}

/**
 * The definition of a request-queue object.
 */
struct request_queue {
    Entry*          head;
    Entry*          tail;
    pthread_mutex_t mutex;
};

/**
 * Returns a new request-queue.
 *
 * @retval NULL  Failure. \c log_add() called.
 * @return       Pointer to a new request-queue. The client should call \c
 *               rq_free() when it is no longer needed.
 */
RequestQueue*
rq_new(void)
{
    RequestQueue* rq = LOG_MALLOC(sizeof(RequestQueue),
            "missed-file request-queue");
    if (rq) {
        rq->head = rq->tail = NULL;

        if (pthread_mutex_init(&rq->mutex, NULL)) {
            LOG_ADD0("Couldn't initialize mutex of request-queue");
            free(rq);
            rq = NULL;
        }
    }

    return rq;
}

/**
 * Frees a request-queue. Accessing the queue after calling this function
 * results in undefined behavior.
 *
 * @param[in] rq  Pointer to the request-queue to be freed or NULL.
 */
void
rq_free(
    RequestQueue* const rq)
{
    if (rq) {
        Entry* entry;

        (void)pthread_mutex_lock(&rq->mutex);

        entry = rq->head;

        while (entry) {
            Entry* next = entry->next;

            entry_free(entry);
            entry = next;
        }

        (void)pthread_mutex_unlock(&rq->mutex);
        (void)pthread_mutex_destroy(&rq->mutex);
        free(rq);
    }
}

/**
 * Adds an entry to the tail of the queue.
 *
 * @param[in,out] rq     Pointer to the queue. Not checked.
 * @param[in]     entry  Pointer to the entry to be added. Not checked.
 */
static void
rq_addTail(
    RequestQueue* const rq,
    Entry* const        entry)
{
    (void)pthread_mutex_lock(&rq->mutex);

    if (!rq->head)
        rq->head = entry;
    if (rq->tail)
        rq->tail->next = entry;
    rq->tail = entry;

    (void)pthread_mutex_unlock(&rq->mutex);
}

/**
 * Removes the entry at the head of the queue.
 *
 * @param[in,out] rq    Pointer to the queue. Not checked.
 * @retval        NULL  The queue is empty.
 * @return              Pointer to the entry that was at the head of the queue.
 */
static Entry*
rq_removeHead(
    RequestQueue* const rq)
{
    Entry* entry;

    (void)pthread_mutex_lock(&rq->mutex);

    entry = rq->head;
    if (entry) {
        rq->head = entry->next;
        if (rq->tail == entry)
            rq->tail = NULL;
    }

    (void)pthread_mutex_unlock(&rq->mutex);

    return entry;
}

/**
 * Adds a request to a queue.
 *
 * @param[in,out] rq      Pointer to the request-queue to which to add a
 *                        request.
 * @param[in]     fileId  VCMTP file identifier of the data-product to be
 *                        requested.
 * @retval        0       Success.
 * @retval        EINVAL  @code{rq == NULL}. \c log_add() called.
 * @retval        ENOMEM  Out of memory. \c log_add() called.
 */
int
rq_add(
    RequestQueue* const rq,
    const VcmtpFileId   fileId)
{
    Entry* entry;

    if (!rq)
        return EINVAL;

    entry = entry_new(fileId);
    if (!entry)
        return ENOMEM;

    rq_addTail(rq, entry);

    return 0;
}

/**
 * Removes and returns the request at the head of the request-queue.
 *
 * @param[in,out] rq      Pointer to the request-queue.
 * @param[out]    fileId  Pointer to the VCMTP file identifier to be set to
 *                        that of the entry.
 * @retval        0       Success. \c *fileId is set.
 * @retval        EINVAL  @code{rq == NULL || fileId == NULL}. \c log_add() called.
 * @retval        ENOENT  The request-queue is empty.
 */
int
rq_remove(
    RequestQueue* const rq,
    VcmtpFileId* const       fileId)
{
    Entry* entry;

    if (!rq || !fileId)
        return EINVAL;

    entry = rq_removeHead(rq);
    if (!entry)
        return ENOENT;

    entry_fin(entry, fileId);

    return 0;
}
