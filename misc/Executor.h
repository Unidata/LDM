/**
 * This file defines an execution service for asynchronous jobs.
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
 * Execution service
 ******************************************************************************/

typedef struct executor Executor;

/**
 * Creates a new execution service.
 *
 * @retval    `NULL`           Failure. `log_add()` called.
 * @return                     New execution service.
 */
Executor*
executor_new(void);

/**
 * Deletes an execution service.
 *
 * @param[in,out] executor  Execution service to be deleted
 */
void
executor_free(Executor* const executor);

/**
 * Sets the object to call after a job has completed.
 *
 * @param[in] executor         Execution service
 * @param[in] completer        Object to pass to `afterCompletion()` as first
 *                             argument or `NULL`
 * @param[in] afterCompletion  Function to call after job has completed or
 *                             `NULL`. Should return `0` on success.
 */
void
executor_setAfterCompletion(
        Executor* const restrict executor,
        void* const restrict     completer,
        int                    (*afterCompletion)(
                                     void* restrict   completer,
                                     Future* restrict future));

/**
 * Submits a job to be executed asynchronously.
 *
 * @param[in,out] exec      Execution service
 * @param[in,out] obj       Job object or `NULL`
 * @param[in]     run       Function to run job. Must return 0 on success.
 * @param[in]     halt      Function to cancel job or `NULL` to obtain default
 *                          cancellation. Must return 0 on success.
 * @param[in]     get       Function to return result of job or `NULL` if no
 *                          result will be return
 * @retval        `NULL`    Failure. `log_add()` called.
 * @return                  Future of job. Caller should pass to `future_free()`
 *                          when it's no longer needed.
 */
Future*
executor_submit(
        Executor* const exec,
        void* const   obj,
        int         (*run)(void* obj, void** result),
        int         (*halt)(void* obj, pthread_t thread));

/**
 * Returns the number of uncompleted job.
 *
 * @param[in] executor  Execution service
 * @return              Number of uncompleted job
 */
size_t
executor_size(Executor* const executor);

/**
 * Shuts down an execution service and, optionally, asynchronously cancels all
 * submitted but not yet completed jobs. Upon return, the execution service will
 * no longer accept job submissions.
 *
 * @param[in,out] exec    Execution service
 * @param[in]     now     Whether or not to cancel uncompleted jobs
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
