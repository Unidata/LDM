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
    int             isCancelled;
};

/**
 * Returns a new file-identifier queue.
 *
 * @retval NULL  Failure. \c log_add() called.
 * @return       Pointer to a new file-identifier queue. The client should call
 *               \c fiq_free() when it is no longer needed.
 */
FileIdQueue*
fiq_new(void)
{
    FileIdQueue* fiq = LOG_MALLOC(sizeof(FileIdQueue),
            "missed-file file-identifier queue");

    if (fiq) {
        if (pthread_mutex_init(&fiq->mutex, NULL)) {
            LOG_SERROR0("Couldn't initialize mutex of file-identifier queue");
            free(fiq);
            fiq = NULL;
        }
        else {
            if (pthread_cond_init(&fiq->cond, NULL)) {
                LOG_SERROR0("Couldn't initialize condition-variable of file-identifier queue");
                (void)pthread_mutex_destroy(&fiq->mutex);
                free(fiq);
                fiq = NULL;
            }
            else {
                fiq->head = fiq->tail = NULL;
                fiq->isCancelled = 0;
            }
        } /* "fiq->mutex" allocated */
    } /* "fiq" allocated */

    return fiq;
}

static void
fiq_lock(
    FileIdQueue* const fiq)
{
    (void)pthread_mutex_lock(&fiq->mutex);
}

static void
fiq_unlock(
    FileIdQueue* const fiq)
{
    (void)pthread_mutex_unlock(&fiq->mutex);
}

static void
fiq_cleanup_unlock(
    void* const arg)
{
    fiq_unlock((FileIdQueue*)arg);
}

/**
 * Frees a file-identifier queue. Accessing the queue after calling this function
 * results in undefined behavior.
 *
 * @param[in] fiq  Pointer to the file-identifier queue to be freed or NULL.
 */
void
fiq_free(
    FileIdQueue* const fiq)
{
    if (fiq) {
        Entry* entry = fiq->head;

        while (entry) {
            Entry* next = entry->next;

            entry_free(entry);
            entry = next;
        }

        (void)pthread_cond_destroy(&fiq->cond);
        (void)pthread_mutex_destroy(&fiq->mutex);
        free(fiq);
    }
}

/**
 * Adds an entry to the tail of the queue. Does nothing if the queue has been
 * canceled.
 *
 * @param[in,out] fiq        Pointer to the queue. Not checked.
 * @param[in]     entry      Pointer to the entry to be added. Not checked.
 * @retval        0          Success.
 * @retval        ECANCELED  The queue has been canceled.
 */
static int
fiq_addTail(
    FileIdQueue* const fiq,
    Entry* const        entry)
{
    int status;

    fiq_lock(fiq);
    if (fiq->isCancelled) {
        status = ECANCELED;
    }
    else {
        if (!fiq->head)
            fiq->head = entry;
        if (fiq->tail)
            fiq->tail->next = entry;
        fiq->tail = entry;
        status = 0;

        (void)pthread_cond_broadcast(&fiq->cond);
    }
    fiq_unlock(fiq);

    return status;
}

/**
 * Removes the entry at the head of the queue. Blocks until that entry exists or
 * the queue is canceled.
 *
 * This is a thread cancellation point.
 *
 * @param[in,out] fiq        Pointer to the queue. Not checked.
 * @param[out]    entry      Pointer to the pointer to what was the head entry.
 * @retval        0          Success.
 * @retval        ECANCELED  The queue has been canceled.
 */
static int
fiq_removeHead(
    FileIdQueue* const fiq,
    Entry** const      entry)
{
    int    status;

    fiq_lock(fiq);
    pthread_cleanup_push(fiq_cleanup_unlock, fiq); // to ensure mutex release

    while (fiq->head == NULL && !fiq->isCancelled)
        pthread_cond_wait(&fiq->cond, &fiq->mutex); // cancellation point

    if (fiq->isCancelled) {
        status = ECANCELED;
    }
    else {
        Entry* ent = fiq->head;

        fiq->head = ent->next;
        if (fiq->tail == ent)
            fiq->tail = NULL;
        *entry = ent;
        status = 0;
    }

    pthread_cleanup_pop(1); // releases mutex

    return status;
}

/**
 * Adds a file-identifier to a queue.
 *
 * @param[in,out] fiq        Pointer to the file-identifier queue to which to
 *                           add a file-identifier.
 * @param[in]     fileId     VCMTP file identifier of the data-product.
 * @retval        0          Success.
 * @retval        EINVAL     @code{fiq == NULL}. \c log_add() called.
 * @retval        ENOMEM     Out of memory. \c log_add() called.
 * @retval        ECANCELED  The queue has been canceled.
 */
int
fiq_add(
    FileIdQueue* const fiq,
    const VcmtpFileId   fileId)
{
    Entry* entry;
    int    status;

    if (!fiq)
        return EINVAL;

    entry = entry_new(fileId);
    if (!entry)
        return ENOMEM;

    status = fiq_addTail(fiq, entry);
    if (status)
        entry_free(entry);

    return status;
}

/**
 * Removes and returns the file-identifier at the head of the file-identifier
 * queue. Blocks until such an entry is available or the queue is canceled.
 *
 * This is a thread cancellation point.
 *
 * @param[in,out] fiq        Pointer to the file-identifier queue.
 * @param[out]    fileId     Pointer to the VCMTP file identifier to be set to
 *                           that of the entry.
 * @retval        0          Success. \c *fileId is set.
 * @retval        EINVAL     @code{fiq == NULL || fileId == NULL}.
 * @retval        ECANCELED  Operation of the queue has been canceled.
 */
int
fiq_remove(
    FileIdQueue* const fiq,
    VcmtpFileId* const fileId)
{
    Entry* entry;
    int    status;

    if (!fiq || !fileId)
        return EINVAL;

    status = fiq_removeHead(fiq, &entry); // cancellation point
    if (status == 0)
        entry_fin(entry, fileId);

    return status;
}

/**
 * Cancels the operation of a VCMTP file-identifier queue. Idempotent.
 *
 * @param[in] fiq     Pointer to the queue to be canceled.
 * @retval    0       Success.
 * @retval    EINVAL  `fiq == NULL`
 */
int
fiq_cancel(
    FileIdQueue* const fiq)
{
    if (!fiq)
        return EINVAL;

    fiq_lock(fiq);
    fiq->isCancelled = 1;
    (void)pthread_cond_broadcast(&fiq->cond); // not a cancellation point
    fiq_unlock(fiq);

    return 0;
}
