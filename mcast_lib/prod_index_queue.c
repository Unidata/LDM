/**
 * Copyright 2014 University Corporation for Atmospheric Research.
 * All rights reserved. See file COPYRIGHT in the top-level source-directory
 * for legal conditions.
 *
 *   @file prod_index_queue.c
 * @author Steven R. Emmerson
 *
 * This file implements a non-persistent, thread-safe FIFO queue of product
 * indexes.
 */

#include "config.h"

#include "ldm.h"
#include "log.h"
#include "prod_index_queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * The type of an entry in a product-index queue.
 */
typedef struct entry {
    struct entry*  prev;   ///< points to the previous entry towards the head
    struct entry*  next;   ///< points to the next entry towards the tail
    McastProdIndex iProd;  ///< index of the product
} Entry;

/**
 * Returns a new entry.
 *
 * @param[in] iProd   Index of the product.
 * @retval    NULL    Error. \c log_add() called.
 * @return            Pointer to the new entry. The client should call \c
 *                    entry_free() when it is no longer needed.
 */
static Entry*
entry_new(
    const McastProdIndex iProd)
{
    Entry* entry = LOG_MALLOC(sizeof(Entry), "product-index queue-entry");

    if (entry) {
        entry->iProd = iProd;
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
 * Returns the product-index of an entry.
 *
 * @param[in] entry  Pointer to the entry.
 * @return           The product-index of the entry.
 */
static inline McastProdIndex
entry_getProductIndex(
    const Entry* const entry)
{
    return entry->iProd;
}

/**
 * The definition of a product-index queue object.
 */
struct prod_index_queue {
    Entry*          head;
    Entry*          tail;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    int             isCancelled;
    size_t          count;
};

static inline void
lock(
    ProdIndexQueue* const fiq)
{
    (void)pthread_mutex_lock(&fiq->mutex);
}

static inline void
unlock(
    ProdIndexQueue* const fiq)
{
    (void)pthread_mutex_unlock(&fiq->mutex);
}

/**
 * Adds an entry to the tail of the queue. Does nothing if the queue has been
 * canceled. The queue must be locked.
 *
 * @pre                      {`lock(fiq)` has been called}
 * @param[in,out] fiq        Pointer to the queue. Not checked.
 * @param[in]     tail       Pointer to the entry to be added. Not checked.
 *                           Must have NULL "previous" and "next" pointers.
 * @retval        0          Success.
 * @retval        ECANCELED  The queue has been canceled.
 */
static int
addTail(
    ProdIndexQueue* const fiq,
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
    ProdIndexQueue* const restrict fiq,
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
    ProdIndexQueue* const fiq)
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

/**
 * Initializes a mutex.
 *
 * @param[in] mutex  Mutex to be initialized.
 * @retval    true   Success.
 * @retval    false  Failure. `log_start()` called.
 */
static bool
fiq_initMutex(
        pthread_mutex_t* const mutex)
{
    pthread_mutexattr_t mutexAttr;
    int                 status = pthread_mutexattr_init(&mutexAttr);

    if (status) {
        LOG_ERRNUM0(status, "Couldn't initialize mutex attributes");
    }
    else {
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);

        if ((status = pthread_mutexattr_settype(&mutexAttr,
                PTHREAD_MUTEX_RECURSIVE))) {
            LOG_ERRNUM0(status, "Couldn't set recursive mutex attribute");
        }
        else if ((status = pthread_mutex_init(mutex, &mutexAttr))) {
            LOG_SERROR0("Couldn't initialize mutex");
        }

        (void)pthread_mutexattr_destroy(&mutexAttr);
    } // `mutexAttr` initialized

    return status == 0;
}

/**
 * Initializes the locking mechanism of a product-index queue.
 *
 * @param[in] fiq    The product-index queue.
 * @retval    true   Success.
 * @retval    false  Failure. `log_start()` called.
 */
static bool
fiq_initLock(
        ProdIndexQueue* const fiq)
{
    if (fiq_initMutex(&fiq->mutex)) {
        int status;

        if (0 == (status = pthread_cond_init(&fiq->cond, NULL)))
            return true;

        LOG_ERRNUM0(status, "Couldn't initialize condition-variable");
        (void)pthread_mutex_destroy(&fiq->mutex);
    }

    return false;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new product-index queue.
 *
 * @retval NULL  Failure. \c log_add() called.
 * @return       Pointer to a new product-index queue. The client should call
 *               \c fiq_free() when it is no longer needed.
 */
ProdIndexQueue*
fiq_new(void)
{
    ProdIndexQueue* fiq = LOG_MALLOC(sizeof(ProdIndexQueue),
            "missed-product product-index queue");

    if (fiq) {
        if (fiq_initLock(fiq)) {
            fiq->head = fiq->tail = NULL;
            fiq->count = 0;
            fiq->isCancelled = 0;
        }
        else {
            free(fiq);
            fiq = NULL;
        }
    } /* "fiq" allocated */

    return fiq;
}

/**
 * Clears a product-index queue of all entries.
 *
 * @param[in] fiq  The product-index queue to be cleared.
 * @return         The number of entries removed.
 */
size_t
fiq_clear(
    ProdIndexQueue* const fiq)
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
 * Frees a product-index queue. Accessing the queue after calling this
 * function results in undefined behavior.
 *
 * @param[in] fiq  Pointer to the product-index queue to be freed or NULL.
 */
void
fiq_free(
    ProdIndexQueue* const fiq)
{
    if (fiq) {
        fiq_clear(fiq);
        (void)pthread_cond_destroy(&fiq->cond);
        (void)pthread_mutex_destroy(&fiq->mutex);
        free(fiq);
    }
}

/**
 * Adds a product-index to a queue.
 *
 * @param[in,out] fiq        Pointer to the product-index queue to which to
 *                           add a product-index.
 * @param[in]     iProd      Index of the data-product.
 * @retval        0          Success.
 * @retval        ENOMEM     Out of memory. \c log_add() called.
 * @retval        ECANCELED  The queue has been canceled.
 */
int
fiq_add(
    ProdIndexQueue* const   fiq,
    const McastProdIndex iProd)
{
    Entry* entry = entry_new(iProd);
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
 * Returns (but does not remove) the product-index at the head of the
 * product-index queue. Blocks until such an entry is available or
 * the queue is canceled.
 *
 * @param[in,out] fiq        Pointer to the product-index queue.
 * @param[out]    iProd      Pointer to the product-index to be set to
 *                           that of the head of the queue.
 * @retval        0          Success. \c *iProd is set.
 * @retval        ECANCELED  Operation of the queue has been canceled.
 */
int
fiq_peekWait(
    ProdIndexQueue* const fiq,
    McastProdIndex* const iProd)
{
    lock(fiq);

    int    status;
    Entry* entry;

    if ((status = getHead(fiq, &entry)) == 0)
        *iProd = entry_getProductIndex(entry);

    unlock(fiq);

    return status;
}

/**
 * Immediately removes and returns the product-index at the head of a
 * product-index queue. Doesn't block.
 *
 * @param[in,out] fiq        Pointer to the product-index queue.
 * @param[out]    iprod      Pointer to the product-index to be set to
 *                           that of the head of the queue.
 * @retval        0          Success. \c *iProd is set.
 * @retval        ENOENT     The queue is empty.
 */
int
fiq_removeNoWait(
    ProdIndexQueue* const fiq,
    McastProdIndex* const iProd)
{
    lock(fiq);

    Entry* entry = removeHead(fiq);
    int    status;

    if (entry == NULL) {
        status = ENOENT;
    }
    else {
        *iProd = entry_getProductIndex(entry);
        entry_free(entry);
        status = 0;
    }

    unlock(fiq);

    return status;
}

/**
 * Immediately returns (but does not remove) the product-index at the head of
 * the product-index queue.
 *
 * @param[in,out] fiq        Pointer to the product-index queue.
 * @param[out]    iProd      Pointer to the product-index to be set to
 *                           that of the head of the queue.
 * @retval        0          Success. \c *iProd is set.
 * @retval        ENOENT     The queue is empty.
 */
int
fiq_peekNoWait(
    ProdIndexQueue* const    fiq,
    McastProdIndex* const iProd)
{
    lock(fiq);

    Entry* entry = fiq->head;
    int    status;

    if (entry == NULL) {
        status = ENOENT;
    }
    else {
        *iProd = entry_getProductIndex(entry);
        status = 0;
    }

    unlock(fiq);

    return status;
}

/**
 * Returns the number of entries currently in a product-index queue.
 *
 * @param[in] fiq  The product-index queue.
 * @return         The number of identifiers in the queue.
 */
size_t
fiq_count(
    ProdIndexQueue* const fiq)
{
    size_t count;

    lock(fiq);
    count = fiq->count;
    unlock(fiq);

    return count;
}

/**
 * Cancels the operation of a VCMTP product-index queue. Idempotent.
 *
 * @param[in] fiq     Pointer to the queue to be canceled.
 * @retval    0       Success.
 * @retval    EINVAL  `fiq == NULL`
 */
int
fiq_cancel(
    ProdIndexQueue* const fiq)
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
 * Indicates if a product-index queue has been canceled.
 *
 * @param[in] fiq    Pointer to the product-index queue.
 * @retval    false  The queue has not been canceled.
 * @retval    true   The queue has been canceled.
 */
bool
fiq_isCanceled(
    ProdIndexQueue* const fiq)
{
    bool isCanceled;

    lock(fiq);
    isCanceled = fiq->isCancelled;
    unlock(fiq);

    return isCanceled;
}
