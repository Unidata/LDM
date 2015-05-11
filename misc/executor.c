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
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>

struct job {
    void*         (*start)(void*); ///< `pthread_create()` start function
    void          (*stop)(void*);  ///< job stop function or `NULL`
    void*           arg;           ///< `pthread_create()` argument or `NULL`
    void*           result;        ///< job result
    Executor*       exe;           ///< associated executor
    DllElt*         elt;           ///< associated element in active-job list
    pthread_t       thread;        ///< executing thread
    enum {
        JOB_PENDING,
        JOB_EXECUTING,
        JOB_COMPLETED
    }               state;         ///< state of the job
    int             status;        ///< status of execution
    bool            wasStopped;    ///< whether or not a job was stopped
    pthread_mutex_t mutex;         ///< for inter-thread visibility of members
    bool            locked;
};

struct executor {
    Dll*            active;     ///< list of active jobs
    Queue*          completed;  ///< queue of completed jobs
    size_t          count;      ///< total number of jobs
    pthread_cond_t  cond;       ///< for signaling executor state change
    pthread_mutex_t mutex;      ///< for locking executor state
    enum {
        EXE_ACTIVE,
        EXE_SHUTTING_DOWN,
        EXE_SHUTDOWN,
        EXE_BEING_FREED
    }               state;
    int             errCode;    ///< error number
    bool            locked;
};

/**
 * Initializes a mutex. The mutex will have protocol `PTHREAD_PRIO_INHERIT` and
 * type `PTHREAD_MUTEX_ERRORCHECK`.
 *
 * @param[in] mutex      The mutex.
 * @retval    0          Success.
 * @retval    EAGAIN     Insufficient system resources other than memory.
 *                       `log_add()` called.
 * @retval    ENOMEM     Out-of-memory. `log_add()` called.
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
        status = pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);
        UASSERT(status != EPERM);
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_ERRORCHECK);

        status = pthread_mutex_init(mutex, &mutexAttr);
        UASSERT(status != EPERM);
        UASSERT(status != EBUSY);
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
 */
static void exe_lock(
        Executor* const exe)
{
    int status = pthread_mutex_lock(&exe->mutex);
    UASSERT(status == 0);
    exe->locked = true;
}

/**
 * Unlocks an executor's mutex.
 *
 * @param[in] exe      The executor to be unlocked.
 */
static void exe_unlock(
        Executor* const exe)
{
    exe->locked = false;
    int status = pthread_mutex_unlock(&exe->mutex);
    UASSERT(status == 0);
}

/**
 * Locks a job's mutex.
 *
 * @param[in] exe      The job to be locked.
 */
static void job_lock(
        Job* const job)
{
    int status = pthread_mutex_lock(&job->mutex);
    UASSERT(status == 0);
    job->locked = true;
}

/**
 * Unlocks a job's mutex.
 *
 * @param[in] job      The job to be unlocked.
 */
static void job_unlock(
        Job* const job)
{
    job->locked = false;
    int status = pthread_mutex_unlock(&job->mutex);
    UASSERT(status == 0);
}

/**
 * Initializes a job. The job does not start executing.
 *
 * @param[out] job     The job.
 * @param[in]  start   Starting function for `pthread_create()`.
 * @param[in]  arg     Argument for `pthread_create()` and `stop()`.
 * @param[in]  stop    Stopping function or NULL.
 * @retval     0       Success.
 * @retval     EAGAIN  Insufficient system resources other than memory.
 *                     `log_add()` called.
 */
static int job_init(
        Job* const restrict       job,
        void*             (*const start)(void*),
        void* restrict            arg,
        void              (*const stop)(void*))
{
    UASSERT(job != NULL && start != NULL);

    int status = mutex_init(&job->mutex);

    if (status) {
        LOG_ADD0("Couldn't initialize job's mutex");
    }
    else {
        job->arg = arg;
        job->elt = NULL;
        job->exe = NULL;
        job->result = NULL;
        job->start = start;
        job->state = JOB_PENDING;
        job->status = 0;
        job->stop = stop;
        job->wasStopped = false;
        job->locked = false;
    }

    return status;
}

