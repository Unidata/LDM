/**
 * This file defines an executor of asynchronous tasks.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Executor.h
 *  Created on: May 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "Future.h"

#ifndef MCAST_LIB_LDM7_EXECUTOR_H_
#define MCAST_LIB_LDM7_EXECUTOR_H_

#ifdef __cplusplus
    extern "C" {
#endif

typedef struct executor Executor;

/**
 * Creates a new executor.
 *
 * @retval `NULL`  Failure. `log_add()` called.
 * @return         New executor.
 */
Executor*
executor_new();

/**
 * Deletes an executor.
 *
 * @param[in,out] executor  Executor to be deleted
 */
void
executor_delete(Executor* const executor);

/**
 * Submits a job to be executed asynchronously.
 *
 * @param[in,out] exec      Executor
 * @param[in,out] obj       Job object
 * @param[in]     runFunc   Function to run job
 * @param[in]     haltFunc  Function to cancel job
 * @retval        `NULL`    Failure. `log_add()` called.
 * @return                  Future of job. Caller should call `future_delete()`
 *                          when it's no longer needed.
 */
Future*
executor_submit(
        Executor* const exec,
        void* const     obj,
        void*         (*runFunc)(void* obj),
        bool          (*haltFunc)(void* obj, pthread_t thread));

/**
 * Shuts down an executor. Upon return, the executor will no longer accept new
 * jobs.
 *
 * @param[in,out] exec  Executor
 * @param[in]     now   Whether or not to cancel running jobs
 */
void
executor_shutdown(
        Executor* const exec,
        const bool      now);

#ifdef __cplusplus
    }
#endif

#endif /* MCAST_LIB_LDM7_EXECUTOR_H_ */
