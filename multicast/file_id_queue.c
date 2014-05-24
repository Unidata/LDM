/**
 * Copyright 2014 University Corporation for Atmospheric Research.
 * All rights reserved. See file COPYRIGHT in the top-level source-directory
 * for legal conditions.
 *
 *   @file file_id_queue.c
 * @author Steven R. Emmerson
 *
 * This file implements a thread-safe queue of VCMTP file identifiers.
 */

#include "config.h"

#include "ldm.h"
#include "log.h"
#include "file_id_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

/**
 * The type of an entry in a file-identifier queue.
 */
typedef struct entry {
    struct entry* next;   /* points to the next entry towards the tail */
    VcmtpFileId   fileId; /* VCMTP identifier of the file */
} Entry;

/**
 * Returns a new entry.
 *
 * @param[in] fileId  VCMTP file identifier of the file.
 * @retval    NULL    Error. \c log_add() called.
 * @return            Pointer to the new entry. The client should call \c
 *                    entry_fin() when it is no longer needed.
 */
static Entry*
entry_new(
    const VcmtpFileId fileId)
{
    Entry* entry = LOG_MALLOC(sizeof(Entry), "file-identifier queue-entry");

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
    Entry* const         entry,
    VcmtpFileId* const   fileId)
{
    *fileId = entry->fileId;
    entry_free(entry);
}

/**
 * The definition of a file-identifier queue object.
 */
struct file_id_queue {
    Entry*          head;
    Entry*          tail;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
};

/**
 * Returns a new file-identifier queue.
 *
 * @retval NULL  Failure. \c log_add() called.
 * @return       Pointer to a new file-identifier queue. The client should call
 *               \c rq_free() when it is no longer needed.
 */
FileIdQueue*
fiq_new(void)
{
    FileIdQueue* rq = LOG_MALLOC(sizeof(FileIdQueue),
            "missed-file file-identifier queue");

    if (rq) {
        if (pthread_mutex_init(&rq->mutex, NULL)) {
            LOG_SERROR0("Couldn't initialize mutex of file-identifier queue");
            free(rq);
            rq = NULL;
        }
        else {
            if (pthread_cond_init(&rq->cond, NULL)) {
                LOG_SERROR0("Couldn't initialize condition-variable of file-identifier queue");
                (void)pthread_mutex_destroy(&rq->mutex);
                free(rq);
                rq = NULL;
            }
            else {
                rq->head = rq->tail = NULL;
            }
        } /* "rq->mutex" allocated */
    } /* "rq" allocated */

    return rq;
}

/**
 * Frees a file-identifier queue. Accessing the queue after calling this function
 * results in undefined behavior.
 *
 * @param[in] rq  Pointer to the file-identifier queue to be freed or NULL.
 */
void
fiq_free(
    FileIdQueue* const rq)
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
        (void)pthread_cond_destroy(&rq->cond);
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
fiq_addTail(
    FileIdQueue* const rq,
    Entry* const        entry)
{
    (void)pthread_mutex_lock(&rq->mutex);

    if (!rq->head)
        rq->head = entry;
    if (rq->tail)
        rq->tail->next = entry;
    rq->tail = entry;

    (void)pthread_cond_broadcast(&rq->cond);
    (void)pthread_mutex_unlock(&rq->mutex);
}

/**
 * Removes the entry at the head of the queue. Blocks until that entry exists.
 *
 * @param[in,out] rq         Pointer to the queue. Not checked.
 * @return                   Pointer to what was the head entry.
 */
static Entry*
fiq_removeHead(
    FileIdQueue* const rq)
{
    Entry* entry;

    (void)pthread_mutex_lock(&rq->mutex);

    while (rq->head == NULL)
        pthread_cond_wait(&rq->cond, &rq->mutex);

    entry = rq->head;
    rq->head = entry->next;
    if (rq->tail == entry)
        rq->tail = NULL;

    (void)pthread_mutex_unlock(&rq->mutex);

    return entry;
}

/**
 * Adds a file-identifier to a queue.
 *
 * @param[in,out] rq      Pointer to the file-identifier queue to which to add a
 *                        file-identifier.
 * @param[in]     fileId  VCMTP file identifier of the data-product.
 * @retval        0       Success.
 * @retval        EINVAL  @code{rq == NULL}. \c log_add() called.
 * @retval        ENOMEM  Out of memory. \c log_add() called.
 */
int
fiq_add(
    FileIdQueue* const rq,
    const VcmtpFileId   fileId)
{
    Entry* entry;

    if (!rq)
        return EINVAL;

    entry = entry_new(fileId);
    if (!entry)
        return ENOMEM;

    fiq_addTail(rq, entry);

    return 0;
}

/**
 * Removes and returns the file-identifier at the head of the file-identifier
 * queue. Blocks until such an entry is available.
 *
 * @param[in,out] rq      Pointer to the file-identifier queue.
 * @param[out]    fileId  Pointer to the VCMTP file identifier to be set to
 *                        that of the entry.
 * @retval        0       Success. \c *fileId is set.
 * @retval        EINVAL  @code{rq == NULL || fileId == NULL}.
 */
int
fiq_remove(
    FileIdQueue* const rq,
    VcmtpFileId* const  fileId)
{
    Entry* entry;

    if (!rq || !fileId)
        return EINVAL;

    entry = fiq_removeHead(rq);
    entry_fin(entry, fileId);

    return 0;
}
