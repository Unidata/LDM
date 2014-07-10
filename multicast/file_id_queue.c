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
static inline void
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
static inline VcmtpFileId
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
    size_t          count;
};

static inline void
lock(
    FileIdQueue* const fiq)
{
    (void)pthread_mutex_lock(&fiq->mutex);
}

static inline void
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
        if (fiq->count == 0) {
            fiq->head = fiq->tail = tail;
        }
        else {
            tail->prev = fiq->tail;
            fiq->tail->next = tail;
            fiq->tail = tail;
        }
        fiq->count++;
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
    if (fiq->count == 0)
        return NULL;

    Entry* entry = fiq->head;

    fiq->head = entry->next;
    if (fiq->head)
        fiq->head->prev = NULL;
    if (fiq->tail == entry)
        fiq->tail = NULL;
    fiq->count--;

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
                fiq->count = 0;
                fiq->isCancelled = 0;
            }
        } /* "fiq->mutex" allocated */
    } /* "fiq" allocated */

    return fiq;
}

/**
 * Clears a file-identifier queue of all entries.
 *
 * @param[in] fiq  The file-identifier queue to be cleared.
 * @return         The number of entries removed.
 */
size_t
fiq_clear(
    FileIdQueue* const fiq)
{
    lock(fiq);

    size_t count = fiq->count;

    for (Entry *next, *entry = fiq->head; entry; entry = next) {
        next = entry->next;
        entry_free(entry);
    }

    fiq->head = fiq->tail = NULL;
    fiq->count = 0;

    unlock(fiq);

    return count;
}

/**
 * Frees a file-identifier queue. Accessing the queue after calling this
 * function results in undefined behavior.
 *
 * @param[in] fiq  Pointer to the file-identifier queue to be freed or NULL.
 */
void
fiq_free(
    FileIdQueue* const fiq)
{
    if (fiq) {
        fiq_clear(fiq);
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
 * @retval        ENOMEM     Out of memory. \c log_add() called.
 * @retval        ECANCELED  The queue has been canceled.
 */
int
fiq_add(
    FileIdQueue* const fiq,
    const VcmtpFileId  fileId)
{
    Entry* entry = entry_new(fileId);
    int    status;

    if (!entry) {
        status = ENOMEM;
    }
    else {
        lock(fiq);
        status = addTail(fiq, entry);
        unlock(fiq);
        if (status)
            entry_free(entry);
    }

    return status;
}

/**
 * Returns (but does not remove) the file-identifier at the head of the
 * file-identifier queue. Blocks until such an entry is available or
 * the queue is canceled.
 *
 * @param[in,out] fiq        Pointer to the file-identifier queue.
 * @param[out]    fileId     Pointer to the VCMTP file identifier to be set to
 *                           that of the head of the queue.
 * @retval        0          Success. \c *fileId is set.
 * @retval        ECANCELED  Operation of the queue has been canceled.
 */
int
fiq_peekWait(
    FileIdQueue* const fiq,
    VcmtpFileId* const fileId)
{
    lock(fiq);

    int    status;
    Entry* entry;

    if ((status = getHead(fiq, &entry)) == 0)
        *fileId = entry_getFileId(entry);

    unlock(fiq);

    return status;
}

/**
 * Immediately removes and returns the file-identifier at the head of a
 * file-identifier queue. Doesn't block.
 *
 * @param[in,out] fiq        Pointer to the file-identifier queue.
 * @param[out]    fileId     Pointer to the VCMTP file identifier to be set to
 *                           that of the head of the queue.
 * @retval        0          Success. \c *fileId is set.
 * @retval        ENOENT     The queue is empty.
 */
int
fiq_removeNoWait(
    FileIdQueue* const fiq,
    VcmtpFileId* const fileId)
{
    lock(fiq);

    Entry* entry = removeHead(fiq);
    int    status;

    if (entry == NULL) {
        status = ENOENT;
    }
    else {
        *fileId = entry_getFileId(entry);
        entry_free(entry);
        status = 0;
    }

    unlock(fiq);

    return status;
}

/**
 * Immediately returns (but does not remove) the file-identifier at the head of
 * the file-identifier queue.
 *
 * @param[in,out] fiq        Pointer to the file-identifier queue.
 * @param[out]    fileId     Pointer to the VCMTP file identifier to be set to
 *                           that of the head of the queue.
 * @retval        0          Success. \c *fileId is set.
 * @retval        ENOENT     The queue is empty.
 */
int
fiq_peekNoWait(
    FileIdQueue* const fiq,
    VcmtpFileId* const fileId)
{
    lock(fiq);

    Entry* entry = fiq->head;
    int    status;

    if (entry == NULL) {
        status = ENOENT;
    }
    else {
        *fileId = entry_getFileId(entry);
        status = 0;
    }

    unlock(fiq);

    return status;
}

/**
 * Returns the number of entries currently in a file-identifier queue.
 *
 * @param[in] fiq  The file-identifier queue.
 * @return         The number of identifiers in the queue.
 */
size_t
fiq_count(
    FileIdQueue* const fiq)
{
    size_t count;

    lock(fiq);
    count = fiq->count;
    unlock(fiq);

    return count;
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

/**
 * Indicates if a file-identifier queue has been canceled.
 *
 * @param[in] fiq    Pointer to the file-identifier queue.
 * @retval    false  The queue has not been canceled.
 * @retval    true   The queue has been canceled.
 */
bool
fiq_isCanceled(
    FileIdQueue* const fiq)
{
    bool isCanceled;

    lock(fiq);
    isCanceled = fiq->isCancelled;
    unlock(fiq);

    return isCanceled;
}
