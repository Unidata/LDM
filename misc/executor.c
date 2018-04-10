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
    DllElt*         elt;           ///< associated element in executor's job-list
    pthread_t       thread;        ///< executing thread
    enum {
        JOB_PENDING,
        JOB_EXECUTING,
        JOB_CANCELLED,
        JOB_STOPPING,
        JOB_COMPLETED
    }               state;         ///< state of the job
    int             status;        ///< status of execution
    bool            wasStopped;    ///< if a job was externally stopped
    pthread_mutex_t mutex;         ///< for inter-thread visibility of members
};

struct executor {
    Dll*            jobs;       ///< list of jobs
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
};

/**
 * Initializes a mutex. The mutex will have protocol `PTHREAD_PRIO_INHERIT` and
 * type `PTHREAD_MUTEX_ERRORCHECK`. This module depends on these attributes.
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
        log_errno_q(status, "Couldn't initialize mutex attributes");
    }
    else {
        status = pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);
        log_assert(status != EPERM);
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_ERRORCHECK);

        status = pthread_mutex_init(mutex, &mutexAttr);
        log_assert(status != EPERM);
        log_assert(status != EBUSY);
        if (status)
            log_errno_q(status, "Couldn't initialize mutex");

        (void)pthread_mutexattr_destroy(&mutexAttr);
    }

    return status;
}

/**
 * Verifies that a mutex is locked. Aborts if it isn't. The mutex must not be
 * recursive.
 *
 * @param[in] mutex  The non-recursive mutex.
 */
static void verifyLocked(
        pthread_mutex_t* const mutex)
{
    if (pthread_mutex_trylock(mutex) == 0)
        log_assert(false);
}

/**
 * Verifies that a mutex is unlocked. Aborts if it isn't. The mutex must not be
 * recursive.
 *
 * @param[in] mutex  The non-recursive mutex.
 */
static void verifyUnlocked(
        pthread_mutex_t* const mutex)
{
    if (pthread_mutex_trylock(mutex) == 0) {
        pthread_mutex_unlock(mutex);
        return;
    }
    log_assert(false);
}

/**
 * Locks an executor's mutex.
 *
 * @pre                The executor is unlocked on this thread.
 * @param[in] exe      The executor to be locked.
 * @post               The executor is locked on this thread.
 */
static void exe_lock(
        Executor* const exe)
{
    log_debug_1("exe_lock(): Entered");
    int status = pthread_mutex_lock(&exe->mutex);
    log_assert(status == 0);
}

/**
 * Unlocks an executor's mutex.
 *
 * @pre                The executor is locked on this thread.
 * @param[in] exe      The executor to be unlocked.
 * @post               The executor is unlocked on this thread.
 */
static void exe_unlock(
        Executor* const exe)
{
    log_debug_1("exe_unlock(): Entered");
    int status = pthread_mutex_unlock(&exe->mutex);
    log_assert(status == 0);
}

/**
 * Enqueues a job on the completed-job queue of its executor. Signals the
 * executor's condition variable if successful.
 *
 * @pre               The job's executor is locked.
 * @param[in] job     The job.
 * @retval    0       Success
 * @retval    ENOMEM  Out of memory. 'log_add()` called.
 * @post              The job's executor's condition variable was signaled.
 * @post              The job's executor is locked.
 */
static int exe_enqueueCompleted(
        Job* const job)
{
    Executor* const exe = job->exe;

    verifyLocked(&exe->mutex);

    int status = q_enqueue(exe->completed, job);
    if (status) {
        log_add("Couldn't enqueue job on executor's completed-job queue");
        exe->errCode = status;
    }

    int condStatus = pthread_cond_broadcast(&exe->cond);
    log_assert(condStatus == 0);

    return status;
}

/**
 * Adds a job to the completed-job queue of its executor. Signals the
 * executor's condition variable.
 *
 * @pre               The job's executor is unlocked.
 * @param[in] job     The job.
 * @retval    0       Success.
 * @retval    ENOMEM  Out of memory. 'log_add()` called.
 * @post              The associated executor's condition variable was signaled.
 * @pre               The job's executor is unlocked.
 */
