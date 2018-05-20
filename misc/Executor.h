/**
 * This file defines an execution service for asynchronous tasks.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Executor.h
 *  Created on: May 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "../../misc/Future.h"
#include "config.h"


#ifndef MCAST_LIB_LDM7_EXECUTOR_H_
#define MCAST_LIB_LDM7_EXECUTOR_H_

#ifdef __cplusplus
    extern "C" {
#endif

/******************************************************************************
 * Iterator over list of uncompleted futures
 ******************************************************************************/

typedef struct futuresIter FuturesIter;

/**
 * Deletes an iterator over a list of futures.
 *
 * @param[in,out] iter  Iterator
 */
void
futuresIter_delete(FuturesIter* const iter);

/**
 * Removes the future at the head of a list of futures that were awaiting
 * execution and returns it.
 *
 * @param[in,out] iter  Iterator over a list of futures that were awaiting
 *                      execution
 * @retval        NULL  No more such futures
 * @return              Future at head of list
 */
Future*
futuresIter_remove(FuturesIter* const iter);

/******************************************************************************
 * Execution service
 ******************************************************************************/

typedef struct executor Executor;

/**
 * Creates a new execution service.
 *
 * @param[in] afterCompletion  Function to call with task's future after the
 *                             task has completed -- either by having its run
 *                             function return or by being canceled. Must return
 *                             0 on success. May be `NULL` for no
 *                             post-completion processing.
 * @param[in] completer        Object to pass to `afterCompletion`. May be
 *                             `NULL`. Ignored if `afterCompletion` is `NULL`.
 * @retval    `NULL`           Failure. `log_add()` called.
 * @return                     New execution service.
 */
Executor*
executor_new(
        int       (*afterCompletion)(
                        void* const restrict   completer,
                        Future* const restrict future),
        void* const completer);

/**
 * Deletes an execution service.
 *
 * @param[in,out] executor  Execution service to be deleted
 */
void
executor_free(Executor* const executor);

/**
 * Submits a task to be executed asynchronously.
 *
 * @param[in,out] exec      Execution service
 * @param[in,out] obj       Job object
 * @param[in]     runFunc   Function to run task. Must return 0 on success.
 * @param[in]     haltFunc  Function to cancel task. Must return 0 on success.
 * @retval        `NULL`    Failure. `log_add()` called.
 * @return                  Future of task. Caller should call `future_delete()`
 *                          when it's no longer needed.
 */
Future*
executor_submit(
        Executor* const exec,
        void* const     obj,
        int           (*runFunc)(void* obj, void** result),
        int           (*haltFunc)(void* obj, pthread_t thread));

/**
 * Returns the number of uncompleted task.
 *
 * @param[in] executor  Execution service
 * @return              Number of uncompleted task
 */
size_t
executor_size(Executor* const executor);

/**
 * Shuts down an execution service by canceling all submitted but not completed
 * tasks. Upon return, the execution service will no longer accept task
 * submissions. Doesn't wait for tasks to complete. Returns an iterator over the
 * list of uncompleted futures whose run functions were not called.
 *
 * @param[in,out] exec    Execution service. Must exist for the duration of
 *                        returned iterator.
 * @param[in]     now     Whether or not to cancel uncompleted tasks
 * @retval        0       Success
 * @return                Error code. `log_add()` called.
 */
int
executor_shutdown(
        Executor* const exec,
        const bool      now);

#ifdef __cplusplus
    }
#endif

#endif /* MCAST_LIB_LDM7_EXECUTOR_H_ */
