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
    void*         (*start)(void*); ///< `pthread_create()` start function
    void          (*stop)(void*);  ///< job stop function or `NULL`
    void*           arg;           ///< `pthread_create()` argument or `NULL`
    void*           result;        ///< job result
    Executor*       exe;           ///< associated executor
    DllElt*         elt;           ///< associated element in active-job list
    pthread_t       thread;        ///< executing thread
    int             status;        ///< status of execution
    bool            wasCanceled;   ///< whether thread was canceled
    pthread_mutex_t mutex;         ///< for inter-thread visibility of members
};

struct executor {
    Dll*            active;     ///< list of active jobs
    Queue*          completed;  ///< queue of completed jobs
    pthread_mutex_t mutex;      ///< for locking state
    pthread_cond_t  cond;       ///< for signaling state change
    bool            isReady;    ///< whether executor is accepting jobs
};

/**
 * Initializes a mutex. The mutex will have protocol `PTHREAD_PRIO_INHERIT` and
 * type `PTHREAD_MUTEX_ERRORCHECK`.
 *
 * @param[in] mutex      The mutex.
 * @retval    0          Success.
 * @retval    EAGAIN     Insufficient system resources other than memory.
 *                       `log_add()` called.
 * @retval    EBUSY      Logic error. `log_add()` called.
 * @retval    ENOMEM     Out-of-memory. `log_add()` called.
 * @retval    EPERM      Logic error. `log_add()` called.
 */
static int mutex_init(
        pthread_mutex_t* const mutex)
{
    pthread_mutexattr_t mutexAttr;
    int                 status = pthread_mutexattr_init(&mutexAttr);

    if (status) {
        LOG_ERRNUM0(status, "Couldn't initialize mutex attributes");
    }
    else {
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);
        (void)pthread_mutexattr_settype(&mutexAttr,  PTHREAD_MUTEX_ERRORCHECK);

        status = pthread_mutex_init(mutex, &mutexAttr);
        if (status)
            LOG_ERRNUM0(status, "Couldn't initialize mutex");

        (void)pthread_mutexattr_destroy(&mutexAttr);
    }

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
static int exe_lock(
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
static int exe_unlock(
        Executor* const exe)
{
    int status = pthread_mutex_unlock(&exe->mutex);
    if (status)
        LOG_ERRNUM0(status, "Couldn't unlock mutex");
    return status;
}

/**
 * Locks a job's mutex.
 *
 * @param[in] exe      The job to be locked.
 * @retval    0        Success.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 */
static int job_lock(
        Job* const job)
{
    int status = pthread_mutex_lock(&job->mutex);
    if (status)
        LOG_ERRNUM0(status, "Couldn't lock mutex");
    return status;
}

/**
 * Unlocks a job's mutex.
 *
 * @param[in] job      The job to be unlocked.
 * @retval    0        Success.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EPERM    Logic error. `log_add()` called.
 */
static int job_unlock(
        Job* const job)
{
    int status = pthread_mutex_unlock(&job->mutex);
    if (status)
        LOG_ERRNUM0(status, "Couldn't unlock mutex");
    return status;
}

/**
 * Returns a new job.
 *
 * @param[in] start  Starting function for `pthread_create()`.
 * @param[in] arg    Argument for `pthread_create()` and `stop()`.
 * @param[in] stop   Stopping function or NULL.
 * @param[in] exe    The job executor.
 * @retval    NULL   `start == NULL`, `exe == NULL`, out-of-memory, or logic
 *                   error. `log_add()` called.
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
            int status = mutex_init(&job->mutex);

            if (status) {
                free(job);
                job = NULL;
            }
            else {
                job->arg = arg;
                job->elt = NULL;
                job->exe = exe;
                job->result = NULL;
                job->start = start;
                job->status = 0;
                job->stop = stop;
                job->wasCanceled = false;
            }
        } // `job` allocated
    }

    return job;
}

/**
 * Adds a job to a list.
 *
 * @pre                The job is unlocked.
 * @param[in] job      The job.
 * @param[in] list     The list.
 * @retval    0        Success.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @post               The job is unlocked.
 */
static int job_addToList(
        Job* const restrict job,
        Dll* const restrict list)
{
    int status = job_lock(job);

    if (status == 0) {
        DllElt* const elt = dll_add(list, job);

        if (elt == NULL) {
            status = ENOMEM;
        }
        else {
            job->elt = elt;
            status = 0;
        }

        job_unlock(job);
    }

    return status;
}

/**
 * Removes a job from a list. Assumes the job is locked.
 *
 * @pre                The job is locked.
 * @param[in] job      The job to be removed.
 * @param[in] list     The list from which to remove the job.
 * @post               The job is locked.
 */
static void removeFromList(
        Job* const restrict job,
        Dll* const restrict list)
{
    (void)dll_remove(list, job->elt);
    job->elt = NULL;
}

/**
 * Removes a job from a list. Assumes the job is unlocked.
 *
 * @pre                The job is unlocked.
 * @param[in] job      The job to be removed.
 * @param[in] list     The list from which to remove the job.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @post               The job is unlocked.
 */
static int job_removeFromList(
        Job* const restrict job,
        Dll* const restrict list)
{
    int status = job_lock(job);

    if (status == 0) {
        removeFromList(job, list);
        job_unlock(job);
    }

    return status;
}

/**
 * Performs final operations on a completed job.
 *
 * @pre                Job is locked.
 * @pre                Associated executor is unlocked.
 * @pre                Job is in `active` list.
 * @param[in] job      The completed job.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @post               Job is in `completed` queue.
 * @post               `job->status` is set.
 * @post               Associated executor is unlocked.
 * @post               Job is locked.
 */
static int job_completed(
        Job* const job)
{
    int oldState;
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);

    Executor* const exe = job->exe;
    int             status = exe_lock(exe);

    if (status) {
        LOG_ADD0("Couldn't lock executor");
    }
    else {
        status = q_enqueue(exe->completed, job);

        if (status) {
            LOG_ADD0("Couldn't enqueue job on completed-job queue");
        }
        else {
            removeFromList(job, exe->active); // job locked version

            status = pthread_cond_broadcast(&exe->cond);
            if (status)
                LOG_ERRNUM0(status, "Couldn't signal condition variable");
        }

        job->status = status;
        (void)exe_unlock(exe);
    } // `exe` is locked

    if (status)
        log_log(LOG_ERR);
    log_free();

    int ignoredState;
    (void)pthread_setcancelstate(oldState, &ignoredState);

    return status;
}

