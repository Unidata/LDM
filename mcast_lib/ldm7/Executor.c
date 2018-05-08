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
    Future*         future;  ///< Job future
    struct jobList* jobList; ///< List of jobs containing this one
    struct job*     prev;    ///< Previous job in list
    struct job*     next;    ///< Next job in list
} Job;

static Job*
job_new(
        struct jobList* const restrict jobList,
        Future* const restrict         future)
{
    Job* job = log_malloc(sizeof(Job), "job");

    if (job) {
        job->future = future;
        job->jobList = jobList;
        job->prev = job->next = NULL;
    }

    return job;
}

static void
job_delete(Job* const job)
{
    free(job);
}

static void jobList_remove(Job* const job);

static void*
job_run(void* const arg)
{
    Job* const   job = (Job*)arg;

    future_run((Future*)job->future);

    jobList_remove(job); // Deletes `job`

    return NULL;
}

static void
job_cancel(Job* const job)
{
    future_cancel(job->future);
}

static void
job_wait(Job* const job)
{
    future_wait(job->future, NULL);
}

/******************************************************************************
 * Thread-safe list of jobs:
 ******************************************************************************/

typedef struct jobList {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    struct job*     head;
    struct job*     tail;
} JobList;

static void
jobList_lock(JobList* const jobList)
{
    int status = pthread_mutex_lock(&jobList->mutex);
    log_assert(status == 0);
}

static void
jobList_unlock(JobList* const jobList)
{
    int status = pthread_mutex_unlock(&jobList->mutex);
    log_assert(status == 0);
}

/**
 */
static JobList*
jobList_new()
{
    JobList* jobList = log_malloc(sizeof(JobList), "job list");

    if (jobList) {
        jobList->head = jobList->tail = NULL;

        int status = pthread_cond_init(&jobList->cond, NULL);

        if (status) {
            log_add("Couldn't initialize condition-variable");
        }
        else {
            status = mutex_init(&jobList->mutex, PTHREAD_MUTEX_ERRORCHECK,
                    true);

            if (status)
                (void)pthread_cond_destroy(&jobList->cond);
        } // `jobList->cond` initialized

        if (status) {
            free(jobList);
            jobList = NULL;
        }
    } // `jobList` allocated

    return jobList;
}

static void
jobList_delete(JobList* const jobList)
{
    jobList_lock(jobList);
        Job* job = jobList->head;

        while (job != NULL) {
            Job* next = job->next;
            job_delete(job);
            job = next;
        }

        (void)pthread_cond_destroy(&jobList->cond);
    jobList_unlock(jobList);

    (void)pthread_mutex_destroy(&jobList->mutex);

    free(jobList);
}

static Job*
jobList_add(
        JobList* const restrict jobList,
        Future* const restrict  future)
{
    Job* const job = job_new(jobList, future);

    if (job) {
        jobList_lock(jobList);
            job->prev = jobList->tail;

            if (jobList->head == NULL)
                jobList->head = job;
            if (jobList->tail)
                jobList->tail->next = job;

            jobList->tail = job;
        jobList_unlock(jobList);
    }

    return job;
}

static void
jobList_remove(Job* const restrict job)
{
    Job* const     prev = job->prev;
    Job* const     next = job->next;
    JobList* const jobList = job->jobList;

    jobList_lock(jobList);
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

        // `jobList_shutdown()` must be notified
        (void)pthread_cond_signal(&jobList->cond);
    jobList_unlock(jobList);

    job_delete(job);
}

static void
jobList_shutdown(JobList* const jobList)
{
    jobList_lock(jobList);
        Job* next;

        for (Job* job = jobList->head; job != NULL; job = next) {
            next = job->next;
            job_cancel(job);
        }

        while (jobList->head != NULL)
            (void)pthread_cond_wait(&jobList->cond, &jobList->mutex);
    jobList_unlock(jobList);
}

/******************************************************************************
 * Executor:
 ******************************************************************************/

struct executor {
    pthread_mutex_t mutex;
    JobList*        jobList;
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

    if (executor->jobList == NULL) {
        log_add("Couldn't create new job list");
        status = ENOMEM;
    }
    else {
        status = mutex_init(&executor->mutex, PTHREAD_MUTEX_ERRORCHECK, true);

        if (status)
            jobList_delete(executor->jobList);
    } // `executor->jobList` allocated

    return status;
}

static void
executor_deinit(Executor* const executor)
{
    executor_lock(executor);
        jobList_delete(executor->jobList);
    executor_unlock(executor);

    (void)pthread_mutex_destroy(&executor->mutex);
}

Executor*
executor_new()
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
executor_delete(Executor* const executor)
{
    executor_deinit(executor);
    free(executor);
}

Future*
executor_submit(
        Executor* const executor,
        void* const     obj,
        void*         (*runFunc)(void* obj),
        bool          (*haltFunc)(void* obj, pthread_t thread))
{
    Future* future = future_new(obj, runFunc, haltFunc);

    if (future == NULL) {
        log_add("Couldn't create new future");
    }
    else {
        executor_lock(executor);
            int  status;

            if (executor->isShutdown) {
                log_add("Executor is shut down");
                status = EPERM;
            }
            else {
                Job* job = jobList_add(executor->jobList, future);

                if (job == NULL) {
                    log_add("Couldn't add future to job list");
                }
                else {
                    pthread_t thread;
                    status = pthread_create(&thread, NULL, job_run, job);
                    (void)pthread_detach(thread);

                    if (status) {
                        log_add_errno(status, "Couldn't create job thread");
                        jobList_remove(job);
                    }
                } // `job` created
            } // Executor is not shut down
        executor_unlock(executor);

        if (status) {
            future_delete(future);
            future = NULL;
        }
    } // `future` created

    return future;
}

void
executor_shutdown(
        Executor* const executor,
        const bool      now)
{
    executor_lock(executor);
        if (!executor->isShutdown) {
            executor->isShutdown = true;

            if (now)
                jobList_shutdown(executor->jobList);
        }
    executor_unlock(executor);
}
