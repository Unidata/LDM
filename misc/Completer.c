/**
 * This file defines an object that decorates an Executor with a queue of
 * completed asynchronous tasks.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Completer.c
 *  Created on: May 8, 2018
 *      Author: Steven R. Emmerson
 */
#include "../../misc/Completer.h"

#include "config.h"

#include "Thread.h"

#include "log.h"

/******************************************************************************
 * Job:
 ******************************************************************************/

typedef struct job {
    /// Future of task submitted to completion service
    Future*           future;
    struct completer* comp;   ///< Associated completion service
} Job;

/**
 * Creates a new job.
 *
 * @param[in] obj       Object submitted to completion service. May be `NULL`.
 * @param[in] run       Function to execute `obj`
 * @param[in] halt      Function to cancel execution. Must return 0 on success.
 *                      May be `NULL`.
 * @retval    `NULL`    Failure. `log_add()` called.
 */
static Job*
job_new(struct completer* const restrict comp,
        void* const                      obj,
        int                            (*run)(void* obj, void** result),
        int                            (*halt)(void* obj, pthread_t thread))
{
    Job* job = log_malloc(sizeof(Job), "completion job");

    if (job) {
        job->comp = comp;
        job->future = future_new(obj, run, halt);

        if (job->future == NULL) {
            log_add("Couldn't create future");
            free(job);
            job = NULL;
        }
    } // `job` allocated

    return job;
}

/**
 * Frees a job.
 *
 * @param[in,out] job  Job to be freed
 */
static void
job_free(Job* const job)
{
    future_free(job->future);
    free(job);
}

/**
 * Executes a job.
 *
 * @param[in,out] arg        Job to be executed
 * @param[out]    result     Result of executing the job
 * @retval        0          Success
 * @retval        ECANCELED  Job was canceled
 * @retval        ENOMEM     Out of memory. `log_add()` called.
 * @return                   Error code from submitted run function. `log_add()`
 *                           called.
 */
static int
job_run(void* const restrict  arg,
        void** const restrict result)
{
    Job* const job = (Job*)arg;

    (void)future_run(job->future);

    /*
     * `future_run()` returned => task completed => no waiting for result, which
     * would fail anyway because the current thread would wait on itself
     */
    int status = future_getResultNoWait(job->future, result);

    return status;
}

/**
 * Cancels a job. Called if `completer_shutdown(..., true)` is called.
 *
 * @param[in,out] arg      Job to be canceled
 * @param[in,out] thread   Thread on which the job is executing
 * @retval        0        Success
 * @retval        ENOMEM   Out of memory. `log_add()` called.
 * @return                 Error code from submitted cancel function.
 *                         `log_add()` called.
 */
static int
job_cancel(
        void* const arg,
        pthread_t   thread)
{
    Job* const    job = (Job*)arg;
    Future* const future = job->future;
    int           status = future_cancel(future);

    if (status)
        log_add("Couldn't cancel submitted task");

    return status;
}

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
 * @param[in] future  Future of task submitted to execution service. Not freed.
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
 * @param[in]     future  Future of task submitted to execution service
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
 * @return               Future that was at head of queue
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

static size_t
doneQ_size(DoneQ* const doneQ)
{
    return doneQ->size;
}

/******************************************************************************
 * Completion service:
 ******************************************************************************/

struct completer {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    Executor*       exec;
    DoneQ*          doneQ;
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
 * Processes the future of a completed task that was submitted to the execution
 * service. Adds the future to the queue of completed futures. Called by the
 * completion service's execution service.
 *
 * @param[in,out] arg     Completion service
 * @param[in]     future  Future of task submitted to execution service
 * @retval        0       Success
 * @retval        ENOMEM  Out of memory. `log_add()` called.
 */
static int
completer_afterCompletion(
        void* const restrict   arg,
        Future* const restrict future)
{
    int              status;
    Completer* const comp = (Completer*)arg;

    /*
     * NB: `comp->exec` is holding a lock, so this function must not call an
     * execution service function on `comp->exec`. See
     * `executor_afterCompletion()`.
     */
    completer_lock(comp);
        status = doneQ_add(comp->doneQ, future);

        if (status) {
            log_add("Couldn't add completed future to queue");
        }
        else {
            // Notify `completer_take()`
            (void)pthread_cond_signal(&comp->cond);
        }
    completer_unlock(comp);

    job_free(future_getObj(future)); // Must be freed somewhere

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
    comp->doneQ = doneQ_new();

    if (comp->doneQ == NULL) {
        log_add("Couldn't create queue for completed futures");
        status = ENOMEM;
    }
    else {
        status = pthread_cond_init(&comp->cond, NULL);

        if (status) {
            log_add_errno(status, "Couldn't initialize condition-variable");
        }
        else {
            status = mutex_init(&comp->mutex, PTHREAD_MUTEX_RECURSIVE, true);

            if (status == 0) {
                comp->exec = executor_new(completer_afterCompletion, comp);

                if (comp->exec == NULL) {
                    log_add("Couldn't create new execution service");
                    (void)pthread_mutex_destroy(&comp->mutex);
                    status = ENOMEM;
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
    Job*    job = job_new(comp, obj, run, halt);

    if (job == NULL) {
        log_add("Couldn't create new job");
        future = NULL;
    }
    else {
        future = executor_submit(comp->exec, job, job_run, job_cancel);

        if (future == NULL) {
            log_add("Couldn't submit job to execution service");
            job_free(job);
            job = NULL;
        }
    } // `job` created

    return future;
}

Future*
completer_take(Completer* const comp)
{
    Future* future;

    completer_lock(comp);
        while ((future = doneQ_take(comp->doneQ)) == NULL &&
                executor_size(comp->exec)) {
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
        /*
         * Locking isn't necessary and would, otherwise, require a recursive
         * mutex because `job_cancel()` could be called, which calls
         * `completer_add()`.
         */
        status = executor_shutdown(comp->exec, now);
    completer_unlock(comp);

    return status;
}