static int exe_addToCompleted(
        Job* const job)
{
    Executor* const exe = job->exe;

    exe_lock(exe);
    int status = exe_enqueueCompleted(job);
    exe_unlock(exe);

    return status;
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
    log_assert(status == 0);
}

/**
 * Unlocks a job's mutex.
 *
 * @param[in] job      The job to be unlocked.
 */
static void job_unlock(
        Job* const job)
{
    int status = pthread_mutex_unlock(&job->mutex);
    log_assert(status == 0);
}

/**
 * Tests the state of a job.
 *
 * @pre             The job is unlocked.
 * @param[in job    The job.
 * @param[in state  The state against which to test.
 * @retval   true   The job was in the given state.
 * @retval   false  The job wasn't in the given state.
 * @post            The job is unlocked.
 */
static bool job_testState(
        Job* const job,
        const int  state)
{
    job_lock(job);
    bool wasEqual = job->state == state;
    job_unlock(job);
    return wasEqual;
}

/**
 * Tests the state of a job and sets it if appropriate.
 *
 * @pre                    The job is unlocked.
 * @param[in] job          The job.
 * @param[in] currState    The state to test the current state of the job
 *                         against.
 * @param[in] nextState    The state to set the job to if the current state of
 *                         the job is `currState`.
 * @retval    true         The state of the job was `currState` and has been set
 *                         to `nextState`.
 * @retval    false        The state of the job wasn't `currState` and is
 *                         unchanged.
 * @post                   The job is unlocked.
 */
static bool job_compareAndSetState(
        Job* const job,
        const int  currState,
        const int  nextState)
{
    bool wasEqual = false;
    job_lock(job);
    if (job->state == currState) {
        job->state = nextState;
        wasEqual = true;
    }
    job_unlock(job);
    return wasEqual;
}

/**
 * Initializes a job. The job does not start executing.
 *
 * @param[out] job     The job.
 * @param[in]  start   Starting function for `pthread_create()`.
 * @param[in]  arg     Argument for `pthread_create()` and `stop()`.
 * @param[in]  stop    Stopping function or NULL.
 * @param[in]  exe     The executor for the job.
 * @retval     0       Success.
 * @retval     EAGAIN  Insufficient system resources other than memory.
 *                     `log_add()` called.
 */
static int job_init(
        Job* const restrict       job,
        void*             (*const start)(void*),
        void* restrict            arg,
        void              (*const stop)(void*),
        Executor* const restrict  exe)
{
    log_assert(job != NULL && start != NULL);

    int status = mutex_init(&job->mutex);

    if (status) {
        log_add("Couldn't initialize job's mutex");
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
        job->exe = exe;
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
 * @param[in]  exe     The executor for the job.
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
        void               (*const stop)(void*),
        Executor* const restrict   exe)
{
    Job* const jb = log_malloc(sizeof(Job), "job");
    int        status;

    if (jb == NULL) {
        status = ENOMEM;
    }
    else {
        status = job_init(jb, start, arg, stop, exe);

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
 * Finalizes a job that has completed. A job completes when either its
 * start-function returns or its executing thread is cancelled. In either case,
 * this function is executed on the job's executing thread.
 *
 * @pre                The job's executor is unlocked.
 * @pre                The job is locked.
 * @param[in] job      The completed job.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @post               The job is in its executor's completed-job queue and the
 * @post               The condition variable of the job's executor was
 *                     signaled.
 * @post               `job->state == COMPLETED`.
 * @post               The job is unlocked.
 */
static int job_completed(
        Job* const job)
{
    int status;

    job->state = JOB_COMPLETED;
    job_unlock(job);
    status = exe_addToCompleted(job);

    return status;
}

/**
 * Performs cleanup operations for a job whose executing thread has been
 * canceled.
 *
 * @pre            Job is unlocked.
 * @param[in] arg  Pointer to the job.
 * @post           Job is in associated executor's `completed` queue if
 *                 `job->state == JOB_STOPPING`.
 * @post           `job->state == JOB_COMPLETED` if it was `JOB_STOPPING`.
 * @post           Job is unlocked.
 */
static void job_canceled(
        void* const arg)
{
    Job* job = (Job*)arg;
    int  status;

    job_lock(job);
    if (job->state == JOB_CANCELLED) {
        job->wasStopped = true;
        status = job_completed(job); // unlocks job
    }
    else {
        job_unlock(job);
        status = 0;
    }

    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);
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
        job->status = 0;

        pthread_cleanup_pop(0); // use `job_completed()` instead
        status = job_completed(job); // unlocks job
    }
    else {
        job_unlock(job);
        status = 0;
    }

    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);
    log_free(); // end of thread

    return NULL;
}