/**
 * Returns a new job. The job is not executing.
 *
 * @param[out] job     The job.
 * @param[in]  start   Starting function for `pthread_create()`.
 * @param[in]  arg     Argument for `pthread_create()` and `stop()`.
 * @param[in]  stop    Stopping function or NULL.
 * @retval     0       Success. `*job` is set. The caller should call
 *                     `job_free(*job)` when it's no longer needed.
 * @retval     EAGAIN  Insufficient system resources other than memory.
 *                     `log_add()` called.
 * @retval     ENOMEM  Out-of-memory. `log_add()` called.
 */
static int job_new(
        Job** const restrict       job,
        void*              (*const start)(void*),
        void* restrict             arg,
        void               (*const stop)(void*))
{
    Job* const jb = LOG_MALLOC(sizeof(Job), "job");
    int        status;

    if (jb == NULL) {
        status = ENOMEM;
    }
    else {
        status = job_init(jb, start, arg, stop);

        if (status) {
            free(jb);
        }
        else {
            *job = jb;
        }
    } // `jb` allocated

    return status;
}

/**
 * Adds a job to an executor's active list.
 *
 * @pre                The executor is locked.
 * @pre                The job is unlocked.
 * @pre                `job->state == PENDING`
 * @param[in] job      The job.
 * @param[in] exe      The executor.
 * @retval    0        Success.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @post               The job is in the executor's active list.
 * @post               The job is unlocked.
 * @post               The executor is locked.
 */
static int job_addToList(
        Job* const restrict      job,
        Executor* const restrict exe)
{
    UASSERT(exe->locked);
    int status;

    job_lock(job);

    UASSERT(job->state == JOB_PENDING);

    DllElt* const elt = dll_add(exe->active, job);

    if (elt == NULL) {
        status = ENOMEM;
    }
    else {
        job->exe = exe;
        job->elt = elt;
        status = 0;
    }

    job_unlock(job);

    return status;
}

/**
 * Removes a job from a list.
 *
 * @param[in] job      The job to be removed.
 * @param[in] list     The list from which to remove the job.
 */
static void removeFromList(
        Job* const restrict job,
        Dll* const restrict list)
{
    (void)dll_remove(list, job->elt);
    job->elt = NULL;
}

/**
 * Removes a job from its executor's active list. Assumes the job is unlocked.
 *
 * @pre                The executor is locked.
 * @pre                The job is unlocked.
 * @param[in] job      The job.
 * @param[in] exe      The executor.
 * @post               The job is unlocked.
 * @post               The executor is locked.
 */
static void job_removeFromList(
        Job* const restrict     job)
{
    job_lock(job);
    Executor* const exe = job->exe;
    UASSERT(exe->locked);
    removeFromList(job, exe->active);
    job_unlock(job);
}

/**
 * Moves a job from the active list to the completed queue.
 *
 * @pre                Job is in `active` list.
 * @pre                Executor is locked.
 * @param[in] job      The job.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called. Job is neither in the
 *                     active list nor in the completed queue.
 * @post               Executor is locked.
 * @post               Job is in `completed` queue.
 */
static int job_move(
        Job* const job)
{
    Executor* const exe = job->exe;
    UASSERT(exe->locked);
    int             queueStatus = q_enqueue(exe->completed, job);

    if (queueStatus) {
        LOG_ADD0("Couldn't enqueue job on executor's completed-job queue");
        exe->errCode = queueStatus;
    }

    /*
     * The job is unconditionally removed from the active list because if it was
     * left there then `exe_getCompleted()` and `exe_shutdown()` would never
     * return because the active list would never become empty.
     */
    removeFromList(job, exe->active);

    int condStatus = pthread_cond_broadcast(&exe->cond);
    UASSERT(condStatus == 0);

    return queueStatus;
}

