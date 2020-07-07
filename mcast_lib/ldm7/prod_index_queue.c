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
    FmtpProdIndex iProd;  ///< index of the product
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
    const FmtpProdIndex iProd)
{
    Entry* entry = log_malloc(sizeof(Entry), "product-index queue-entry");

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
static inline FmtpProdIndex
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
    ProdIndexQueue* const piq)
{
    int status = pthread_mutex_lock(&piq->mutex);

    if (status)
        log_add_syserr("Couldn't lock mutex");
}

static inline void
unlock(
    ProdIndexQueue* const piq)
{
    int status = pthread_mutex_unlock(&piq->mutex);

    if (status)
        log_add_syserr("Couldn't unlock mutex");
}

/**
 * Adds an entry to the tail of the queue. Does nothing if the queue has been
 * canceled. The queue must be locked.
 *
 * @pre                      `lock(piq)` has been called
 * @param[in,out] piq        Pointer to the queue. Not checked.
 * @param[in]     tail       Pointer to the entry to be added. Not checked.
 *                           Must have NULL "previous" and "next" pointers.
 * @retval        0          Success.
 * @retval        ECANCELED  The queue has been canceled. `log_add()` called.
 */
static int
addTail(
    ProdIndexQueue* const piq,
    Entry* const          tail)
{
    int status;

    if (piq->isCancelled) {
        log_add("The queue has been shutdown");
        status = ECANCELED;
    }
    else {
        if (piq->count == 0) {
            piq->head = piq->tail = tail;
        }
        else {
            tail->prev = piq->tail;
            piq->tail->next = tail;
            piq->tail = tail;
        }
        piq->count++;
        status = 0;

        (void)pthread_cond_signal(&piq->cond);
    }

    return status;
}

/**
 * Returns the entry at the head of the queue. Blocks until that entry exists or
 * the queue is canceled. The queue must be locked.
 *
 * @pre                      The queue is locked.
 * @param[in,out] piq        Pointer to the queue. Not checked.
 * @param[out]    entry      Pointer to the pointer to the head entry.
 * @retval        0          Success.
 * @retval        ECANCELED  The queue has been canceled.
 */
static int
getHead(
    ProdIndexQueue* const restrict piq,
    Entry** const restrict         entry)
{
    int    status;

    while (piq->head == NULL && !piq->isCancelled)
        pthread_cond_wait(&piq->cond, &piq->mutex); // cancellation point

    if (piq->isCancelled) {
        status = ECANCELED;
    }
    else {
        *entry = piq->head;
        status = 0;
    }

    return status;
}

/**
 * Removes the entry at the head of the queue. If the queue is empty, then no
 * action is performed. The queue must be locked.
 *
 * @param[in,out] piq        Pointer to the queue. Not checked.
 * @retval        NULL       The queue is empty.
 * @return                   Pointer to what was the head entry.
 */
static Entry*
removeHead(
    ProdIndexQueue* const piq)
{
    if (piq->count == 0)
        return NULL;

    Entry* entry = piq->head;

    piq->head = entry->next;
    if (piq->head)
        piq->head->prev = NULL;
    if (piq->tail == entry)
        piq->tail = NULL;
    piq->count--;

    return entry;
}

/**
 * Initializes a mutex.
 *
 * @param[in] mutex  Mutex to be initialized.
 * @retval    true   Success.
 * @retval    false  Failure. `log_add()` called.
 */
static bool
piq_initMutex(
        pthread_mutex_t* const mutex)
{
    pthread_mutexattr_t mutexAttr;
    int                 status = pthread_mutexattr_init(&mutexAttr);

    if (status) {
        log_errno(status, "Couldn't initialize mutex attributes");
    }
    else {
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);

        if ((status = pthread_mutexattr_settype(&mutexAttr,
                PTHREAD_MUTEX_RECURSIVE))) {
            log_errno(status, "Couldn't set recursive mutex attribute");
        }
        else if ((status = pthread_mutex_init(mutex, &mutexAttr))) {
            log_add_syserr("Couldn't initialize mutex");
        }

        (void)pthread_mutexattr_destroy(&mutexAttr);
    } // `mutexAttr` initialized

    return status == 0;
}

/**
 * Initializes the locking mechanism of a product-index queue.
 *
 * @param[in] piq    The product-index queue.
 * @retval    true   Success.
 * @retval    false  Failure. `log_add()` called.
 */
