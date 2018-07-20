/**
 * This file defines an object that executes asynchronous tasks and presents a
 * queue of completed tasks.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Completer.c
 *  Created on: May 8, 2018
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "Completer.h"
#include "Thread.h"
#include "log.h"

/******************************************************************************
 * Entry in queue of completed futures:
 ******************************************************************************/

typedef struct entry {
    Future*       future;
    struct entry* prev;
    struct entry* next;
} Entry;

/**
 * Creates a new entry for a queue of completed futures.
 *
 * @param[in] future  Completed future
 * @retval    `NULL`  Out of memory. `log_add()` called.
 * @return            New entry
 */
static Entry*
entry_new(Future* const future)
{
    Entry* const entry = log_malloc(sizeof(Entry),
            "entry for queue of completed futures");

    if (entry) {
        entry->future = future;
        entry->next = entry->prev = NULL;
    }

    return entry;
}

/**
 * Frees an entry for a queue of completed futures. Doesn't free the entry's
 * future.
 *
 * @param[in] entry  Entry to be freed. Doesn't free entry's future.
 */
inline static void
entry_free(Entry* const entry)
{
    free(entry);
}

/******************************************************************************
 * Queue of completed futures:
 ******************************************************************************/

typedef struct doneQ {
    Entry* head;
    Entry* tail;
    size_t size;
} DoneQ;

/**
 * Creates a new, empty queue of completed futures.
 *
 * @retval `NULL`  Failure. `log_add()` called.
 * @return         New, empty queue of completed futures
 */
static DoneQ*
doneQ_new()
{
    DoneQ* doneQ = log_malloc(sizeof(DoneQ), "queue of completed futures");

    if (doneQ) {
        doneQ->head = doneQ->tail = NULL;
        doneQ->size = 0;
    }

    return doneQ;
}

/**
 * Frees a queue of completed futures. Doesn't free any futures.
 *
 * @param[in,out] doneQ  Queue to be freed
 */
static void
doneQ_free(DoneQ* const doneQ)
{
    for (Entry* entry = doneQ->head; entry != NULL; ) {
        Entry* next = entry->next;
        entry_free(entry); // Doesn't free `entry->future`
        entry = next;
    }

    free(doneQ);
}

/**
 * Adds a future to a queue of completed futures.
 *
 * @param[in,out] doneQ   Queue of completed futures
 * @param[in]     future  Completed future
 * @retval        0       Success
 * @retval        ENOMEM  Out of memory. `log_add()` called.
 */
static int
doneQ_add(
        DoneQ* const restrict  doneQ,
        Future* const restrict future)
{
    int          status;
    Entry* const entry = entry_new(future);

    if (entry == NULL) {
        status = ENOMEM;
    }
    else {
        Entry* const tail = doneQ->tail;

        // Attach entry
        entry->prev = tail;
        if (tail)
            tail->next = entry;
        doneQ->tail = entry;

        if (doneQ->head == NULL)
            doneQ->head = entry; // Adjust head

        doneQ->size++;
        status = 0;
    }

    return status;
}

/**
 * Removes the future at the head of a queue of completed futures and returns
 * it.
 *
 * @param[in,out] doneQ  Queue of completed futures
 * @retval        NULL   Queue is empty
 * @return               Future at head of queue
 */
static Future*
doneQ_take(DoneQ* const doneQ)
{
    Future*      future;
    Entry* const head = doneQ->head;

    if (head == NULL) {
        future = NULL;
    }
    else {
        Entry* const next = head->next;

        // Detach head
        doneQ->head = next;
        if (next)
            next->prev = NULL;

        if (doneQ->tail == head)
            doneQ->tail = NULL; // Adjust tail

        doneQ->size--;
        future = head->future;

        entry_free(head); // Doesn't free entry's future
    }

    return future;
}

/******************************************************************************
 * Completion service:
 ******************************************************************************/

struct completer {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    Executor*       exec;
    DoneQ*          doneQ;
    unsigned        numFutures;
    bool            isShutdown;
};

/**
 * Locks a completion service.
 *
 * @param[in,out] comp  Completion service to be locked
 */
static void
completer_lock(Completer* const comp)
{
    (void)mutex_lock(&comp->mutex);
}

/**
 * Unlocks a completion service.
 *
 * @param[in,out] comp  Completion service to be unlocked
 */
