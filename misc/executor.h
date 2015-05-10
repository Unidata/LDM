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
#include <sys/types.h>

typedef struct job      Job;
typedef struct executor Executor;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Indicates whether or not a job completed because it was externally stopped.
 *
 * @param[in] job     The job.
 * @retval    `true`  if and only if the job completed because it was externally
 *                    stopped.
 */
bool job_wasStopped(
        Job* const job);

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
 * Frees the resources of a job. Because `exe_clear()` will free all jobs not
 * returned by `exe_getCompleted()`, The caller should only call this function
 * for jobs returned by `exe_getCompleted()`
 *
 * @param[in] job     The job to be freed or NULL.
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
        Job** const restrict     job);

/**
 * Returns the number of jobs submitted to an executor but not yet removed by
 * `exe_getCompleted()` or `exe_clear()`.
 *
 * @param[in] exe  The executor.
 * @return         The number of jobs submitted to an executor but not yet
 *                 removed
 */
size_t exe_count(
        Executor* const exe);

/**
 * Removes and returns the next completed job. Blocks until a completed job is
 * available or `exe_free()` is called.
 *
 * @param[in] exe      The executor.
 * @retval    NULL     `exe_free()` was called.
 * @return             The next completed job.
 */
Job* exe_getCompleted(
        Executor* const restrict exe);

/**
 * Shuts down an executor. Calls the stop function or cancels the thread of all
 * active jobs. Blocks until all jobs have completed. Idempotent.
 *
 * @param[in] exe      The executor.
 * @retval    0        Success.
 * @retval    ENOMEM   Out-of-memory. `log_add()` called.
 */
int exe_shutdown(
        Executor* const restrict exe);

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
        Executor* const restrict exe);

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
        Executor* const exe);

#ifdef __cplusplus
    }
#endif

#endif /* EXECUTOR_H_ */
