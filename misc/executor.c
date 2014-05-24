/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file executor.c
 *
 * This file implements a thread-safe executor of concurrent jobs.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "doubly_linked_stack.h"
#include "executor.h"
#include "log.h"

#include <pthread.h>
#include <stdlib.h>

struct Executor {
    Dls*            running;   ///< stack of running threads
    Dls*            completed; ///< stack of completed threads
    pthread_mutex_t mutex;     ///< to maintain the consistency of the stacks
    pthread_cond_t  cond;      ///< for signaling the completion of a thread
};

/**
 * Returns a new executor.
 *
 * @return       Pointer to a new executor.
 * @retval NULL  Error. `log_add()` called.
 */
Executor*
ex_new(void)
{
    Executor* const ex = LOG_MALLOC(sizeof(Executor), "executor of concurrent jobs");

    if (ex == NULL)
        goto return_NULL;

    ex->running = dls_new();
    if (ex->running == NULL)
        goto free_executor;

    ex->completed = dls_new();
    if (ex->completed == NULL)
        goto free_running;

    if (pthread_mutex_init(&ex->mutex, NULL)) {
        LOG_SERROR0("Couldn't initialize mutex of executor");
        goto free_completed;
    }

    if (pthread_cond_init(&ex->cond, NULL)) {
        LOG_SERROR0("Couldn't initialize condition-variable of executor");
        goto free_mutex;
    }

    return ex;

free_mutex:
    pthread_mutex_destroy(&ex->mutex);
free_completed:
    dls_free(ex->completed);
free_running:
    dls_free(ex->running);
free_executor:
    free(ex);
return_NULL:
    return NULL;
}

/**
 * Frees an executor.
 *
 * @param[in] ex  Pointer to the executor to be freed.
 */
void
ex_free(
    Executor* const ex)
{
    // TODO
}

/**
 * Submits a function to be run on a separate thread.
 *
 * @param[in]  start    Pointer to the start function to be executed on a
 *                      separate thread.
 * @param[in]  arg      Pointer to the argument to be passed to the function.
 * @param[out] thread   Pointer to the thread identifier to be set.
 * @retval     0        Success.
 * @retval     EAGAIN   The system lacked the necessary resources to create
 *                      another thread, or the system-imposed limit on the total
 *                      number of threads in a process {PTHREAD_THREADS_MAX}
 *                      would be exceeded. `log_add()` called.
 */
int
ex_submit(
    Executor* const  ex,
    void*    (*const start)(void* arg),
    void* const      arg,
    pthread_t* const thread)
{
    // TODO
    int           status = pthread_create(thread, NULL, start, arg);

    if (status)
        LOG_SERROR0("Couldn't create new thread");

    return status;
}

/**
 * Retrieves and removes the job representing the next completed task, waiting
 * if none are yet present.
 *
 * @param[in]  ex      Pointer to the executor.
 * @param[out] thread  Pointer to the thread identifier of the next completed
 *                     task.
 * @retval    0        Success.
 */
int
ex_take(
    Executor* const   ex,
    pthread_t* const  thread)
{
    // TODO
    return 0;
}

/**
 * Cancels all executing jobs.
 *
 * @param[in]  ex  Pointer to the executor.
 * @retval     0   Success.
 */
int
ex_cancel(
    Executor* const ex)
{
    // TODO
    return 0;
}
