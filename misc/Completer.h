/**
 * This file declares an object that decorates an Executor with a queue of
 * completed asynchronous tasks.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Completer.h
 *  Created on: May 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include <stdbool.h>
#include "Executor.h"

#ifndef MCAST_LIB_LDM7_COMPLETER_H_
#define MCAST_LIB_LDM7_COMPLETER_H_

typedef struct completer Completer;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Creates a new completion service.
 *
 * @retval    `NULL`  Failure. `log_add()` called.
 * @return            New completion service
 */
Completer*
completer_new();

/**
 * Deletes a completion service. Opposite of `completer_new()`.
 *
 * @param[in,out] comp  Completion service to be deleted
 */
void
completer_free(Completer* const comp);

/**
 * Submits a task for asynchronous execution to a completion service.
 *
 * @param[in,out] comp      Completion service
 * @param[in]     obj       Object to be executed. May be `NULL`.
 * @param[in]     run       Function to execute `obj`
 * @param[in]     halt      Function to cancel execution. Must return 0 on
 *                          success.
 * @retval        `NULL`    Failure. `log_add()` called.
 * @return                  Future of the submitted task. *NB: `future_free()`
 *                          must not be called on this future until it has been
 *                          returned by `completer_take()`.*
 */
Future*
completer_submit(
        Completer* const comp,
        void* const      obj,
        int            (*run)(void* obj, void** result),
        int            (*halt)(void* obj, pthread_t thread));

/**
 * Returns the future of the oldest completed task. Blocks until a future is
 * available or no more futures will be available.
 *
 * @param[in] comp    Completion service
 * @retval    `NULL`  No more futures can be returned
 * @return            Future of oldest completed task. `future_free()` may be
 *                    called on the future -- but if it is and the task
 *                    allocated a result object then a memory-leak will occur.
 */
Future*
completer_take(Completer* const comp);

/**
 * Shuts down a completion service. Doesn't block. Caller should repeatedly call
 * `completer_take()` (and deal with each future) until the completion service
 * is empty.
 *
 * NB: This function won't return if an uncompleted future is canceled but its
 * task doesn't complete.
 *
 * @param[in,out] comp    Completion service
 * @param[in]     now     Whether or not to cancel uncompleted futures
 * @retval        0       Success
 * @retval        ENOMEM  Out of memory
 * @threadsafety          Safe
 * @see `completer_submit()`
 * @see `future_cancel()`
 */
int
completer_shutdown(
        Completer* const comp,
        const bool       now);

/**
 * Returns the number of futures in a completion service (both completed and
 * uncompleted).
 *
 * @param[in] comp  Completion service
 * @return          Number of futures
 */
size_t
completer_size(Completer* const comp);

#ifdef __cplusplus
    }
#endif

#endif /* MCAST_LIB_LDM7_COMPLETER_H_ */
