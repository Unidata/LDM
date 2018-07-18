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

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Returns a new future for an asynchronous job.
 *
 * @return              New future
 * @retval    `NULL`    Out of memory. `log_add()` called.
 * @threadsafety        Safe
 */
Future*
future_new();

/**
 * Frees a future. Doesn't free the object given to `future_new()` unless
 * `future_addFree()` was called. Doesn't free the future's job.
 *
 * NB: Undefined behavior will result if any of the other future functions are
 * called after this function on the same future.
 *
 * NB: A memory-leak will occur if the job allocated a result object which was
 * not retrieved by a call to `future_getResult()`.
 *
 * @param[in] future     Future to be freed
 * @retval    0          Success
 * @retval    EINVAL     Associated job is being executed. Future wasn't freed.
 *                       `log_add()` called.
 * @threadsafety         Thread-compatible but not thread-safe
 */
int
future_free(Future* future);

typedef struct job Job;

/**
 * Sets the job associated with a future.
 */
void
future_setJob(
        Future* const future,
        Job* const    job);

/**
 * Sets the function for freeing a future's object.
 *
 * @param[in] future  Future
 * @param[in] free    Function to free the object given to `future_new()` or
 *                    NULL for no such freeing
 */
void
future_setFree(
        Future* const future,
        void        (*free)(void* obj));

/**
 * Returns the object given to `future_new()`.
 *
 * @param[in] future  Future
 * @return            Object given to `future_new()`
 */
void*
future_getObj(Future* const future);

/**
 * Executes a future's job on the current thread.
 *
 * @param[in,out] future     Future
 * @retval        0          Success
 * @retval        EINVAL     Future isn't in appropriate state
 * @threadsafety             Safe
 */
int
future_run(Future* future);

/**
 * Asynchronously cancels a future. If the associated job hasn't started, then
 * it should never start. Does nothing if the job has already completed.
 *
 * @param[in,out] future           Future to be canceled
 * @retval        0                Success
 * @retval        ENOTRECOVERABLE  State protected by mutex is not recoverable.
 *                                 `log_add()` called.
 * @return                         Error code from cancellation function given
 *                                 to `future_new()`. `log_add()` called.
 * @threadsafety                   Safe
 */
int
future_cancel(Future* future);

/**
 * Sets the result of execution.
 *
 * @param[in,out] future  Future
 * @param[in]     status  Return-value of run function
 * @param[in]     result  Result pointer or `NULL`
 */
void
future_setResult(
        Future* const restrict future,
        int                    status,
        void* const restrict   result,
        const bool             wasCanceled);

/**
 * Returns the result of a future's job. Blocks until the job has completed --
 * either normally or because `future_cancel()` was called.
 *
 * NB: A memory leak will occur if the job allocated a result object and the
 * given result pointer is `NULL`.
 *
 * @param[in,out] future           Future
 * @param[out]    result           Result of job execution or `NULL`. NB:
 *                                 Potential for memory-leak if `NULL`.
 * @retval        0                Success. Task's run function returned zero.
 *                                 `*result` is set if `result != NULL`.
 * @retval        ECANCELED        Task was canceled
 * @retval        ENOTRECOVERABLE  State protected by mutex is not recoverable.
 *                                 `log_add()` called.
 * @retval        EPERM            Task's run function returned non-zero value
 * @threadsafety                   Safe
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
 *                           and `result` is NULL.
 * @retval        0          Success. `*result` is set if `result != NULL`.
 * @retval        EDEADLK    Deadlock detected
 * @retval        ECANCELED  Task was canceled
 * @return                   Return value of `future_new()`'s `runFunc` argument
 * @threadsafety             Safe
 */
int
future_getAndFree(
        Future* const restrict future,
        void** const restrict  result);

/**
 * Returns the return-value of the job's run function. Must be called after
 * `future_getResult()`.
 *
 * @param[in] future   Task's future
 * @return             Return value of job's run-function
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
