/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: executor.h
 * @author: Steven R. Emmerson
 *
 * This file defines the API for an executor of asynchronous jobs.
 */

#ifndef EXECUTOR_H_
#define EXECUTOR_H_

#include <stdbool.h>

typedef struct job      Job;
typedef struct executor Executor;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Indicates whether or not the thread that was executing a job was canceled.
 *
 * @param[in] job     The job.
 * @retval    `true`  if and only if the job's thread was canceled.
 */
bool job_wasCanceled(
        Job* const job);

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
        Job* const job);

/**
 * Returns the result of a job.
 *
 * @param[in] job  The job to have its result returned.
 * @return         The result of the job.
 */
void* job_result(
        Job* const job);

/**
 * Frees the resources of a job.
 *
 * @param[in] job  The job to be freed.
 */
void job_free(
        Job* const job);

/**
 * Returns a new job executor.
 *
 * @retval NULL  Insufficient system resources or logic error. `log_add()`
 *               called.
 * @return       Pointer to new job executor.
 */
Executor* exe_new(void);

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
        Job** const restrict     job);

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
        Job** const restrict     job);

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
        Executor* const restrict exe);

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
        Executor* const restrict exe);

/**
 * Frees an executor's resources. Shuts down the executor and clears it if
 * necessary.
 *
 * @param[in] exe  The excutor to be freed or NULL.
 */
void exe_free(
        Executor* const exe);

#ifdef __cplusplus
    }
#endif

#endif /* EXECUTOR_H_ */