static bool
piq_initLock(
        ProdIndexQueue* const piq)
{
    if (piq_initMutex(&piq->mutex)) {
        int status = pthread_cond_init(&piq->cond, NULL);

        if (0 == status)
            return true;

        log_errno(status, "Couldn't initialize condition-variable");
        (void)pthread_mutex_destroy(&piq->mutex);
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
 *               \c piq_free() when it is no longer needed.
 */
ProdIndexQueue*
piq_new(void)
{
    ProdIndexQueue* piq = log_malloc(sizeof(ProdIndexQueue),
            "missed-product product-index queue");

    if (piq) {
        if (piq_initLock(piq)) {
            piq->head = piq->tail = NULL;
            piq->count = 0;
            piq->isCancelled = 0;
        }
        else {
            free(piq);
            piq = NULL;
        }
    } /* "piq" allocated */

    return piq;
}

/**
 * Clears a product-index queue of all entries.
 *
 * @param[in] piq  The product-index queue to be cleared.
 * @return         The number of entries removed.
 */
size_t
piq_clear(
    ProdIndexQueue* const piq)
{
    lock(piq);

    size_t count = piq->count;

    for (Entry *next, *entry = piq->head; entry; entry = next) {
        next = entry->next;
        entry_free(entry);
    }

    piq->head = piq->tail = NULL;
    piq->count = 0;

    unlock(piq);

    return count;
}

/**
 * Frees a product-index queue. Accessing the queue after calling this
 * function results in undefined behavior.
 *
 * @param[in] piq  Pointer to the product-index queue to be freed or NULL.
 */
void
piq_free(
    ProdIndexQueue* const piq)
{
    if (piq) {
        piq_clear(piq);
        (void)pthread_cond_destroy(&piq->cond);
        (void)pthread_mutex_destroy(&piq->mutex);
        free(piq);
    }
}

/**
 * Adds a product-index to a queue.
 *
 * @param[in,out] piq        Pointer to the product-index queue to which to
 *                           add a product-index.
 * @param[in]     iProd      Index of the data-product.
 * @retval        0          Success.
 * @retval        ENOMEM     Out of memory. \c log_add() called.
 * @retval        ECANCELED  The queue has been canceled.
 */
int
piq_add(
    ProdIndexQueue* const piq,
    const FmtpProdIndex   iProd)
{
    Entry* entry = entry_new(iProd);
    int    status;

    if (!entry) {
        status = ENOMEM;
    }
    else {
        lock(piq);
        status = addTail(piq, entry);
        unlock(piq);
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
 * @param[in,out] piq        Pointer to the product-index queue.
 * @param[out]    iProd      Pointer to the product-index to be set to
 *                           that of the head of the queue.
 * @retval        0          Success. \c *iProd is set.
 * @retval        ECANCELED  Operation of the queue has been canceled.
 */
int
piq_peekWait(
    ProdIndexQueue* const piq,
    FmtpProdIndex* const  iProd)
{
    int    status;
    Entry* entry;

    lock(piq);

    if ((status = getHead(piq, &entry)) == 0)
        *iProd = entry_getProductIndex(entry);

    unlock(piq);

    return status;
}

/**
 * Immediately removes and returns the product-index at the head of a
 * product-index queue. Doesn't block.
 *
 * @param[in,out] piq        Pointer to the product-index queue.
 * @param[out]    iprod      Pointer to the product-index to be set to
 *                           that of the head of the queue.
 * @retval        0          Success. \c *iProd is set.
 * @retval        ENOENT     The queue is empty.
 */
int
piq_removeNoWait(
    ProdIndexQueue* const piq,
    FmtpProdIndex* const  iProd)
{
    lock(piq);

    Entry* entry = removeHead(piq);
    int    status;

    if (entry == NULL) {
        status = ENOENT;
    }
    else {
        *iProd = entry_getProductIndex(entry);
        entry_free(entry);
        status = 0;
    }

    unlock(piq);

    return status;
}

/**
 * Immediately returns (but does not remove) the product-index at the head of
 * the product-index queue.
 *
 * @param[in,out] piq        Pointer to the product-index queue.
 * @param[out]    iProd      Pointer to the product-index to be set to
 *                           that of the head of the queue.
 * @retval        0          Success. \c *iProd is set.
 * @retval        ENOENT     The queue is empty.
 */
int
piq_peekNoWait(
    ProdIndexQueue* const piq,
    FmtpProdIndex* const  iProd)
{
    lock(piq);

    Entry* entry = piq->head;
    int    status;

    if (entry == NULL) {
        status = ENOENT;
    }
    else {
        *iProd = entry_getProductIndex(entry);
        status = 0;
    }

    unlock(piq);

    return status;
}

/**
 * Returns the number of entries currently in a product-index queue.
 *
 * @param[in] piq  The product-index queue.
 * @return         The number of identifiers in the queue.
 */
size_t
piq_count(
    ProdIndexQueue* const piq)
{
    size_t count;

    lock(piq);
    count = piq->count;
    unlock(piq);

    return count;
}

/**
 * Cancels the operation of a FMTP product-index queue. Idempotent.
 *
 * @param[in] piq     Pointer to the queue to be canceled.
 * @retval    0       Success.
 * @retval    EINVAL  `piq == NULL`
 */
int
piq_cancel(
    ProdIndexQueue* const piq)
{
    if (!piq)
        return EINVAL;

    lock(piq);
        piq->isCancelled = 1;
        (void)pthread_cond_signal(&piq->cond); // not a cancellation point
    unlock(piq);

    return 0;
}

/**
 * Restarts the operation of an FMTP product-index queue on which `piq_cancel()`
 * has been called. Idempotent.
 *
 * @param[in] piq     Queue to be restarted
 * @retval    0       Success
 * @retval    EINVAL  `piq == NULL`
 */
int
piq_restart(ProdIndexQueue* const piq)
{
    if (!piq)
        return EINVAL;

    lock(piq);
        piq->isCancelled = 0;
        (void)pthread_cond_signal(&piq->cond); // not a cancellation point
    unlock(piq);

    return 0;
}

/**
 * Indicates if a product-index queue has been canceled.
 *
 * @param[in] piq    Pointer to the product-index queue.
 * @retval    false  The queue has not been canceled.
 * @retval    true   The queue has been canceled.
 */
bool
piq_isCanceled(
    ProdIndexQueue* const piq)
{
    bool isCanceled;

    lock(piq);
        isCanceled = piq->isCancelled;
    unlock(piq);

    return isCanceled;
}