/**
 * Stops a job. If the job is pending, then it never executes and is added to
 * the completed-job queue. If the job is executing, then it is stopped by
 * calling the `stop` function given to `job_new()` if that argument was
 * non-NULL; otherwise, the thread on which the job is executing is canceled.
 *
 * @pre                The job is unlocked.
 * @pre                The job's executor is locked.
 * @param[in] job      The job to be stopped.
 * @retval    0        Success.
 * @retval    ENOMEM   Out of memory. 'log_add()` called.
 * @post               The job's executor is locked.
 * @post               The job is unlocked.
 */
static int job_stop(
        Job* const job)
{
    int status;

    log_debug_1("job_stop(): Entered");
    job_lock(job);

    /*
     * This function is called by `exe_shutdown()` and the job might have
     * completed on its own.
     */
    if (job->state == JOB_PENDING) {
        log_debug_1("job_stop(): Job was PENDING");
        job->state = JOB_COMPLETED;
        job->wasStopped = true;
        job->status = 0;
        job_unlock(job);
        status = exe_enqueueCompleted(job);
        if (status)
            job_free(job); // prevents memory leak
    }
    else if (job->state == JOB_EXECUTING) {
        job->wasStopped = true;
        if (job->stop) {
            job->state = JOB_STOPPING;
            log_debug_1("job_stop(): Calling job's stop() function");
            job_unlock(job); // precondition for `job->stop()`
            job->stop(job->arg);
        }
        else {
            job->state = JOB_CANCELLED;
            job_unlock(job); // precondition for `job_cancelled()`
            log_debug_1("job_stop(): Canceling job's thread");
            int status = pthread_cancel(job->thread);
            log_assert(status == 0);
        }
        status = 0;
    }
    else {
        job_unlock(job);
        status = 0;
    }

    log_debug_1("job_stop(): Returning");
    return status;
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
        log_assert(status != EBUSY);
        if (status) {
            log_errno_q(status,
                    "Couldn't initialize executor condition variable");
        }
        else {
            Dll* jobs = dll_new();
            if (jobs == NULL) {
                status = ENOMEM;
            }
            else {
                Queue* completed = q_new();
                if (completed == NULL) {
                    log_add("Couldn't allocate completed-job queue");
                    status = ENOMEM;
                }
                else {
                    exe->jobs = jobs;
                    exe->completed = completed;
                    exe->state = EXE_ACTIVE;
                    exe->errCode = 0;
                } // `completed` allocated

                if (status)
                    dll_free(jobs);
            } // `jobs` allocated

            if (status)
                (void)pthread_cond_destroy(&exe->cond);
        } // `exe->cond` initialized

        if (status)
            (void)pthread_mutex_destroy(&exe->mutex);
    } // `exe->mutex` initialized

    return status;
}

/**
 * Adds a job to an executor's executing job-list.
 *
 * @pre                The executor is locked.
 * @pre                The job is unlocked.
 * @pre                `job->state == PENDING`
 * @param[in] exe      The executor.
 * @param[in] job      The job.
 * @retval    0        Success.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @post               The job is in the executor's job list.
 * @post               The job is unlocked.
 * @post               The executor is locked.
 */
static int exe_addToList(
        Executor* const restrict exe,
        Job* const restrict      job)
{
    int status;

    verifyLocked(&exe->mutex);

    job_lock(job);
    log_assert(job->state == JOB_PENDING);

    DllElt* const elt = dll_add(exe->jobs, job);

    if (elt == NULL) {
        status = ENOMEM;
    }
    else {
        job->elt = elt;
        status = 0;
    }

    job_unlock(job);

    return status;
}

