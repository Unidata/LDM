/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: executor.c
 * @author: Steven R. Emmerson
 *
 * This file implements an executor of asynchronous jobs.
 */

#include "config.h"

#include "doubly_linked_list.h"
#include "executor.h"
#include "log.h"
#include "queue.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

struct job {
    void*   (*start)(void*); ///< `pthread_create()` start function
    void    (*stop)(void*);  ///< job stop function or `NULL`
    void*     arg;           ///< `pthread_create()` argument or `NULL`
    void*     result;        ///< job result
    Executor* exe;           ///< associated job executor
    DllElt*   elt;           ///< associated element in active-job list
    pthread_t thread;        ///< executing thread
    int       status;        ///< status of execution
    bool      wasCanceled;   ///< whether thread was canceled
};

struct executor {
    Dll*            active;     ///< list of active jobs
    Queue*          completed;  ///< queue of completed jobs
    pthread_mutex_t mutex;      ///< for locking state
    pthread_cond_t  cond;       ///< for signaling state change
    bool            isReady;    ///< whether executor is accepting jobs
};

/**
 * Initializes an executor.
 *
 * @param[in] exe     The executor to be initialized.
 * @retval    0       Success.
 * @retval    ENOMEM  Out-of-memory. `log_add()` called.
 * @retval    EAGAIN  The system lacked the necessary resources (other than
 *                    memory). `log_add()` called.
 * @retval    EBUSY   Logic error. `log_add()` called.
 */
static int exe_init(
        Executor* const exe)
{
    pthread_mutexattr_t mutexAttr;

    int status = pthread_mutexattr_init(&mutexAttr);
    if (status) {
        LOG_ERRNUM0(status, "Couldn't initialize mutex attributes");
    }
    else {
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);
        /*
         * The mutex is recursive mutex because `exe_free()` calls
         * `exe_shutdown()` and `exe_clear()`.
         */
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);

        status = pthread_mutex_init(&exe->mutex, &mutexAttr);
        if (status) {
            LOG_ERRNUM0(status, "Couldn't initialize mutex");
        }
        else {
            status = pthread_cond_init(&exe->cond, NULL);
            if (status) {
                LOG_ERRNUM0(status, "Couldn't initialize condition variable");
            }
            else {
                Dll* active = dll_new();
                if (active == NULL) {
                    status = ENOMEM;
                }
                else {
                    Queue* completed = q_new();
                    if (completed == NULL) {
                        LOG_ADD0("Couldn't allocate completed-job queue");
                        status = ENOMEM;
                    }
                    else {
                        exe->active = active;
                        exe->completed = completed;
                        exe->isReady = true;
                    } // `completed` allocated

                    if (status)
                        dll_free(active);
                } // `active` allocated

                if (status)
                    pthread_cond_destroy(&exe->cond);
            } // `exe->cond` initialized

            if (status)
                pthread_mutex_destroy(&exe->mutex);
        } // `exe->mutex` initialized

        (void)pthread_mutexattr_destroy(&mutexAttr);
    } // `mutexAttr` initialized

    return status;
}

/**
 * Locks an executor's mutex.
 *
 * @param[in] exe      The executor to be locked.
 * @retval    0        Success.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 */
static inline int exe_lock(
        Executor* const exe)
{
    int status = pthread_mutex_lock(&exe->mutex);
    if (status)
        LOG_ERRNUM0(status, "Couldn't lock mutex");
    return status;
}

/**
 * Unlocks an executor's mutex.
 *
 * @param[in] exe      The executor to be unlocked.
 * @retval    0        Success.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EPERM    Logic error. `log_add()` called.
 */
static inline int exe_unlock(
        Executor* const exe)
{
    int status = pthread_mutex_unlock(&exe->mutex);
    if (status)
        LOG_ERRNUM0(status, "Couldn't unlock mutex");
    return status;
}

/**
 * Returns a new executor job.
 *
 * @param[in] start  Starting function for `pthread_create()`.
 * @param[in] arg    Argument for `pthread_create()` and `stop()`.
 * @param[in] stop   Stopping function or NULL.
 * @param[in] exe    The job executor.
 * @retval    NULL   `start == NULL`, `exe == NULL`, or out-of-memory.
 *                   `log_add()` called.
 * @return           Pointer to the new executor job.
 */
