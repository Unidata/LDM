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
 * @param[in] runFunc   Function to start execution
 * @param[in] haltFunc  Function to stop execution or `NULL`. Parameters are
 *                      `obj` and thread on which `runFunc` is executing. Must
 *                      return `true` on success and `false` on failure. If
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
        void* (*runFunc)(void* obj),
        bool  (*haltFunc)(void* obj, pthread_t thread));

/**
 *
 * @param future
 * @retval 0      Success
 * @retval EBUSY  Task is executing
 * @threadsafety  Safe
 */
int
future_delete(Future* future);

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
 *
 * @param future
 * @retval `true`   Task was canceled
 * @retval `false`  Task could not be canceled
 * @threadsafety    Safe
 */
bool
future_cancel(Future* future);

/**
 * Waits for a future's task to complete.
 *
 * @param[in,out] future     Future
 * @param[out]    result     Result of task execution or `NULL`
 * @retval        0          Success. `*result` is set if `result != NULL`.
 * @retval        EDEADLK    Deadlock detected
 * @retval        ECANCELED  Task was canceled
 * @threadsafety             Safe
 */
int
future_wait(
        Future* future,
        void**  result);

/**
 * Returns the executable object given to `future_new()`.
 *
 * @param[in] future  Future
 * @return            Executable object given to `future_new()`
 * @threadsafety      Safe
 */
void*
future_getObj(Future* future);

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
