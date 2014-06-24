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
    struct entry* prev;   /* points to the previous entry towards the head */
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
        entry->prev = entry->next = NULL;
    }

    return entry;
}

/**
 * Frees an entry.
 *
 * @param[in,out] entry  Pointer to the entry to be freed or NULL.
 */
inline static void
entry_free(
    Entry* const entry)
{
    free(entry);
}

/**
 * Returns the file identifier of an entry.
 *
 * @param[in] entry  Pointer to the entry.
 * @return           The file identifier of the entry.
 */
inline static VcmtpFileId
entry_getFileId(
    const Entry* const entry)
{
    return entry->fileId;
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

inline static void
lock(
    FileIdQueue* const fiq)
{
    (void)pthread_mutex_lock(&fiq->mutex);
}

inline static void
unlock(
    FileIdQueue* const fiq)
{
    (void)pthread_mutex_unlock(&fiq->mutex);
}

/**
 * Adds an entry to the tail of the queue. Does nothing if the queue has been
 * canceled. The queue must be locked.
 *
 * @param[in,out] fiq        Pointer to the queue. Not checked.
 * @param[in]     tail       Pointer to the entry to be added. Not checked.
 *                           Must have NULL "previous" and "next" pointers.
 * @retval        0          Success.
 * @retval        ECANCELED  The queue has been canceled.
 */
static int
addTail(
    FileIdQueue* const fiq,
    Entry* const       tail)
{
    int status;

    if (fiq->isCancelled) {
        status = ECANCELED;
    }
    else {
        if (fiq->head == NULL) {
            fiq->head = fiq->tail = tail;
        }
        else {
            tail->prev = fiq->tail;
            fiq->tail->next = tail;
            fiq->tail = tail;
        }
        status = 0;

        (void)pthread_cond_broadcast(&fiq->cond);
    }

    return status;
}

/**
 * Returns the entry at the head of the queue. Blocks until that entry exists or
 * the queue is canceled. The queue must be locked.
 *
 * @param[in,out] fiq        Pointer to the queue. Not checked.
 * @param[out]    entry      Pointer to the pointer to the head entry.
 * @retval        0          Success.
 * @retval        ECANCELED  The queue has been canceled.
 */
static int
getHead(
    FileIdQueue* const restrict fiq,
    Entry** const restrict      entry)
{
    int    status;

    while (fiq->head == NULL && !fiq->isCancelled)
        pthread_cond_wait(&fiq->cond, &fiq->mutex); // cancellation point

    if (fiq->isCancelled) {
        status = ECANCELED;
    }
    else {
        *entry = fiq->head;
        status = 0;
    }

    return status;
}

/**
 * Removes the entry at the head of the queue. If the queue is empty, then no
 * action is performed. The queue must be locked.
 *
 * @param[in,out] fiq        Pointer to the queue. Not checked.
 * @retval        NULL       The queue is empty.
 * @return                   Pointer to what was the head entry.
 */
static Entry*
removeHead(
    FileIdQueue* const fiq)
{
    Entry* entry = fiq->head;

    if (entry) {
        fiq->head = entry->next;
        if (fiq->head)
            fiq->head->prev = NULL;
        if (fiq->tail == entry)
            fiq->tail = NULL;
    }

    return entry;
}

/**
 * Removes the entry at the tail of the queue. If the queue is empty, then no
 * action is performed. The queue must be locked.
 *
 * @param[in,out] fiq        Pointer to the queue. Not checked.
 * @retval        NULL       The queue is empty.
 * @return                   Pointer to what was the tail entry.
 */
static Entry*
removeTail(
    FileIdQueue* const fiq)
{
    Entry* entry = fiq->tail;

    if (entry) {
        fiq->tail = entry->prev;
        if (fiq->tail)
            fiq->tail->next = NULL;
        if (fiq->head == entry)
            fiq->head = NULL;
    }

    return entry;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

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
    const VcmtpFileId  fileId)
{
    Entry* entry;
    int    status;

    if (!fiq)
        return EINVAL;

    entry = entry_new(fileId);
    if (!entry)
        return ENOMEM;

    lock(fiq);
    status = addTail(fiq, entry);
    unlock(fiq);
    if (status)
        entry_free(entry);

    return status;
}

/**
 * Returns (but does not remove) the file-identifier at the head of the
 * file-identifier queue. Blocks until such an entry is available or the queue
 * is canceled.
 *
 * @param[in,out] fiq        Pointer to the file-identifier queue.
 * @param[out]    fileId     Pointer to the VCMTP file identifier to be set to
 *                           that of the entry.
 * @retval        0          Success. \c *fileId is set.
 * @retval        EINVAL     @code{fiq == NULL || fileId == NULL}.
 * @retval        ECANCELED  Operation of the queue has been canceled.
 */
int
fiq_peek(
    FileIdQueue* const fiq,
    VcmtpFileId* const fileId)
{
    if (!fiq || !fileId)
        return EINVAL;

    lock(fiq);

    Entry* entry;
    int    status = getHead(fiq, &entry);

    if (status == 0)
        *fileId = entry_getFileId(entry);

    unlock(fiq);

    return status;
}

/**
 * Removes the head of the queue. If the queue is empty then no action is
 * performed.
 *
 * @param[in,out] fiq        Pointer to the file-identifier queue.
 * @retval        0          Success. \c *fileId is set.
 * @retval        EINVAL     @code{fiq == NULL || fileId == NULL}.
 */
int
fiq_removeHead(
    FileIdQueue* const fiq)
{

    if (!fiq)
        return EINVAL;

    lock(fiq);
    entry_free(removeHead(fiq));
    unlock(fiq);

    return 0;
}

/**
 * Removes the tail of the queue. If the queue is empty then no action is
 * performed.
 *
 * @param[in,out] fiq        Pointer to the file-identifier queue.
 * @retval        0          Success. \c *fileId is set.
 * @retval        EINVAL     @code{fiq == NULL || fileId == NULL}.
 */
int
fiq_removeTail(
    FileIdQueue* const fiq)
{

    if (!fiq)
        return EINVAL;

    lock(fiq);
    entry_free(removeTail(fiq));
    unlock(fiq);

    return 0;
}

/**
 * Removes and returns the file-identifier at the head of the file-identifier
 * queue. Blocks until such an entry is available or the queue is canceled.
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

    if (!fiq || !fileId)
        return EINVAL;

    lock(fiq);

    Entry* entry;
    int    status = getHead(fiq, &entry);

    if (status == 0) {
        *fileId = entry_getFileId(entry);
        entry_free(removeHead(fiq));
    }

    unlock(fiq);

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

    lock(fiq);
    fiq->isCancelled = 1;
    (void)pthread_cond_broadcast(&fiq->cond); // not a cancellation point
    unlock(fiq);

    return 0;
}