static Job* job_new(
        void*              (*const start)(void*),
        void* restrict             arg,
        void               (*const stop)(void*),
        Executor* const restrict   exe)
{
    Job* job;
    if (start == NULL || exe == NULL) {
        job = NULL;
    }
    else {
        job = LOG_MALLOC(sizeof(Job), "asynchronous job");
        if (job) {
            job->arg = arg;
            job->elt = NULL;
            job->exe = exe;
            job->result = NULL;
            job->start = start;
            job->status = 0;
            job->stop = stop;
            job->wasCanceled = false;
        }
    }
    return job;
}

/**
 * Adds a job to a list.
 *
 * @param[in] job     The job.
 * @param[in] list    The list.
 * @retval    0       Success.
 * @retval    ENOMEM  Out-of-memory. `log_add()` called.
 */
static int job_addToList(
        Job* const restrict job,
        Dll* const restrict list)
{
    int           status;
    DllElt* const elt = dll_add(list, job);
    if (elt == NULL) {
        status = ENOMEM;
    }
    else {
        job->elt = elt;
        status = 0;
    }
    return status;
}

/**
 * Removes a job from a list.
 *
 * @param[in] job   The job to be removed.
 * @param[in] list  The list from which to remove the job.
 */
static void job_removeFromList(
        Job* const restrict job,
        Dll* const restrict list)
{
    (void)dll_remove(list, job->elt);
    job->elt = NULL;
}

/**
 * Handles a job that's completed. Moves it from the active job-list to the
 * completed job-queue.
 *
 * @param[in] arg  Pointer to the job.
 */
static void job_completed(
        void* const arg)
{
    Job* const      job = (Job*)arg;
    Executor* const exe = job->exe;
    int             oldState;

    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);

    int status = exe_lock(exe);

    if (status == 0) {
        status = q_enqueue(exe->completed, job);

        if (status) {
            LOG_ADD0("Couldn't enqueue job on completed-job queue");
        }
        else {
            (void)dll_remove(exe->active, job->elt);
            job->elt = NULL;

            status = pthread_cond_broadcast(&exe->cond);
            if (status)
                LOG_ERRNUM0(status, "Couldn't signal condition variable");
        }

        job->status = status;
        (void)exe_unlock(exe);
    }

    int ignoredState;
    (void)pthread_setcancelstate(oldState, &ignoredState);
}

/**
 * Performs cleanup operations on a job whose thread has been canceled.
 *
 * @param[in] arg  Pointer to the job.
 */
static void job_canceled(
        void* const arg)
{
    Job* const      job = (Job*)arg;

    job_completed(job);
    job->wasCanceled = true;
}

/**
 * Called by `pthread_create()`.
 *
 * @pre              `job` is in the executor's active-job list.
 * @param[in] job    The executor job.
 * @retval    NULL   Always.
 * @post             `job` is in the executor's completed-job queue.
 */
