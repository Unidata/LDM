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
 * @param[in] obj       Executable object
 * @param[in] runFunc   Function to start execution. Must return 0 on success.
 * @param[in] haltFunc  Function to stop execution or `NULL`. Parameters are
 *                      `obj` and thread on which `runFunc` is executing. Must
 *                      return 0 on success and `false` on failure. If
 *                      `NULL`, then the thread on which `runFunc` is executing
 *                      is sent a SIGTERM. NB: `pthread_cond_wait()` doesn't
 *                      return when interrupted, so a task that uses it --
 *                      including via `StopFlag` --  should explicitly specify
 *                      the halt function that doesn't use `pthread_kill()`.
 * @retval    `NULL`    Out of memory. `log_add()` called.
 * @threadsafety        Safe
 */
Future*
future_new(
        void*   obj,
        int   (*runFunc)(void* obj, void** result),
        int   (*haltFunc)(void* obj, pthread_t thread));

/**
 *
 * @param future
 * @retval 0      Success
 * @retval EBUSY  Task is executing
 * @threadsafety  Safe
 */
int
future_free(Future* future);

/**
 * Identify the thread that is or is about to execute a future.
 *
 * @param[in,out] future  Future
 * @param[in]     thread  Thread identifier
 */
void
future_setThread(
        Future* const   future,
        const pthread_t thread);

/**
 * Returns the thread on which a future is executing. Bad things will happen if
 * `future_run()` or `future_setThread()` isn't called first.
 *
 * @param[in] future  Future
 * @return            Thread on which future is executing
 * @threadsafety      Safe
 */
pthread_t
future_getThread(Future* const future);

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
 * Cancels a future.
 *
 * @param[in,out] future  Future to be canceled
 * @retval        0  Success
 * @return           Error code. `log_add()` called.
 * @threadsafety     Safe
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
 * Returns the result of a future's task. Waits for the task to complete.
 *
 * @param[in,out] future     Future
 * @param[out]    result     Result of task execution or `NULL`
 * @retval        0          Success. `*result` is set if `result != NULL`.
 * @retval        EDEADLK    Deadlock detected
 * @retval        ECANCELED  Task was canceled
 * @return                   Return value of `future_new()`'s `runFunc` argument
 * @threadsafety             Safe
 */
int
future_getResult(
        Future* future,
        void**  result);

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