/**
 * Performs final operations on a completed job. Logs any error messages and
 * unlocks the job.
 *
 * @pre                Job is locked.
 * @pre                `job->state == COMPLETED`
 * @pre                Job is in `active` list.
 * @pre                Executor is unlocked.
 * @param[in] job      The completed job.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @post               Executor is unlocked.
 * @post               Job is in `completed` queue.
 * @post               `job->state == COMPLETED`.
 * @post               Job is unlocked.
 */
static int job_completed(
        Job* const job)
{
    UASSERT(job->locked);
    Executor* const exe = job->exe;

    UASSERT(job->state == JOB_COMPLETED);
    job_unlock(job);

    exe_lock(exe);
    int status = job_move(job);
    exe_unlock(exe);

    return status;
}

/**
 * Performs cleanup operations for a job whose thread has been canceled.
 *
 * @pre            Job is unlocked.
 * @pre            `job->state == COMPLETED`.
 * @pre            Job is in `active` list.
 * @pre            Executor is unlocked.
 * @param[in] arg  Pointer to the job.
 * @post           Executor is unlocked.
 * @post           Job is in `completed` queue.
 * @post           `job->state == COMPLETED`.
 * @post           Job is unlocked.
 */
static void job_canceled(
        void* const arg)
{
    Job* job = (Job*)arg;

    job_lock(job);
    UASSERT(job->state == JOB_COMPLETED);
    job->wasStopped = true;

    int status = job_completed(job); // unlocks job

    log_log(status ? LOG_ERR : LOG_INFO);
    log_free(); // end of thread
}

/**
 * Executes a pending job. Called by `pthread_create()`.
 *
 * @pre              `job` is unlocked.
 * @param[in] arg    Pointer to the job to execute.
 * @retval    NULL   Always.
 * @post             `job` is unlocked.
 */
static void* job_start(
        void* const arg)
{
    Job* const job = (Job*)arg;
    int        cancelState;
    int        status;

    job_lock(job);

    if (job->state == JOB_PENDING) {
        pthread_cleanup_push(job_canceled, job);

        job->state = JOB_EXECUTING;
        job_unlock(job); // So `job_stop()` can work
        (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelState);
        job->result = job->start(job->arg);
        (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancelState);

        job_lock(job);
        job->state = JOB_COMPLETED;
        job->status = 0;

        pthread_cleanup_pop(0); // use `job_completed()` instead
        status = job_completed(job); // unlocks job
    }
    else {
        job_unlock(job);
    }

    log_log(status ? LOG_ERR : LOG_INFO);
    log_free(); // end of thread

    return NULL;
}

/**
 * Stops a job. If the job is pending, then it never executes and is moved to
 * the completed queue. If the job is executing, then it is stopped by calling
 * the `stop` function given to `job_new()` if that argument was non-NULL;
 * otherwise, the thread on which the job is executing is canceled.
 *
 * @pre                The executor is locked.
 * @param[in] job      The job to be stopped.
 * @return             The number of SIGINT-s that were sent.
 * @post               The executor is locked.
 */
static void job_stop(
        Job* const job)
{
    UASSERT(job->exe->locked);

    udebug("job_stop(): Entered");
    job_lock(job);

    /*
     * This function is called by `exe_shutdown()` and the job might have
     * completed on its own.
     */
    if (job->state == JOB_PENDING) {
        udebug("job_stop(): Job was PENDING");
        job->state = JOB_COMPLETED;
        job->wasStopped = true;
        job->status = 0;
        job_unlock(job);
        if (job_move(job))
            job_free(job); // prevents memory leak
    }
    else if (job->state == JOB_EXECUTING) {
        if (job->stop) {
            job->state = JOB_COMPLETED;
            job->wasStopped = true;
            udebug("job_stop(): Calling job's stop() function");
            job->stop(job->arg);
        }
        else {
            udebug("job_stop(): Canceling job's thread");
            job->state = JOB_COMPLETED;
            job->wasStopped = true;
            int status = pthread_cancel(job->thread);
            UASSERT(status == 0);
        }
        job_unlock(job);
    }
    else {
        job_unlock(job);
    }

    udebug("job_stop(): Returning");
}

