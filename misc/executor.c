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

#include "doubly_linked_list.h"
#include "executor.h"
#include "log.h"

#include <pthread.h>
#include <stdlib.h>

struct Task {
    void*         (*start)(void*); ///< client's start function
    void*           arg;           ///< argument for start function
    void*           result;        ///< result of submitted task
    Executor*       executor;      ///< the associated executor
    DllElt*         elt;           ///< the containing list element
    pthread_t       thread;        ///< thread that runs the task
};

struct Executor {
    Dll*            running;   ///< list of running threads
    Dll*            completed; ///< list of completed threads
    pthread_mutex_t mutex;     ///< to maintain the consistency of the executor
    pthread_cond_t  cond;      ///< for signaling the completion of a thread
};

/**
 * Returns a new task that wraps a client-submitted task.
 *
 * @param[in] start  Pointer to the start function of the client-submitted task.
 * @param[in] arg    Pointer to the argument for the start function.
 * @retval    NULL   Error. `log_add()` called.
 * @return           Pointer to the new corresponding task.
 */
static Task*
task_new(
    Executor* const executor,
    void*   (*const start)(void*),
    void* const     arg)
{
    Task* const task = LOG_MALLOC(sizeof(Task), "task");

    if (task) {
        task->executor = executor;
        task->start = start;
        task->arg = arg;
        task->result = NULL;
        task->elt = NULL;
    }

    return task;
}

/**
 * Frees a task.
 *
 * @param[in] task  Pointer to the task to be freed.
 */
void
task_free(
    Task* const task)
{
    free(task);
}

/**
 * Cleans-up after a task completes. The task is moved from the associated
 * executor's list of running tasks to that executor's list of completed tasks.
 *
 * @param[in] arg  Pointer to the task to be cleaned-up.
 */
static void
task_cleanup(
    void* const arg)
{
    Task* const task = (Task*)arg;

    if (ex_moveToCompleted(task->executor, task->elt)) {
        LOG_ADD0("Couldn't move task from running list to completed list");
        log_log(LOG_ERR);
    }
}

/**
 * Starts executing a task on the current thread.
 *
 * @param[in] arg  Pointer to the task to execute on the current thread.
 */
static void*
task_start(
    void* const arg)
{
    Task* const task = (Task*)arg;
    int         status = ex_addToRunning(task->executor, task);

    if (status) {
        LOG_ADD0("Couldn't add task to list of executing tasks");
        log_log(LOG_ERR);
    }
    else {
        pthread_cleanup_push(task_cleanup, task);
        task->result = task->start(task->arg);
        pthread_cleanup_pop(1);
    }

    return (void*)status;
}

/**
 * Executes a task in a new thread.
 *
 * @param[in] task    Pointer to the task to be started.
 * @retval    0       Success.
 * @retval    EAGAIN  The system lacked the necessary resources to create
 *                    another thread, or the system-imposed limit on the total
 *                    number of threads in a process {PTHREAD_THREADS_MAX} would
 *                    be exceeded.
 */
static int
task_execute(
    Task* const task)
{
    int status = pthread_create(&task->thread, NULL, task_start, task);

    if (status) {
        LOG_SERROR0("Couldn't create new thread");
    }
    else {
        /*
         * The following makes the thread unjoinable and ensures destruction of
         * all thread resources.
         */
        (void)pthread_detach(task->thread);
    }

    return status;
}

/**
 * Cancels a task.
 *
 * @param[in] task  Pointer to the task to be cancelled.
 */
void
task_cancel(
    Task* const task)
{
    (void)pthread_cancel(task->thread);
}

/**
 * Returns a new executor.
 *
 * @return       Pointer to a new executor.
 * @retval NULL  Error. `log_add()` called.
 */
