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
#include "../../misc/Executor.h"

#include "config.h"

#include "log.h"
#include <errno.h>
#include "../../misc/Thread.h"

/******************************************************************************
 * Job to be executed:
 ******************************************************************************/

typedef struct job {
    /// Future of task submitted to `executor_submitFuture()`
    Future*          future;
    struct executor* executor; ///< Associated execution service
    struct job*      prev;     ///< Previous job in list
    struct job*      next;     ///< Next job in list
} Job;

static Job*
job_new(
        struct executor* const restrict executor,
        Future* const restrict          future)
{
    Job* job = log_malloc(sizeof(Job), "job");

    if (job) {
        job->future = future;
        job->executor = executor;
        job->prev = job->next = NULL;
    }

    return job;
}

static void
job_delete(Job* const job)
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

    executor_afterCompletion(job->executor, job);

    log_free();

    return NULL;
}

static int
job_cancel(Job* const job)
{
    return future_cancel(job->future);
}

/******************************************************************************
 * Thread-compatible but not thread-safe list of jobs:
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
        job_delete(job);
        job = next;
    }

    free(jobList);
}

static Job*
jobList_add(
        JobList* const restrict  jobList,
        Executor* const restrict executor,
        Future* const restrict   future)
{
    Job* const job = job_new(executor, future);

    if (job) {
        job->prev = jobList->tail;

        if (jobList->head == NULL)
            jobList->head = job;
        if (jobList->tail)
            jobList->tail->next = job;

        jobList->tail = job;
        jobList->size++;
    }

    return job;
}

/**
 * Removes a specific job from a list of jobs. Doesn't delete the job.
 *
 * @param[in,out] jobList  List of jobs
 * @param[in]     job      Job to be removed
 * @threadsafety           Safe
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

static Future*
jobList_removeHead(JobList* const jobList)
{
    Future* future;

    Job* const    headJob = jobList->head;

    if (headJob == NULL) {
        future = NULL;
    }
    else {
        future = headJob->future;

        jobList_remove(jobList, headJob);
        job_delete(headJob); // Doesn't delete future
    }

    return future;
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
        int  stat = job_cancel(job);

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
 * Iterator over list of futures
 ******************************************************************************/

struct futuresIter {
    JobList*         jobList;
    pthread_mutex_t* mutex;
};

static FuturesIter*
futuresIter_new(
        JobList* const restrict         jobList,
        pthread_mutex_t* const restrict mutex)
{
    FuturesIter* iter = log_malloc(sizeof(FuturesIter),
            "iterator over list of waiting futures");

    if (iter) {
        iter->mutex = mutex;
        iter->jobList = jobList;
    }

    return iter;
}

void
futuresIter_delete(FuturesIter* const iter)
{
    free(iter);
}

Future*
futuresIter_remove(FuturesIter* const iter)
{
    mutex_lock(iter->mutex);
        Future* future;
        while ((future = jobList_removeHead(iter->jobList)) &&
                future_runFuncCalled(future))
            ;
    mutex_unlock(iter->mutex);

    return future;
}

/******************************************************************************
 * Thread-safe execution service:
 ******************************************************************************/

struct executor {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
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
executor_init(
        Executor* const executor,
        int           (*afterCompletion)(
                            void* const restrict   completer,
                            Future* const restrict future),
        void* const     completer)
{
    int status;

    executor->isShutdown = false;
    executor->jobList = jobList_new();
    executor->afterCompletion = afterCompletion;
    executor->completer = completer;

    if (executor->jobList == NULL) {
        log_add("Couldn't create new job list");
        status = ENOMEM;
    }
    else {
        status = pthread_cond_init(&executor->cond, NULL);

        if (status) {
            log_add_errno(status, "Couldn't initialize condition-variable");
        }
        else {
            status = mutex_init(&executor->mutex, PTHREAD_MUTEX_ERRORCHECK,
                    true);

            if (status)
                (void)pthread_cond_destroy(&executor->cond);
        } // `executor->cond` created

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

Executor*
executor_new(
        int       (*afterCompletion)(
                        void* restrict   completer,
                        Future* restrict future),
        void* const completer)
{
    Executor* executor = log_malloc(sizeof(Executor), "executor");

    if (executor) {
        if (executor_init(executor, afterCompletion, completer)) {
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

/**
 * Submits a job's future to be executed asynchronously.
 *
 * @param[in,out] exec      Execution service
 * @param[in,out] future    Job's future. Caller should call `future_delete()`
 *                          when it's no longer needed.
 * @retval        0         Success
 * @retval        EAGAIN    Insufficient system resources. `log_add()` called.
 * @retval        ENOMEM    Out of memory. `log_add()` called.
 * @retval        EPERM     Execution service is shut down. `log_add()` called.
 */
static int
executor_submitFuture(
        Executor* const restrict executor,
        Future* const restrict   future)
{
    int status;

    executor_lock(executor);
        if (executor->isShutdown) {
            log_add("Executor is shut down");
            status = EPERM;
        }
        else {
            Job* job = jobList_add(executor->jobList, executor, future);

            if (job == NULL) {
                log_add("Couldn't add future to job list");
                status = ENOMEM;
            }
            else {
                pthread_t thread;
                status = pthread_create(&thread, NULL, job_run, job);

                if (status) {
                    log_add_errno(status, "Couldn't create job thread");
                    jobList_remove(executor->jobList, job);
                    job_delete(job); // Doesn't delete future
                }
                else {
                    future_setThread(future, thread);
                    (void)pthread_cond_signal(&executor->cond);
                }
            } // `job` created
        } // Executor is not shut down
    executor_unlock(executor);

    return status;
}

Future*
executor_submit(
        Executor* const executor,
        void* const     obj,
        int           (*runFunc)(void* obj, void** result),
        int           (*haltFunc)(void* obj, pthread_t thread))
{
    Future* future = future_new(obj, runFunc, haltFunc);

    if (future == NULL) {
        log_add("Couldn't create new future");
    }
    else if (executor_submitFuture(executor, future)) {
        log_add("Couldn't submit task for execution");
        future_free(future);
        future = NULL;
    } // `future` created

    return future;
}

static void
executor_afterCompletion(
        Executor* const restrict executor,
        Job* const restrict      job)
{
    if (executor->afterCompletion &&
            executor->afterCompletion(executor->completer, job->future))
        log_add("Couldn't process task after it completed");

    executor_lock(executor);
        jobList_remove(executor->jobList, job);
    executor_unlock(executor);

    job_delete(job); // Doesn't delete future
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
            executor_unlock(executor);
            status = 0;
        }
        else {
            executor->isShutdown = true;
            status = now
                    ? jobList_cancelAll(executor->jobList)
                    : 0;

            executor_unlock(executor);
        }

    return status;
}