static void
completer_unlock(Completer* const comp)
{
    (void)mutex_unlock(&comp->mutex);
}

/**
 * Adds a completed future to the queue of completed futures. Called by
 * `job_run()`.
 *
 * @param[in,out] arg     Completion service
 * @param[in]     future  Completed future
 * @retval        0       Success
 * @retval        ENOMEM  Out of memory. `log_add()` called.
 */
static int
completer_add(
        void* const restrict   arg,
        Future* const restrict future)
{
    int              status;
    Completer* const comp = (Completer*)arg;

    completer_lock(comp);
        status = doneQ_add(comp->doneQ, future);

        if (status) {
            log_add("Couldn't add completed future to queue");
        }
        else {
            --comp->numFutures;

            // Notify `completer_take()`
            (void)pthread_cond_broadcast(&comp->cond);
        }
    completer_unlock(comp);

    return status;
}

/**
 * Initializes a completion service.
 *
 * @param[in,out] comp  Completion service
 * @retval        0     Success
 * @retval        ENOMEM  Out of memory. `log_ad()` called.
 * @retval        EAGAIN  System lacked resources (other than memory).
 *                        `log_add()` called.
 */
static int
completer_init(Completer* const comp)
{
    int status;

    comp->isShutdown = false;
    comp->numFutures = 0;
    comp->doneQ = doneQ_new();

    if (comp->doneQ == NULL) {
        log_add("Couldn't create queue for completed jobs");
        status = ENOMEM;
    }
    else {
        status = pthread_cond_init(&comp->cond, NULL);

        if (status) {
            log_add_errno(status, "Couldn't initialize condition-variable");
        }
        else {
            status = mutex_init(&comp->mutex, PTHREAD_MUTEX_ERRORCHECK, true);

            if (status == 0) {
                comp->exec = executor_new();

                if (comp->exec == NULL) {
                    log_add("Couldn't create new execution service");
                    (void)pthread_mutex_destroy(&comp->mutex);
                    status = ENOMEM;
                }
                else {
                    executor_setAfterCompletion(comp->exec, comp,
                            completer_add);
                }
            } // `comp->mutex` initialized

            if (status)
                (void)pthread_cond_destroy(&comp->cond);
        } // `comp->cond` initialized

        if (status)
            doneQ_free(comp->doneQ);
    } // `comp->doneQ` created

    return status;
}

static void
completer_destroy(Completer* const comp)
{
    completer_lock(comp);
        executor_free(comp->exec);

        (void)pthread_cond_destroy(&comp->cond);

        doneQ_free(comp->doneQ);
    completer_unlock(comp);

    (void)pthread_mutex_destroy(&comp->mutex);
}

Completer*
completer_new()
{
    int        status;
    Completer* comp = log_malloc(sizeof(Completer), "completion service");

    if (comp) {
        status = completer_init(comp);

        if (status) {
            log_add("Couldn't initialize completion service");
            free(comp);
            comp = NULL;
        }
    } // `comp` allocated

    return comp;
}

void
completer_free(Completer* const comp)
{
    if (comp) {
        completer_destroy(comp);
        free(comp);
    }
}

Future*
completer_submit(
        Completer* const comp,
        void* const      obj,
        int            (*run)(void* obj, void** result),
        int            (*halt)(void* obj, pthread_t thread))
{
    Future* future;

    completer_lock(comp);
        if (comp->numFutures + 1 < comp->numFutures) {
            log_add("Too many submitted jobs: %u", comp->numFutures);
        }
        else {
            future = executor_submit(comp->exec, obj, run, halt);

            if (future == NULL) {
                log_add("Couldn't submit task to execution service");
            }
            else {
                ++comp->numFutures;
            }
        }
    completer_unlock(comp);

    return future;
}

Future*
completer_take(Completer* const comp)
{
    Future* future;

    completer_lock(comp);
        while ((future = doneQ_take(comp->doneQ)) == NULL &&
                comp->numFutures > 0) {
            int status = pthread_cond_wait(&comp->cond, &comp->mutex);
            log_assert(status == 0);
        }
    completer_unlock(comp);

    return future;
}

int
completer_shutdown(
        Completer* const comp,
        const bool       now)
{
    int status;

    completer_lock(comp);
        status = executor_shutdown(comp->exec, now);
    completer_unlock(comp);

    return status;
}