static void* job_start(
        void* const arg)
{
    Job* const      job = (Job*)arg;

    pthread_cleanup_push(job_canceled, job);
    job->result = job->start(job->arg);
    pthread_cleanup_pop(0);
    job_completed(job);

    return NULL;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Indicates whether or not the thread that was executing a job was canceled.
 *
 * @param[in] job     The job.
 * @retval    `true`  if and only if the job's thread was canceled.
 */
bool job_wasCanceled(
        Job* const job)
{
    return job->wasCanceled;
}

/**
 * Returns the status of a job from it's executor's perspective.
 *
 * @param[in] job      The job to have its status returned.
 * @retval    0        The job was executed successfully.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @retval    ENOMEM   Out-of-memory.
 */
int job_status(
        Job* const job)
{
    return job->status;
}

/**
 * Returns the result of a job.
 *
 * @param[in] job  The job to have its result returned.
 * @return         The result of the job.
 */
void* job_result(
        Job* const job)
{
    return job->result;
}

/**
 * Frees the resources of a job.
 *
 * @param[in] job  The job to be freed.
 */
void job_free(
        Job* const job)
{
    free(job);
}

/**
 * Returns a new job executor.
 *
 * @retval NULL  Insufficient system resources or logic error. `log_add()`
 *               called.
 * @return       Pointer to new job executor.
 */
Executor* exe_new(void)
{
    Executor* exe = LOG_MALLOC(sizeof(Executor), "job executor");

    if (exe != NULL) {
        int status = exe_init(exe);
        if (status) {
            free(exe);
            exe = NULL;
        }
    }

    return exe;
}

/**
 * Submits a job for asynchronous execution.
 *
 * @param[in] exe      The executor.
 * @param[in] start    Starting function for `pthread_create()`.
 * @param[in] arg      Argument for `pthread_create()` and `stop()`.
 * @param[in] stop     Stopping function or NULL.
 * @param[out] job     The job.
 * @retval    0        Success. `*job` is set.
 * @retval    EINVAL   `exe` is shut down. `log_add()` called.
 * @retval    EINVAL   `exe` is uninitialized. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @retval    ENOMEM   Out of memory. `log_add()` called.
 * @retval    EAGAIN   System lacked resources. `log_add()` called.
 */
int exe_submit(
        Executor* const restrict exe,
        void*            (*const start)(void*),
        void* restrict           arg,
        void             (*const stop)(void*),
        Job** const restrict     job)
{
    int status = exe_lock(exe);

    if (0 == status) {
        if (!exe->isReady) {
            LOG_START0("Job executor is shut down");
            status = EINVAL;
        }
        else {
            Job* const jb = job_new(start, arg, stop, exe);

            if (jb == NULL) {
                status = ENOMEM;
            }
            else {
                if (job_addToList(jb, exe->active) == 0) {
                    status = pthread_create(&jb->thread, NULL, job_start, jb);
                    if (status) {
                        LOG_ERRNUM0(status, "Couldn't create new thread");
                        job_removeFromList(jb, exe->active);
                        job_free(jb);
                    }
                    else {
                        (void)pthread_detach(jb->thread);
                        *job = jb;
                    }
                }
            }
        }
        (void)exe_unlock(exe);
    }

    return status;
}

/**
 * Removes and returns the next completed job.
 *
 * @param[in] exe      The executor.
 * @param[in] job      The next completed job.
 * @retval    0        Success.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EINVAL   `exe` is uninitialized. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EPERM    Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 */
int exe_getCompleted(
        Executor* const restrict exe,
        Job** const restrict     job)
{
    int status = exe_lock(exe);

    if (status == 0) {
        int  status = 0;

        while (status == 0 && q_size(exe->completed) == 0)
            status = pthread_cond_wait(&exe->cond, &exe->mutex);

        if (status) {
            LOG_ERRNUM0(status, "Couldn't wait on condition variable");
        }
        else {
            *job = q_dequeue(exe->completed);
        }

        (void)exe_unlock(exe);
    } // Executor is locked

    return status;
}

/**
 * Shuts down an executor. Calls the stop function or cancels the thread of all
 * active jobs. Blocks until all jobs have completed.
 *
 * @param[in] exe     The executor.
 * @retval    0       Success.
 * @retval    EAGAIN  Logic error. `log_add()` called.
 * @retval    EINVAL  The executor is already shut down. `log_add()` called.
 */
int exe_shutdown(
        Executor* const restrict exe)
{
    int status = exe_lock(exe);

    if (status == 0) {
        if (!exe->isReady) {
            LOG_START0("Executor is already shut down");
            status = EINVAL;
        }
        else {
            for (Job* job = (Job*)dll_getFirst(exe->active); job;
                    job = (Job*)dll_getFirst(exe->active)) {
                if (job->stop) {
                    job->stop(job->arg);
                }
                else {
                    (void)pthread_cancel(job->thread);
                }
            }

            while (dll_size(exe->active) > 0)
                (void)pthread_cond_wait(&exe->cond, &exe->mutex);

            exe->isReady = false;
        }

        (void)exe_unlock(exe);
    } // Executor is locked

    return status;
}

/**
 * Clears the queue of completed jobs in an executor. The executor must be shut
 * down. After this call, jobs may again be submitted to the executor.
 *
 * @param[in] exe      The executor to be cleared.
 * @retval    0        Success.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EINVAL   The executor isn't shut down. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 */
int exe_clear(
        Executor* const restrict exe)
{
    int status = exe_lock(exe);

    if (status == 0) {
        if (exe->isReady) {
            LOG_START0("Executor is not shut down");
            status = EINVAL;
        }
        else {
            for (Job* job = (Job*)q_dequeue(exe->completed); job;
                    job = (Job*)q_dequeue(exe->completed))
                job_free(job);
            exe->isReady = true;
        }
    } // Executor is locked

    return status;
}

/**
 * Frees an executor's resources. Shuts down the executor and clears it if
 * necessary.
 *
 * @param[in] exe  The excutor to be freed or NULL.
 */
void exe_free(
        Executor* const exe)
{
    if (exe) {
        int status = exe_lock(exe);

        if (status == 0) {
            if (exe->isReady)
                (void)exe_shutdown(exe);
            (void)exe_clear(exe);
            free(exe);
        }
    }
}