/**
 * Initializes an executor.
 *
 * @param[in] exe     The executor to be initialized.
 * @retval    0       Success.
 * @retval    ENOMEM  Out-of-memory. `log_add()` called.
 * @retval    EAGAIN  The system lacked the necessary resources (other than
 *                    memory). `log_add()` called.
 */
static int exe_init(
        Executor* const exe)
{
    int status = mutex_init(&exe->mutex);

    if (status == 0) {
        status = pthread_cond_init(&exe->cond, NULL);
        UASSERT(status != EBUSY);
        if (status) {
            LOG_ERRNUM0(status,
                    "Couldn't initialize executor condition variable");
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
                    exe->locked = false;
                    exe->state = EXE_ACTIVE;
                    exe->errCode = 0;
                } // `completed` allocated

                if (status)
                    dll_free(active);
            } // `active` allocated

            if (status)
                (void)pthread_cond_destroy(&exe->cond);
        } // `exe->cond` initialized

        if (status)
            (void)pthread_mutex_destroy(&exe->mutex);
    } // `exe->mutex` initialized

    return status;
}

/**
 * Submits a job for asynchronous execution.
 *
 * @param[in] exe      The executor.
 * @param[in] job      The job.
 * @retval    0        Success.
 * @retval    EAGAIN   The system lacked the necessary resources to create
 *                     another thread, or the system-imposed limit on the total
 *                     number of threads in a process {PTHREAD_THREADS_MAX}
 *                     would be exceeded.
 * @retval    EINVAL   Job executor is shut down. `log_add()` called.
 * @retval    ENOMEM   Out of memory. `log_add()` called.
 */
static int exe_submitJob(
        Executor* const restrict exe,
        Job* const restrict      job)
{
    int status = job_addToList(job, exe);

    if (status) {
        LOG_ADD0("Couldn't add job to list of active-jobs");
    }
    else {
        status = pthread_create(&job->thread, NULL, job_start, job);
        if (status) {
            LOG_ERRNUM0(status, "Couldn't create new thread");
            job_removeFromList(job);
        }
        else {
            (void)pthread_detach(job->thread);
        }
    }

    return status;
}

/**
 * Shuts down an executor. Calls the stop function or cancels the thread of all
 * active jobs. Blocks until all jobs have completed. Idempotent.
 *
 * @pre                The executor is locked.
 * @pre                `exe->state == SHUTTING_DOWN`
 * @param[in] exe      The executor.
 * @retval    0        Success.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @post               The executor is locked.
 */
static int shutdown(
        Executor* const restrict exe)
{
    UASSERT(exe->locked);
    UASSERT(exe->state == EXE_SHUTTING_DOWN);
    int status;

    DllIter* iter = dll_iter(exe->active);

    if (iter == NULL) {
        status = ENOMEM;
    }
    else {
        while (dll_hasNext(iter)) {
            Job* const job = (Job*)dll_next(iter);
            job_stop(job);
        }
        dll_freeIter(iter);

        while (dll_size(exe->active) > 0) {
            status = pthread_cond_wait(&exe->cond, &exe->mutex);
            UASSERT(status == 0);
        }

        status = exe->errCode;
        if (status)
            LOG_ERRNUM0(status, "The executor encountered an error");
    }

    return status;
}

/**
 * Clears the queue of completed jobs in an executor. The executor must be shut
 * down. After this call, jobs may again be submitted to the executor.
 *
 * @pre                The executor is locked.
 * @pre                The executor is shut down.
 * @param[in] exe      The executor to be cleared.
 * @retval    0        Success.
 * @retval    EINVAL   The executor isn't shut down. `log_add()` called.
 * @post               The executor is locked.
 */
