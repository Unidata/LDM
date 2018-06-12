/**
 * This file defines an executor of asynchronous tasks.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Executor.c
 *  Created on: May 7, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "Executor.h"
#include "log.h"
#include "Thread.h"

#include <errno.h>

/******************************************************************************
 * Job to be executed:
 ******************************************************************************/

typedef struct job {
    /// Future of task submitted to `executor_submitFuture()`
    Future*          future;
    struct executor* executor;   ///< Associated execution service
    struct job*      prev;       ///< Previous job in list
    struct job*      next;       ///< Next job in list
} Job;

// Forward declaration
typedef struct executor Executor;

static Job*
job_new(
        Executor* const restrict executor,
        Future* const restrict   future)
{
    Job* job = log_malloc(sizeof(Job), "job");

    if (job) {
        job->executor = executor;
        job->prev = job->next = NULL;
        job->future = future;
    }

    return job;
}

/**
 * Frees a job. Doesn't free the job's future.
 *
 * @param[in,out] job     Job to be freed
 * @retval        0       Success
 */
static void
job_free(Job* const job)
{
    free(job);
}

// Forward declaration
static void
executor_afterCompletion(
        Executor* const restrict executor,
        Job* const restrict      job);

static void*
job_run(void* const arg)
{
    Job* const job = (Job*)arg;

    (void)future_run(job->future);

    // If this function was executed by a future, then the following wouldn't
    // run if the future was canceled before this function was called. That
    // would be bad.
    executor_afterCompletion(job->executor, job);

    job_free(job); // Doesn't free the job's future

    log_free();

    return NULL;
}

/******************************************************************************
 * Thread-compatible but not thread-safe list of jobs. It allows the
 * execution-service to cancel all running jobs.
 ******************************************************************************/

typedef struct jobList {
    struct job*     head;
    struct job*     tail;
    size_t          size;
} JobList;

/**
 */
static JobList*
jobList_new()
{
    JobList* jobList = log_malloc(sizeof(JobList), "job list");

    if (jobList) {
        jobList->head = jobList->tail = NULL;
        jobList->size = 0;
    } // `jobList` allocated

    return jobList;
}

static void
jobList_free(JobList* const jobList)
{
    Job* job = jobList->head;

    while (job != NULL) {
        Job* next = job->next;
        job_free(job);
        job = next;
    }

    free(jobList);
}

static void
jobList_add(
        JobList* const restrict  jobList,
        Job* const restrict      job)
{
    job->prev = jobList->tail;

    if (jobList->head == NULL)
        jobList->head = job;
    if (jobList->tail)
        jobList->tail->next = job;

    jobList->tail = job;
    jobList->size++;
}

/**
 * Removes a specific job from a list of jobs. Doesn't delete the job.
 *
 * @param[in,out] jobList  List of jobs
 * @param[in]     job      Job to be removed
 * @threadsafety           Compatible but not safe
 */
static void
jobList_remove(
        JobList* const restrict jobList,
        Job* const restrict     job)
{
    Job* const prev = job->prev;
    Job* const next = job->next;

    if (prev) {
        prev->next = next;
    }
    else {
        jobList->head = next;
    }

    if (next) {
        next->prev = prev;
    }
    else {
        jobList->tail = prev;
    }

    jobList->size--;
}

static size_t
jobList_size(JobList* const jobList)
{
    return jobList->size;
}

static int
jobList_cancelAll(JobList* const jobList)
{
    int status = 0;

    for (Job* job = jobList->head; job != NULL; ) {
        Job* next = job->next;
        int  stat = future_cancel(job->future);

        if (stat) {
            log_add("Couldn't cancel job");
            if (status == 0)
                status = stat;
        }

        job = next;
    }

    return status;
}

/******************************************************************************
 * Thread-safe execution service:
 ******************************************************************************/

struct executor {
    pthread_mutex_t mutex;
    JobList*        jobList;
    void*           completer;
    int           (*afterCompletion)(void* completer, Future* future);
    bool            isShutdown;
};

static void
executor_lock(Executor* const executor)
{
    int status = pthread_mutex_lock(&executor->mutex);
    log_assert(status == 0);
}

