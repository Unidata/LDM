/**
 * This file defines the future of an asynchronous job.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Future.h
 *  Created on: May 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include <pthread.h>
#include <stdbool.h>

#ifndef MCAST_LIB_LDM7_FUTURE_H_
#define MCAST_LIB_LDM7_FUTURE_H_

typedef struct future Future;
typedef struct job    Job;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Returns a new future for an asynchronous job.
 *
 * @param[in] job       Future's job. Should eventually be passed to
 *                      `future_free()`.
 * @return              New future
 * @retval    `NULL`    Out of memory. `log_add()` called.
 * @threadsafety        Safe
 * @see `future_free()`
 */
Future*
future_new(Job* const job);

/**
 * Frees a future. Doesn't free the object given to `future_new()`. Cancels the
 * future's job if necessary. Must not be called if `future_getResult()` is
 * executing on another thread. If the future's job hasn't started, then it
 * should never start. Blocks until the future's job completes -- either
 * normally or by being canceled. Frees the future's job.
 *
 * NB: Undefined behavior will result if the future object is dereferenced after
 * this function returns.
 *
 * NB: A memory-leak will occur if the future's job allocated a result object
 * which was not retrieved by a call to `future_getResult()`.
 *
 * @param[in] future           Future to be freed
 * @retval    0                Success
 * @retval    ENOTRECOVERABLE  State protected by mutex is not recoverable.
 *                             `log_add()` called.
 * @retval    ENOTSUP          Job's halt function returned non-zero value.
 *                             `'log_add()` called.
 * @threadsafety               Thread-compatible but not thread-safe
 */
int
future_free(Future* future);

/**
 * Synchronously cancels a future. If the associated job hasn't started, then
 * it should never start. Does nothing if the job has already completed.
 *
 * Idempotent.
 *
 * @param[in,out] future           Future to be canceled
 * @retval        0                Success
 * @retval        ENOTRECOVERABLE  State protected by mutex is not recoverable.
 *                                 `log_add()` called.
 * @retval        ENOTSUP          Job's halt function returned non-zero value.
 *                                 `'log_add()` called.
 * @threadsafety                   Safe
 * @see `future_getResult()`
 */
int
future_cancel(Future* future);

/**
 * Sets the result of execution.
 *
 * @param[in,out] future       Future
 * @param[in]     status       Return-value of run function of future's job
 * @param[in]     result       Result pointer or `NULL`
 * @param[in]     wasCanceled  Was the future's job canceled?
 */
void
future_setResult(
        Future* const restrict future,
        int                    status,
        void* const restrict   result,
        const bool             wasCanceled);

/**
 * Returns the result of a future's job. Blocks until the job has completed --
 * either normally or because it was canceled.
 *
 * NB: A memory leak will occur if the future's job allocated a result object
 * and the `result` argument is `NULL`.
 *
 * @param[in,out] future           Future
 * @param[out]    result           Result of job execution or `NULL`. NB:
 *                                 Potential for memory-leak if `NULL` and job's
 *                                 run function allocates memory for a result.
 * @retval        0                Success. Job's run function returned zero.
 *                                 `*result` is set if `result != NULL`.
 * @retval        ECANCELED        Job was canceled. `*result` is not set.
 * @retval        ENOTRECOVERABLE  State protected by mutex is not recoverable.
 *                                 `log_add()` called. `*result` is not set.
 * @retval        EPERM            Job's run function returned non-zero value.
 *                                 `*result` is not set.
 * @threadsafety                   Safe
 * @see `future_cancel()`
 */
int
future_getResult(
        Future* const restrict future,
        void** const restrict  result);

/**
 * Returns the result of a future's job and frees the future. Blocks until the
 * job has completed. The future should not be dereferenced upon return.
 *
 * @param[in,out] future     Future
 * @param[out]    result     Result of job execution or `NULL`. A memory-leak
 *                           will occur if the job allocated a result object
 *                           and `result` is `NULL`.
 * @retval        0          Success. `*result` is set if `result != NULL`.
 * @retval        ECANCELED  Job was canceled
 * @retval        ENOTSUP    Job's halt function returned non-zero value.
 *                           `'log_add()` called.
 * @retval        EPERM      Job's run function returned non-zero value.
 *                           `*result` is not set.
 * @threadsafety             Safe
 * @see `future_cancel()`
 * @see `future_getResult()`
 * @see `future_free()`
 */
int
future_getAndFree(
        Future* const restrict future,
        void** const restrict  result);

/**
 * Returns the return-value of the job's run function. Should be called after
 * `future_getResult()`.
 *
 * @param[in] future   Job's future
 * @return             Return value of job's run-function
 * @see `future_getResult()`
 */
int
future_getRunStatus(Future* future);

/**
 * Indicates if two futures are considered equal.
 *
 * @param[in] future1  Future
 * @param[in] future2  Future
 * @retval    `true`   Futures are considered equal
 * @retval    `false`  Futures are not considered equal
 */
bool
future_areEqual(
        const Future* future1,
        const Future* future2);

#ifdef __cplusplus
    }
#endif

#endif /* MCAST_LIB_LDM7_FUTURE_H_ */