static int clear(
        Executor* const restrict exe)
{
    UASSERT(exe->locked);
    UASSERT(exe->state == EXE_SHUTDOWN);
    int status;

    for (Job* job = (Job*)q_dequeue(exe->completed); job;
            job = (Job*)q_dequeue(exe->completed)) {
        job_free(job);
    }
    exe->errCode = 0;
    status = 0;

    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Indicates whether or not a job completed because it was externally stopped.
 *
 * @param[in] job     The job.
 * @retval    `true`  if and only if the job completed because it was externally
 *                    stopped.
 */
bool job_wasStopped(
        Job* const job)
{
    job_lock(job);
    bool wasStopped = job->wasStopped;
    job_unlock(job);
    return wasStopped;
}

/**
 * Returns the status of a job from it's executor's perspective.
 *
 * @param[in] job      The job to have its status returned.
 * @retval    0        The job was executed successfully.
 * @retval    EAGAIN   System lacked resources (other than memory). `log_add()`
 *                     called.
 * @retval    ENOMEM   Out-of-memory.
 */
int job_status(
        Job* const job)
{
    job_lock(job);
    int status = job->status;
    job_unlock(job);
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
    job_lock(job);
    void* result = job->result;
    job_unlock(job);
    return result;
}

/**
 * Frees the resources of a job. Because `exe_clear()` will free all jobs not
 * returned by `exe_getCompleted()`, The caller should only call this function
 * for jobs returned by `exe_getCompleted()`
 *
 * @param[in] job     The job to be freed or NULL.
 */
void job_free(
        Job* const job)
{
    if (job != NULL) {
        int status = pthread_mutex_destroy(&job->mutex);
        UASSERT(status == 0);
        free(job);
    }
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
 * @param[in]  exe     The executor.
 * @param[in]  start   Starting function for `pthread_create()`.
 * @param[in]  arg     Argument for `pthread_create()` and `stop()`.
 * @param[in]  stop    Stopping function or NULL.
 * @param[out] job     The job or NULL.
 * @retval     0       Success. `*job` is set.
 * @retval     EINVAL  The executor is shut down. `log_add()` called.
 * @retval     ENOMEM  Out of memory. `log_add()` called.
 */
int exe_submit(
        Executor* const restrict exe,
        void*            (*const start)(void*),
        void* restrict           arg,
        void             (*const stop)(void*),
        Job** const restrict     job)
{
    udebug("exe_submit(): Entered");
    int  status;

    exe_lock(exe);

    if (exe->state != EXE_ACTIVE) {
        LOG_START0("Executor isn't ready");
        status = EINVAL;
    }
    else {
        Job* jb;

        status = job_new(&jb, start, arg, stop);
        if (status == 0) {
            status = exe_submitJob(exe, jb);
            if (status) {
                job_free(jb);
            }
            else {
                if (job)
                    *job = jb;
                status = pthread_cond_broadcast(&exe->cond);
                UASSERT(status == 0);
            }
        }
    }

    exe_unlock(exe);

    udebug("exe_submit(): Returning %d", status);
    return status;
}

/**
 * Returns the number of jobs submitted to an executor but not yet removed by
 * `exe_getCompleted()` or `exe_clear()`.
 *
 * @param[in] exe  The executor.
 * @return         The number of jobs submitted to an executor but not yet
 *                 removed
 */
size_t exe_count(
        Executor* const exe)
{
    ssize_t count;
    exe_lock(exe);
    count = dll_size(exe->active) + q_size(exe->completed);
    exe_unlock(exe);
    return count;
}

/**
 * Removes and returns the next completed job. Blocks until a completed job is
 * available or `exe_free()` is called.
 *
 * @param[in] exe      The executor.
 * @retval    NULL     `exe_free()` was called.
 * @return             The next completed job.
 */
Job* exe_getCompleted(
        Executor* const restrict exe)
{
    Job* job;

    udebug("exe_getCompleted(): Entered");
    exe_lock(exe);

    while (exe->state != EXE_BEING_FREED && q_size(exe->completed) == 0) {
        int status = pthread_cond_wait(&exe->cond, &exe->mutex);
        UASSERT(status == 0);
    }

    if (exe->state == EXE_BEING_FREED) {
        job = NULL;
    }
    else {
        job = q_dequeue(exe->completed);
    }

    exe_unlock(exe);

    udebug("exe_getCompleted(): Returning %p", job);
    return job;
}

/**
 * Shuts down an executor. Calls the stop function or cancels the thread of all
 * active jobs. Blocks until all jobs have completed. Idempotent.
 *
 * @param[in] exe      The executor.
 * @retval    0        Success.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 */
int exe_shutdown(
        Executor* const restrict exe)
{
    udebug("exe_shutdown(): Entered");
    int status;

    exe_lock(exe);

    for (;;) {
        switch (exe->state) {
            case EXE_SHUTDOWN:
                status = 0;
                break;
            case EXE_SHUTTING_DOWN:
                while (exe->state == EXE_SHUTTING_DOWN)
                    pthread_cond_wait(&exe->cond, &exe->mutex);
                continue;
            case EXE_ACTIVE:
            default:
                exe->state = EXE_SHUTTING_DOWN;
                status = shutdown(exe);
                exe->state = EXE_SHUTDOWN;
                pthread_cond_broadcast(&exe->cond);
        }
        break;
    }

    exe_unlock(exe);

    udebug("exe_shutdown(): Returning %d", status);
    return status;
}

/**
 * Clears the queue of completed jobs in an executor and frees the resources of
 * such jobs. The executor must be shut down. After this call, jobs may again be
 * submitted to the executor.
 *
 * @param[in] exe      The executor to be cleared.
 * @retval    0        Success.
 * @retval    EINVAL   The executor isn't shut down. `log_add()` called.
 */
int exe_clear(
        Executor* const restrict exe)
{
    udebug("exe_clear(): Entered");
    int status;

    exe_lock(exe);

    switch (exe->state) {
        case EXE_SHUTDOWN:
            status = clear(exe);
            exe->state = EXE_ACTIVE;
            pthread_cond_broadcast(&exe->cond);
            break;
        default:
            LOG_START0("Executor isn't shut down");
            status = EINVAL;
    }

    exe_unlock(exe);

    udebug("exe_clear(): Returning %d", status);
    return status;
}

/**
 * Frees an executor's resources. The executor must be shut down or empty. Will
 * cause a blocking `exe_getCompleted()` to return.
 *
 * @param[in] exe      The executor to be freed or NULL.
 * @retval    0        Success or `exe == NULL`.
 * @retval    EINVAL   The executor is neither shut down nor empty. `log_add()`
 *                     called.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 */
int exe_free(
        Executor* const exe)
{
    udebug("exe_free(): Entered");
    int status;

    if (exe == NULL) {
        status = 0;
    }
    else {
        exe_lock(exe);

        switch (exe->state) {
            case EXE_SHUTTING_DOWN:
                LOG_ADD0("The executor isn't shut down");
                status = EINVAL;
                break;
            case EXE_ACTIVE:
                if (q_size(exe->completed) || dll_size(exe->active)) {
                    LOG_ADD0("The executor isn't empty");
                    status = EINVAL;
                    break;
                }
            case EXE_SHUTDOWN:
            default:
                exe->state = EXE_BEING_FREED;
                status = pthread_cond_broadcast(&exe->cond);
                UASSERT(status == 0);

                q_free(exe->completed);
                dll_free(exe->active);

                status = pthread_cond_destroy(&exe->cond);
                UASSERT(status == 0);

                status = exe->errCode;
                if (status)
                    LOG_ERRNUM0(status, "The executor encountered an error");

                exe_unlock(exe);
                int tmpStatus = pthread_mutex_destroy(&exe->mutex);
                UASSERT(tmpStatus == 0);

                if (status == 0)
                    status = tmpStatus;

                free(exe);
        }
    }

    udebug("exe_free(): Returning %d", status);
    return status;
}
