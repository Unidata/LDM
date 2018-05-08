/**
 * This file defines the future of an asynchronous task.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Future.c
 *  Created on: May 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "Future.h"
#include "log.h"
#include "Thread.h"

#include <errno.h>
#include <signal.h>

/******************************************************************************
 * Asynchronous task:
 ******************************************************************************/

typedef struct {
    void*           obj;
    void*         (*runFunc)(void* obj);
    bool          (*cancelFunc)(void* obj, pthread_t thread);
} Task;

static bool
task_defaultCancelFunc(
        void*     obj,
        pthread_t thread)
{
    int status = pthread_kill(thread, SIGTERM);

    if (status) {
        if (status == ESRCH) {
            status = 0; // Thread already terminated
        }
        else {
            log_add_errno(status, "Couldn't signal task's thread");
        }
    }

    return status == 0;
}

static Task*
task_new(
        void* const obj,
        void*     (*runFunc)(void* obj),
        bool      (*cancelFunc)(void* obj, pthread_t thread))
{
    Task* task = log_malloc(sizeof(Task), "asynchronous task");

    if (task != NULL) {
        task->obj = obj;
        task->runFunc = runFunc;
        task->cancelFunc = cancelFunc ? cancelFunc : task_defaultCancelFunc;
    }

    return task;
}

inline static void
task_delete(Task* const task)
{
    free(task);
}

inline static void*
task_run(Task* const task)
{
    return task->runFunc(task->obj); // Blocks while executing
}

inline static bool
task_cancel(
        Task* const     task,
        const pthread_t thread)
{
    return task->cancelFunc(task->obj, thread);
}

inline static void*
task_getObj(const Task* const task)
{
    return task->obj;
}

inline static bool
task_areEqual(
        const Task* const task1,
        const Task* const task2)
{
    return task1->obj == task2->obj && task1->runFunc == task2->runFunc &&
            task1->cancelFunc == task2->cancelFunc;
}

/******************************************************************************
 * Future of asynchronous task:
 ******************************************************************************/

typedef enum {
    STATE_INITIALIZED,//!< Future initialized but not running
    STATE_RUNNING,    //!< Future running
    STATE_COMPLETED   //!< Future completed (might have been canceled)
} State;

struct future {
    Task*           task;
    void*           result;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    pthread_t       thread;
    State           state;
    bool            wasCanceled;
};

/**
 * Locks a future.
 *
 * @param[in] future  Future
 * @threadsafety      Safe
 */
static void
lock(Future* const future)
{
    int status = pthread_mutex_lock(&future->mutex);
    log_assert(status == 0);
}

/**
 * Unlocks a future.
 *
 * @param[in] future  Future
 * @threadsafety      Safe
 */
static void
unlock(Future* const future)
{
    int status = pthread_mutex_unlock(&future->mutex);
    log_assert(status == 0);
}

/**
 * Compares and sets the state of a future.
 *
 * @param[in,out] future    Future
 * @param[in]     expect    Expected state of future
 * @param[in]     newState  New state for future if current state equals
 *                          expected state
 * @retval        `true`    Future was in expected state
 * @retval        `false`   Future was not in expected state
 * @threadsafety            Safe
 */
static bool
cas(    Future* const future,
        const State   expect,
        const State   newState)
{
    lock(future);
        const bool wasExpected = future->state == expect;

        if (wasExpected)
            future->state = newState;
    unlock(future);

    return wasExpected;
}

static int
init(   Future* const future,
        void* const   obj,
        void*       (*runFunc)(void* obj),
        bool        (*haltFunc)(void* obj, pthread_t thread))
{
    int status;

    future->state = STATE_INITIALIZED;
    future->result = NULL;
    future->wasCanceled = false;
    future->task = task_new(obj, runFunc, haltFunc);

    if (future->task == NULL) {
        log_add("Couldn't create new task");
        status = ENOMEM;
    }
    else {
        status = mutex_init(&future->mutex, PTHREAD_MUTEX_ERRORCHECK, true);

        if (status == 0) {
            status = pthread_cond_init(&future->cond, NULL);

            if (status)
                log_add_errno(status, "Couldn't initialize condition-variable");
        }
    }

    return status;
}