/**
 * Removes a job from an executor's executing job-list.
 *
 * @pre                The executor is locked.
 * @param[in] job      The job.
 * @post               The executor is locked.
 */
static void exe_removeFromList(
        Executor* const restrict exe,
        Job* const restrict      job)
{
    verifyLocked(&exe->mutex);
    (void)dll_remove(exe->jobs, job->elt);
    job_lock(job);
    job->elt = NULL;
    job_unlock(job);
}

/**
 * Submits a job for asynchronous execution.
 *
 * @pre                The executor is locked.
 * @param[in] exe      The executor.
 * @param[in] job      The job.
 * @retval    0        Success.
 * @retval    EAGAIN   The system lacked the necessary resources to create
 *                     another thread, or the system-imposed limit on the total
 *                     number of threads in a process {PTHREAD_THREADS_MAX}
 *                     would be exceeded.
 * @retval    EINVAL   Job executor is shut down. `log_add()` called.
 * @retval    ENOMEM   Out of memory. `log_add()` called.
 * @post               The executor is locked.
 */
static int exe_submitJob(
        Executor* const restrict exe,
        Job* const restrict      job)
{
    verifyLocked(&exe->mutex);

    int status = exe_addToList(exe, job);
    if (status) {
        log_add("Couldn't add job to list");
    }
    else {
        status = pthread_create(&job->thread, NULL, job_start, job);
        if (status) {
            log_errno_q(status, "Couldn't create new thread");
            exe_removeFromList(exe, job);
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
 * @pre                `exe->state == EXE_SHUTTING_DOWN`
 * @param[in] exe      The executor.
 * @retval    0        Success.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @post               The executor is locked.
 */
static int shutdown(
        Executor* const restrict exe)
{
    verifyLocked(&exe->mutex);
    log_assert(exe->state == EXE_SHUTTING_DOWN);
    int      status = 0;
    DllIter* iter = dll_iter(exe->jobs);

    while (dll_hasNext(iter)) {
        Job* const job = dll_next(iter);
        int tmpStatus = job_stop(job);
        if (status == 0 && tmpStatus)
            status = tmpStatus;
    }
    dll_freeIter(iter);

    while (q_size(exe->completed) < dll_size(exe->jobs)) {
        int tmpStatus = pthread_cond_wait(&exe->cond, &exe->mutex);
        log_assert(tmpStatus == 0);
    }

    if (status == 0 && exe->errCode) {
        status = exe->errCode;
        log_errno_q(status, "The executor encountered an error");
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
    verifyLocked(&exe->mutex);
    log_assert(exe->state == EXE_SHUTDOWN);
    int status;

    for (Job* job = (Job*)q_dequeue(exe->completed); job;
            job = (Job*)q_dequeue(exe->completed)) {
        exe_removeFromList(job->exe, job);
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
 * @pre               The job is unlocked.
 * @param[in] job     The job to be freed or NULL.
 */
void job_free(
        Job* const job)
{
    if (job != NULL) {
        // Because pthread_mutex_destroy() may return 0 for a locked mutex
        verifyUnlocked(&job->mutex);
        int status = pthread_mutex_destroy(&job->mutex);
        log_assert(status == 0);
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
    log_debug_1("exe_new(): Entered");
    Executor* exe = log_malloc(sizeof(Executor), "job executor");

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
 * @retval     0       Success. `*job` is set to non-NULL value.
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
    log_debug_1("exe_submit(): Entered");
    int  status;

    exe_lock(exe);

    if (exe->state != EXE_ACTIVE) {
        log_add("Executor isn't ready");
        status = EINVAL;
    }
    else {
        Job* jb;

        status = job_new(&jb, start, arg, stop, exe);
        if (status == 0) {
            status = exe_submitJob(exe, jb);
            if (status) {
                job_free(jb);
            }
            else {
                if (job)
                    *job = jb;
                status = pthread_cond_broadcast(&exe->cond);
                log_assert(status == 0);
            }
        }
    }

    exe_unlock(exe);

    log_debug_1("exe_submit(): Returning %d", status);
    return status;
}

/**
 * Returns the number of jobs submitted to an executor but not yet removed by
 * `exe_getCompleted()` or `exe_clear()`.
 *
 * @param[in] exe  The executor.
 * @return         The number of jobs submitted to an executor but not yet
 *                 removed.
 */
size_t exe_count(
        Executor* const exe)
{
    log_debug_1("exe_count(): Entered");
    size_t count;
    exe_lock(exe);
    count = dll_size(exe->jobs);
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

    log_debug_1("exe_getCompleted(): Entered");
    exe_lock(exe);

    while (exe->state != EXE_BEING_FREED && q_size(exe->completed) == 0) {
        int status = pthread_cond_wait(&exe->cond, &exe->mutex);
        log_assert(status == 0);
    }

    if (exe->state == EXE_BEING_FREED) {
        job = NULL;
    }
    else {
        job = q_dequeue(exe->completed);
        exe_removeFromList(job->exe, job);
    }

    exe_unlock(exe);

    log_debug_1("exe_getCompleted(): Returning %p", job);
    return job;
}

/**
 * Shuts down an executor. Calls the stop function or cancels the thread of all
 * active jobs. Blocks until all jobs have completed. Idempotent.
 *
 * @param[in] exe      The executor.
 * @retval    0        Success.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 * @retval    EINVAL   Executor is in the wrong state. `log_add()` called.
 */
int exe_shutdown(
        Executor* const restrict exe)
{
    log_debug_1("exe_shutdown(): Entered");
    int status;

    log_debug_1("exe_shutdown(): Calling exe_lock()");
    exe_lock(exe);

    for (;;) {
        switch (exe->state) {
            case EXE_ACTIVE:
                exe->state = EXE_SHUTTING_DOWN;
                log_debug_1("exe_shutdown(): Calling shutdown()");
                status = shutdown(exe);
                exe->state = EXE_SHUTDOWN;
                pthread_cond_broadcast(&exe->cond);
                continue;
            case EXE_SHUTTING_DOWN:
                log_debug_1("exe_shutdown(): Waiting on condition variable");
                while (exe->state == EXE_SHUTTING_DOWN)
                    pthread_cond_wait(&exe->cond, &exe->mutex);
                continue;
            case EXE_SHUTDOWN:
                status = 0;
                break;
            default: // EXE_BEING_FREED
                log_add("Executor is being freed");
                status = EINVAL;
                break;
        }
        break;
    }

    exe_unlock(exe);

    log_debug_1("exe_shutdown(): Returning %d", status);
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
    log_debug_1("exe_clear(): Entered");
    int status;

    exe_lock(exe);

    switch (exe->state) {
        case EXE_SHUTDOWN:
            status = clear(exe);
            exe->state = EXE_ACTIVE;
            pthread_cond_broadcast(&exe->cond);
            break;
        default:
            log_add("Executor isn't shut down");
            status = EINVAL;
    }

    exe_unlock(exe);

    log_debug_1("exe_clear(): Returning %d", status);
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
    log_debug_1("exe_free(): Entered");
    int status;

    if (exe == NULL) {
        status = 0;
    }
    else {
        exe_lock(exe);

        switch (exe->state) {
            case EXE_SHUTTING_DOWN:
                exe_unlock(exe);
                log_add("The executor isn't shut down");
                status = EINVAL;
                break;
            case EXE_ACTIVE:
                if (dll_size(exe->jobs)) {
                    log_add("The executor isn't empty");
                    status = EINVAL;
                    exe_unlock(exe);
                    break;
                }
            case EXE_SHUTDOWN:
            default:
                exe->state = EXE_BEING_FREED;
                status = pthread_cond_broadcast(&exe->cond);
                log_assert(status == 0);

                q_free(exe->completed);
                dll_free(exe->jobs);

                status = pthread_cond_destroy(&exe->cond);
                log_assert(status == 0);

                status = exe->errCode;
                if (status)
                    log_errno_q(status, "The executor encountered an error");

                exe_unlock(exe);
                int tmpStatus = pthread_mutex_destroy(&exe->mutex);
                log_assert(tmpStatus == 0);

                if (status == 0)
                    status = tmpStatus;

                free(exe);
        }
    }

    log_debug_1("exe_free(): Returning %d", status);
    return status;
}
