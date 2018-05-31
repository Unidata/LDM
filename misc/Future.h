/**
 * This file defines the future of an asynchronous task.
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
 * Returns a new future for an asynchronous task.
 *
 * @param[in] obj       Executable object. Not freed by `future_free()`.
 * @param[in] run       Function to start execution. Must return 0 on success.
 * @param[in] halt      Function to cancel execution or `NULL`. Parameters are
 *                      `obj` and thread on which `run` is executing. Must
 *                      return 0 on success and `false` on failure. If
 *                      `NULL`, then the thread on which `run` is executing
 *                      is sent a SIGTERM. NB: `pthread_cond_wait()` doesn't
 *                      return when interrupted, so a task that uses it --
 *                      including via `StopFlag` --  should explicitly specify
 *                      the halt function that doesn't use `pthread_kill()`.
 * @param[in] get       Function to return the result of the task or `NULL` if
 *                      no result will be returned.
 * @retval    `NULL`    Out of memory. `log_add()` called.
 * @threadsafety        Safe
 */
Future*
future_new(
        void*   obj,
        int   (*run)(void* obj),
        int   (*halt)(void* obj, pthread_t thread),
        int   (*get)(void* obj, void** result));

/**
 * Frees a future. Doesn't free the object given to `future_new()` unless
 * `future_addFree()` was called.
 *
 * NB: Undefined behavior will result if any of the other future functions are
 * called after this function on the same future.
 *
 * NB: A memory-leak will occur if the task allocated a result object which was
 * not retrieved by a call to `future_getResult()`.
 *
 * @param[in] future     Future to be freed
 * @retval    0          Success
 * @retval    EINVAL     Future is being executed. Future wasn't freed.
 *                       `log_add()` called.
 * @threadsafety         Thread-compatible but not thread-safe
 */
int
future_free(Future* future);

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
 * Executes a future's task on the current thread.
 *
 * @param[in,out] future     Future
 * @retval        0          Success
 * @retval        EINVAL     Future isn't in appropriate state
 * @threadsafety             Safe
 */
int
future_run(Future* future);

/**
 * Cancels a future. If the task hasn't started, then it should never start.
 * Does nothing if the task has already completed.
 *
 * @param[in,out] future  Future to be canceled
 * @retval        0       Success
 * @return                Error code. `log_add()` called.
 * @threadsafety          Safe
 */
int
future_cancel(Future* future);

/**
 * Returns the result of a future's task. Doesn't wait for the task to complete;
 * consequently, it must be known that the task has completed. This function
 * should only be used by the `Executor` module.
 *
 * @param[in,out] future     Future
 * @param[out]    result     Result of task execution or `NULL`
 * @retval        0          Success. `*result` is set if `result != NULL`.
 * @retval        ECANCELED  Task was canceled
 * @return                   Return value of `future_new()`'s `runFunc` argument
 * @threadsafety             Safe
 */
int
future_getResultNoWait(
        Future* future,
        void**  result);

/**
 * Returns the result of a future's task. Blocks until the task has completed
 * and any executing thread is joined.
 *
 * @param[in,out] future     Future
 * @param[out]    result     Result of task execution or `NULL`
 * @retval        0          Success. `*result` is set if `result != NULL`.
 * @retval        EDEADLK    Deadlock detected. Can be because `future_run()`
 *                           was executed on the current thread.
 * @retval        ECANCELED  Task was canceled
 * @return                   Return value of `future_new()`'s `runFunc` argument
 * @threadsafety             Safe
 */
int
future_getResult(
        Future* const restrict future,
        void** const restrict  result);

/**
 * Returns the result of a future's task and frees the future. Blocks until the
 * task has completed. The future should not be dereferenced upon return.
 *
 * @param[in,out] future     Future
 * @param[out]    result     Result of task execution or `NULL`. A memory-leak
 *                           will occur if the task allocated a result object
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
 * Indicates if the run function given to `future_new()` was called.
 *
 * @param[in] future
 * @retval    `true`   Run function was called
 * @retval    `false`  Run function was not called
 */
bool
future_runFuncCalled(Future* future);

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