/**
 * Performs cleanup operations on a job whose thread has been canceled.
 *
 * @pre            Job is locked.
 * @pre            Associated executor is unlocked.
 * @pre            Job is in `active` list.
 * @param[in] arg  Pointer to the job.
 * @post           Job is in `completed` queue.
 * @post           `wasCanceled == true`.
 * @post           Associated executor is unlocked.
 * @post           Job is unlocked.
 */
static void job_canceled(
        void* const arg)
{
    Job* const      job = (Job*)arg;

    (void)job_completed(job);
    job->wasCanceled = true;

    (void)job_unlock(job); // because this is the executing thread's last action
}

/**
 * Executes a job. Called by `pthread_create()`.
 *
 * @pre              `job` is in the executor's active-job list.
 * @param[in] arg    Pointer to the job to execute.
 * @retval    NULL   Always.
 * @post             `job` is in the executor's completed-job queue.
 * @post             `wasCanceled == true` if and only if the job was canceled.
 * @post             `job->result` is set if and only if the job wasn't
 *                   canceled.
 * @post             `job->status` is set.
 */
static void* job_start(
        void* const arg)
{
    Job* const job = (Job*)arg;
    int        status = job_lock(job);

    if (status == 0) {
        pthread_cleanup_push(job_canceled, job);
        job->result = job->start(job->arg);
        pthread_cleanup_pop(0); // use `job_completed()` instead
        job_completed(job);
        (void)job_unlock(job);
    } // `job` is locked

    return NULL;
}

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
    int status = mutex_init(&exe->mutex);

    if (status == 0) {
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

    return status;
}

/**
 * Shuts down an executor. Calls the stop function or cancels the thread of all
 * active jobs. Blocks until all jobs have completed.
 *
 * @pre                The executor is locked.
 * @param[in] exe      The executor.
 * @retval    0        Success.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EINVAL   The executor is already shut down. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @post               The executor is locked.
 */
int shutdown(
        Executor* const restrict exe)
{
    int status;

    if (!exe->isReady) {
        LOG_START0("Executor is already shut down");
        status = EINVAL;
    }
    else {
        DllIter* iter = dll_iter(exe->active);

        if (iter == NULL) {
            status = ENOMEM;
        }
        else {
            while (dll_hasNext(iter)) {
                Job* const job = (Job*)dll_next(iter);
                if (job->stop) {
                    job->stop(job->arg);
                }
                else {
                    (void)pthread_cancel(job->thread);
                }
            }
            dll_freeIter(iter);

            while (dll_size(exe->active) > 0)
                (void)pthread_cond_wait(&exe->cond, &exe->mutex);

            exe->isReady = false;
            status = 0;
        }
    }

    return status;
}