Executor*
ex_new(void)
{
    Executor* const ex = LOG_MALLOC(sizeof(Executor),
            "executor of concurrent tasks");

    if (ex == NULL)
        goto return_NULL;

    ex->running = dll_new();
    if (ex->running == NULL)
        goto free_executor;

    ex->completed = dll_new();
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
    dll_free(ex->completed);
free_running:
    dll_free(ex->running);
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
 * Submits a client task to be run on a separate thread.
 *
 * @param[in]  start    Pointer to the start function to be executed on a
 *                      separate thread.
 * @param[in]  arg      Pointer to the argument to be passed to the function.
 * @retval     NULL     Failure. `log_add()` called.
 * @return              Pointer to the submitted task. The client should either
 *                      leave it alone or call `task_cancel()` on it.
 */
Task*
ex_submit(
    Executor* const  ex,
    void*    (*const start)(void* arg),
    void* const      arg,
    pthread_t* const thread)
{
    Task* task = task_new(ex, start, arg);

    if (task == NULL) {
        LOG_ADD0("Couldn't create new task");
    }
    else if (task_execute(task)) {
        LOG_ADD0("Couldn't execute task");
        task_free(task);
        task = NULL;
    }

    return task;
}

/**
 * Adds a task to an executor's list of running tasks. This function should only
 * be called by a `Task`.
 *
 * @param[in] ex      Pointer to the executor.
 * @param[in] task    Pointer to the task to be added.
 * @retval    0       Success.
 * @retval    ENOMEM  No more memory. `log_add()` called.
 */
int
ex_addToRunning(
    Executor* const ex,
    Task* const     task)
{
    (void)pthread_mutex_lock(&ex->mutex);
    task->elt = dll_add(ex->running, task);
    (void)pthread_mutex_unlock(&ex->mutex);

    return task->elt
            ? 0
            : ENOMEM;
}

/**
 * Moves a task from an executor's list of running tasks to that executor's list
 * of completed tasks. This function should only be called by a `Task`.
 *
 * @param[in] ex      Pointer to the executor.
 * @param[in] elt     Pointer to the element in the list of running tasks to be
 *                    moved.
 * @retval    0       Success.
 * @retval    ENOMEM  Out-of-memory.
 */
int
ex_moveToCompleted(
    Executor* const ex,
    DllElt* const   elt)
{
    int     status;
    Task*   task;

    (void)pthread_mutex_lock(&ex->mutex);
    task = dll_remove(ex->running, elt);
    if (dll_add(ex->completed, task) == NULL) {
        LOG_ADD0("Couldn't add task to list of completed tasks");
        status = ENOMEM;
    }
    else {
        task->elt = NULL;
        (void)pthread_cond_broadcast(&ex->cond);
        status = 0;
    }
    (void)pthread_mutex_unlock(&ex->mutex);

    return status;
}

/**
 * Retrieves and removes the next completed task, waiting if none are yet
 * present. Returns immediately if there are no completed or running tasks.
 *
 * @param[in]  ex      Pointer to the executor.
 * @retval     NULL    There are no completed or running tasks.
 * @return             Pointer to the next completed task. The client should
 *                     call `task_free()` when it's no longer needed.
 */
Task*
ex_take(
    Executor* const   ex)
{
    Task* task;

    (void)pthread_mutex_lock(&ex->mutex);
    if (dll_size(ex->completed) == 0 && dll_size(ex->running) == 0) {
        task = NULL;
    }
    else {
        while ((task = dll_getFirst(ex->completed)) == NULL)
            pthread_cond_wait(&ex->cond, &ex->mutex);
    }
    (void)pthread_mutex_unlock(&ex->mutex);
    return task;
}

/**
 * Cancels all executing tasks. The client should subsequently drain the
 * executor of completed tasks by calling `ex_take()` until it returns `NULL`.
 *
 * @param[in]  ex  Pointer to the executor.
 */
void
ex_cancel(
    Executor* const ex)
{
    Task* task;

    (void)pthread_mutex_lock(&ex->mutex);
    while ((task = dll_getFirst(ex->running)) != NULL)
        task_cancel(task);
    (void)pthread_mutex_unlock(&ex->mutex);
}