static void
deinit(Future* const future)
{
    int status = pthread_cond_destroy(&future->cond);
    log_assert(status == 0);

    status = pthread_mutex_destroy(&future->mutex);
    log_assert(status == 0);

    task_delete(future->task);
}

/**
 * Waits for a future's task to complete.
 *
 * @pre                      Future is locked
 * @param[in,out] future     Future
 * @retval        0          Success
 * @retval        EDEADLK    Deadlock detected
 * @post                     Future is locked
 * @threadsafety             Safe
 */
static int
wait(Future* const restrict  future)
{
    /*
     * An implementation based on a condition-variable was chosen (rather than
     * one based on `pthread_join()`) in order to allow a simple Executor that
     * uses detached threads to avoid cleaning-up after them.
     */

    int status = 0;

    while (status == 0 && future->state != STATE_COMPLETED)
        status = pthread_cond_wait(&future->cond, &future->mutex);

    return status;
}

Future*
future_new(
        void*   obj,
        void* (*runFunc)(void* obj),
        bool  (*haltFunc)(void* obj, pthread_t thread))
{
    Future* future = log_malloc(sizeof(Future), "future of asynchronous task");

    if (future) {
        int status = init(future, obj, runFunc, haltFunc);

        if (status) {
            log_add("Couldn't initialize future");
            free(future);
            future = NULL;
        }
    }

    return future;
}

int
future_delete(Future* future)
{
    int status;

    if (future == NULL) {
        status = 0;
    }
    else {
        if (cas(future, STATE_RUNNING, STATE_RUNNING)) {
            status = EBUSY;
        }
        else {
            deinit(future);
            free(future);
            status = 0;
        }
    }

    return status;
}

int
future_run(Future* const future)
{
    // Only this function changes the state of a future

    int status;

    lock(future);
        if (future->state == STATE_COMPLETED) {
            unlock(future);
            status = 0;
        }
        else if (future->state != STATE_INITIALIZED) {
            unlock(future);
            status = EINVAL;
        }
        else {
            if (future->wasCanceled) {
                future->state = STATE_COMPLETED;
            }
            else {
                future->state = STATE_RUNNING;
                future->thread = pthread_self();

                unlock(future);
                    void* const result = task_run(future->task);
                lock(future);
                    future->state = STATE_COMPLETED;

                    if (!future->wasCanceled)
                        future->result = result;
            }

            status = pthread_cond_broadcast(&future->cond);
            log_assert(status == 0);

            unlock(future);
        } // State will change

    return status;
}

bool
future_cancel(Future* const future)
{
    lock(future);
        if (future->state == STATE_INITIALIZED) {
            future->wasCanceled = true;
            unlock(future);
        }
        else if (future->state == STATE_COMPLETED) {
            unlock(future);
        }
        else {
            // `future->state == STATE_RUNNING`
            unlock(future);
                if (task_cancel(future->task, future->thread)) {
                    lock(future);
                        future->wasCanceled = true;
                    unlock(future);
                }
        }

    return future->wasCanceled;
}

int
future_wait(
        Future* future,
        void**  result)
{
    int  status;

    lock(future);
        if (future->state == STATE_COMPLETED) {
            if (future->wasCanceled) {
                status = ECANCELED;
            }
            else if (result) {
                *result = future->result;
                status = 0;
            }
        }
        else {
            status = wait(future);

            if (status == 0) {
                if (future->wasCanceled) {
                    status = ECANCELED;
                }
                else if (result) {
                    *result = future->result;
                }
            }
        }
    unlock(future);

    return status;
}

void*
future_getObj(Future* future)
{
    return task_getObj(future->task);
}

bool
future_areEqual(
        const Future* future1,
        const Future* future2)
{
    return task_areEqual(future1->task, future2->task);
}