static void
executor_unlock(Executor* const executor)
{
    int status = pthread_mutex_unlock(&executor->mutex);
    log_assert(status == 0);
}

static int
executor_init(Executor* const executor)
{
    int status;

    executor->isShutdown = false;
    executor->jobList = jobList_new();
    executor->afterCompletion = NULL;
    executor->completer = NULL;

    if (executor->jobList == NULL) {
        log_add("Couldn't create new job list");
        status = ENOMEM;
    }
    else {
        status = mutex_init(&executor->mutex, PTHREAD_MUTEX_ERRORCHECK, true);

        if (status)
            jobList_free(executor->jobList);
    } // `executor->jobList` allocated

    return status;
}

static void
executor_destroy(Executor* const executor)
{
    executor_lock(executor);
        jobList_free(executor->jobList);
    executor_unlock(executor);

    (void)mutex_destroy(&executor->mutex);
}

static void
executor_afterCompletion(
        Executor* const restrict executor,
        Job* const restrict      job)
{
    if (executor->afterCompletion &&
            executor->afterCompletion(executor->completer, job->future))
        log_add("Couldn't process task's future after it completed");

    executor_lock(executor);
        jobList_remove(executor->jobList, job);
    executor_unlock(executor);
}

Executor*
executor_new(void)
{
    Executor* executor = log_malloc(sizeof(Executor), "executor");

    if (executor) {
        if (executor_init(executor)) {
            log_add("Couldn't initialize executor");
            free(executor);
            executor = NULL;
        }
    }

    return executor;
}

void
executor_free(Executor* const executor)
{
    executor_destroy(executor);
    free(executor);
}

void
executor_setAfterCompletion(
        Executor* const restrict executor,
        void* const restrict     completer,
        int                    (*afterCompletion)(
                                     void* restrict   completer,
                                     Future* restrict future))
{
    executor->completer = completer;
    executor->afterCompletion = afterCompletion;
}

static int
executor_submitFuture(
        Executor* const restrict executor,
        Future* const restrict   future)
{
    int        status;
    Job* const job = job_new(executor, future);

    if (job == NULL) {
        log_add("Couldn't create new job");
        status = ENOMEM;
    }
    else {
        executor_lock(executor);
            if (executor->isShutdown) {
                log_add("Executor is shut down");
                status = EPERM;
            }
            else {
                jobList_add(executor->jobList, job);

                pthread_t thread;
                status = pthread_create(&thread, NULL, job_run, job);

                if (status) {
                    log_add_errno(status, "Couldn't create job's thread");
                    jobList_remove(executor->jobList, job);
                }
                else {
                    /*
                     * The thread is detached because it can't be joined:
                     *   - `job_free()` can't join it because it's executing on
                     *     the same thread; and
                     *   - `future_getResult()` can't join it without precluding
                     *     an execution service implementation that uses a
                     *     thread pool.
                     */
                    (void)pthread_detach(thread);
                }
            } // Executor is not shut down
        executor_unlock(executor);

        if (status) {
            job_free(job); // Doesn't free job's future
        }
    } // `job` created

    return status;
}

Future*
executor_submit(
        Executor* const executor,
        void* const     obj,
        int           (*run)(void* obj, void** result),
        int           (*halt)(void* obj, pthread_t thread))
{
    Future* future = future_new(obj, run, halt);

    if (future == NULL) {
        log_add("Couldn't create new future");
    }
    else {
        if (executor_submitFuture(executor, future)) {
            future_free(future);
            future = NULL;
        }
    } // `future` created

    return future;
}

size_t
executor_size(Executor* const executor)
{
    executor_lock(executor);
        size_t size = jobList_size(executor->jobList);
    executor_unlock(executor);

    return size;
}

int
executor_shutdown(
        Executor* const executor,
        const bool      now)
{
    int status;

    executor_lock(executor);
        if (executor->isShutdown) {
            status = 0;
        }
        else {
            executor->isShutdown = true;
            status = now
                    ? jobList_cancelAll(executor->jobList)
                    : 0;
        }
    executor_unlock(executor);

    return status;
}