/**
 * Clears the queue of completed jobs in an executor. The executor must be shut
 * down. After this call, jobs may again be submitted to the executor.
 *
 * @pre                The executor is locked.
 * @param[in] exe      The executor to be cleared.
 * @retval    0        Success.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EBUSY    Logic error. `log_add()` called.
 * @retval    EINVAL   The executor isn't shut down. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @post               The executor is locked.
 */
int clear(
        Executor* const restrict exe)
{
    int status;

    if (exe->isReady) {
        LOG_START0("Executor is not shut down");
        status = EINVAL;
    }
    else {
        status = 0;

        for (Job* job = (Job*)q_dequeue(exe->completed); job;
                job = (Job*)q_dequeue(exe->completed)) {
            int tmpStatus = job_free(job);
            if (status == 0)
                status = tmpStatus;
        }
        exe->isReady = true;
    }

    return status;
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
    (void)job_lock(job);
    bool wasCanceled = job->wasCanceled;
    (void)job_unlock(job);
    return wasCanceled;
}

/**
 * Returns the status of a job from it's executor's perspective.
 *
 * @param[in] job      The job to have its status returned.
 * @retval    0        The job was executed successfully.
 * @retval    EAGAIN   System lacked resources (other than memory). `log_add()`
 *                     called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @retval    ENOMEM   Out-of-memory.
 */
int job_status(
        Job* const job)
{
    (void)job_lock(job);
    int status = job->status;
    (void)job_unlock(job);
    return status;
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
    (void)job_lock(job);
    void* result = job->result;
    (void)job_unlock(job);
    return result;
}

/**
 * Frees the resources of a job.
 *
 * @param[in] job     The job to be freed or NULL.
 * @retval    0       Success.
 * @retval    EBUSY   Logic error. `log_add()` called.
 * @retval    EINVAL  Logic error. `log_add()` called.
 */
int job_free(
        Job* const job)
{
    int status;

    if (job == 0) {
        status = 0;
    }
    else {
        status = pthread_mutex_destroy(&job->mutex);
        if (status)
            LOG_ERRNUM0(status, "Couldn't destroy job mutex");
        free(job);
    }

    return status;
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
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EBUSY    Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    ENOMEM   Out of memory. `log_add()` called.
 */
int exe_submit(
        Executor* const restrict exe,
        void*            (*const start)(void*),
        void* restrict           arg,
        void             (*const stop)(void*),
        Job** const restrict     job)
{
    int status = exe_lock(exe);

    if (status) {
        LOG_ADD0("Couldn't lock executor");
    }
    else {
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
                        status = job_removeFromList(jb, exe->active);
                        int tmpStatus = job_free(jb);
                        if (status == 0)
                            status = tmpStatus;
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
 * @retval    EAGAIN   System lacked resources (other than memory). `log_add()`
 *                     called.
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

    if (status) {
        LOG_ADD0("Couldn't lock executor");
    }
    else {
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
 * @param[in] exe      The executor.
 * @retval    0        Success.
 * @retval    EAGAIN   Logic error. `log_add()` called.
 * @retval    EINVAL   The executor is already shut down. `log_add()` called.
 * @retval    EINVAL   Logic error. `log_add()` called.
 * @retval    EDEADLK  Logic error. `log_add()` called.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 */
int exe_shutdown(
        Executor* const restrict exe)
{
    int status = exe_lock(exe);

    if (status) {
        LOG_ADD0("Couldn't lock executor");
    }
    else {
        status = shutdown(exe);
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

    if (status) {
        LOG_ADD0("Couldn't lock executor");
    }
    else {
        status = clear(exe);
        (void)exe_unlock(exe);
    } // Executor is locked

    return status;
}

/**
 * Frees an executor's resources. Shuts down the executor and clears it if
 * necessary.
 *
 * @param[in] exe      The executor to be freed or NULL.
 * @retval    0        Success or `exe == NULL`.
 * @retval    EAGAIN   Logic error. `log_add()` called. `exe` in unknown state.
 * @retval    EINVAL   Logic error. `log_add()` called. `exe` in unknown state.
 * @retval    EDEADLK  Logic error. `log_add()` called. `exe` in unknown state.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 */
int exe_free(
        Executor* const exe)
{
    int status;

    if (exe == NULL) {
        status = 0;
    }
    else {
        status = exe_lock(exe);

        if (status) {
            LOG_ADD0("Couldn't lock executor");
        }
        else {
            if (exe->isReady)
                status = shutdown(exe);

            if (status == 0) {
                status = clear(exe);

                if (status == 0) {
                    q_free(exe->completed);
                    dll_free(exe->active);
                    (void)pthread_cond_destroy(&exe->cond);
                    (void)pthread_mutex_destroy(&exe->mutex);
                    free(exe);
                }
            }
        }
    }

    return status;
}
