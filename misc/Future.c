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
#include "../../misc/Future.h"

#include "config.h"

#include "log.h"
#include <errno.h>
#include <signal.h>
#include "../../misc/Thread.h"

/******************************************************************************
 * Asynchronous task:
 ******************************************************************************/

typedef struct {
    void*           obj;
    int           (*run)(void* obj, void** result);
    int           (*cancel)(void* obj, pthread_t thread);
} Task;

static int
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

    return status;
}

static Task*
task_new(
        void* const obj,
        int       (*run)(void* obj, void** result),
        int       (*cancel)(void* obj, pthread_t thread))
{
    Task* task = log_malloc(sizeof(Task), "asynchronous task");

    if (task != NULL) {
        task->obj = obj;
        task->run = run;
        task->cancel = cancel ? cancel : task_defaultCancelFunc;
    }

    return task;
}

inline static void
task_delete(Task* const task)
{
    free(task);
}

inline static int
task_run(
        Task* const restrict  task,
        void** const restrict result)
{
    return task->run(task->obj, result); // Blocks while executing
}

inline static int
task_cancel(
        Task* const     task,
        const pthread_t thread)
{
    return task->cancel(task->obj, thread);
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
    return task1->obj == task2->obj && task1->run == task2->run &&
            task1->cancel == task2->cancel;
}

/******************************************************************************
 * Future of asynchronous task:
 ******************************************************************************/

typedef enum {
    STATE_INITIALIZED,///< Future initialized but not running
    STATE_RUNNING,    ///< Future running
    STATE_COMPLETED,  ///< Future completed (might have been canceled)
    STATE_JOINED      ///< Thread on which future is executing has been joined
} State;

struct future {
    Task*           task;
    void*           result;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    pthread_t       thread;
    State           state;
    int             runFuncStatus;
    bool            wasCanceled;
    bool            haveThread;
    bool            runFuncCalled;
};

/**
 * Locks a future.
 *
 * @param[in] future  Future
 * @threadsafety      Safe
 */
static void
future_lock(Future* const future)
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
future_unlock(Future* const future)
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
future_cas(    Future* const future,
        const State   expect,
        const State   newState)
{
    future_lock(future);
        const bool wasExpected = future->state == expect;

        if (wasExpected)
            future->state = newState;
    future_unlock(future);

    return wasExpected;
}

static int
future_init(   Future* const future,
        void* const   obj,
        int         (*run)(void* obj, void** result),
        int         (*halt)(void* obj, pthread_t thread))
{
    int status;

    future->state = STATE_INITIALIZED;
    future->result = NULL;
    future->wasCanceled = false;
    future->haveThread = false;
    future->runFuncCalled = false;
    future->runFuncStatus = 0;
    future->task = task_new(obj, run, halt);

    if (future->task == NULL) {
        log_add("Couldn't create new task");
        status = ENOMEM;
    }
    else {
        status = mutex_init(&future->mutex, PTHREAD_MUTEX_ERRORCHECK, true);

        if (status)
            task_delete(future->task);
    } // `future->task` created

    return status;
}

static void
future_destroy(Future* const future)
{
    future_lock(future);
        task_delete(future->task);
    future_unlock(future);

    int status = pthread_mutex_destroy(&future->mutex);
    log_assert(status == 0);
}

/**
 * Joins the thread on which a future is executing
 *
 * @pre                      Future is unlocked
 * @param[in,out] future     Future
 * @retval        0          Success
 * @retval        EDEADLK    Deadlock detected
 * @post                     Future is unlocked
 * @threadsafety             Safe
 */
static int
future_join(Future* const restrict  future)
{
    log_assert(future->haveThread);

    int status = pthread_join(future->thread, NULL);

    if (status) {
        log_add_errno(status, "Couldn't join thread");
    }
    else {
        future_lock(future);
            future->state = STATE_JOINED;
        future_unlock(future);
    }

    return status;
}

Future*
future_new(
        void*   obj,
        int   (*run)(void* obj, void** result),
        int   (*halt)(void* obj, pthread_t thread))
{
    Future* future = log_malloc(sizeof(Future), "future of asynchronous task");

    if (future) {
        int status = future_init(future, obj, run, halt);

        if (status) {
            log_add("Couldn't initialize future");
            free(future);
            future = NULL;
        }
    }

    return future;
}

int
future_free(Future* future)
{
    int status;

    if (future == NULL) {
        status = 0;
    }
    else {
        if (future_cas(future, STATE_RUNNING, STATE_RUNNING)) {
            status = EBUSY;
        }
        else {
            future_destroy(future);
            free(future);
            status = 0;
        }
    }

    return status;
}

void
future_setThread(
        Future* const   future,
        const pthread_t thread)
{
    future_lock(future);
        if (!future->haveThread) {
            future->thread = thread;
            future->haveThread = true;
        }
    future_unlock(future);
}

pthread_t
future_getThread(Future* const   future)
{
    future_lock(future);
        log_assert(future->haveThread);
        pthread_t thread = future->thread;
    future_unlock(future);

    return thread;
}

void*
future_getObj(Future* const future)
{
    return future->task->obj;
}

int
future_run(Future* const future)
{
    int status;

    future_lock(future);
        if (future->state == STATE_COMPLETED) {
            future_unlock(future);
            status = 0;
        }
        else if (future->state == STATE_RUNNING ||
                future->state == STATE_JOINED) {
            future_unlock(future);
            status = EINVAL;
        }
        else {
            // `future->state == STATE_INITIALIZED`
            if (!future->haveThread) {
                future->thread = pthread_self();
                future->haveThread = true;
            }

            if (future->wasCanceled) {
                future->state = STATE_COMPLETED;
                status = 0;
            }
            else {
                future->state = STATE_RUNNING;
                future->runFuncCalled = true;

                future_unlock(future);
                    void* result;
                    status = task_run(future->task, &result);
                future_lock(future);
                    future->state = STATE_COMPLETED;
                    future->runFuncStatus = status;

                    if (!future->wasCanceled && status == 0)
                        future->result = result;
            } // future wasn't canceled

            future_unlock(future);
        } // Future is initialized. State will change

    return status;
}

int
future_cancel(Future* const future)
{
    int status;

    future_lock(future);
        if (future->state == STATE_INITIALIZED) {
            future->wasCanceled = true;
            future_unlock(future);
            status = 0;
        }
        else if (future->state == STATE_COMPLETED ||
                future->state == STATE_JOINED) {
            future_unlock(future);
            status = 0;
        }
        else {
            // `future->state == STATE_RUNNING`
            future_unlock(future);

            status = task_cancel(future->task, future->thread);

            if (status == 0) {
                future_lock(future);
                    future->wasCanceled = true;
                future_unlock(future);
            }
        }

    return status;
}

int
future_getResultNoWait(
        Future* future,
        void**  result)
{
    int status;

    future_lock(future);
        if (future->wasCanceled) {
            status = ECANCELED;
        }
        else {
            status = future->runFuncStatus;

            if (status == 0 && result)
                *result = future->result;
        }
    future_unlock(future);

    return status;
}

int
future_getResult(
        Future* future,
        void**  result)
{
    // NB: This function can be called before `future_run()` due to asynchrony
    // in thread creation
    future_lock(future);
        int status;

        if (future->state != STATE_JOINED) {
            future_unlock(future);
            status = future_join(future);
        }

        if (status == 0)
            status = future_getResultNoWait(future, result);

    return status;
}

void
future_setResult(
        Future* const restrict future,
        void* const restrict   result)
{
    future_lock(future);
        future->result = result;
    future_unlock(future);
}

bool
future_runFuncCalled(Future* const future)
{
    mutex_lock(&future->mutex);
        return future->runFuncCalled;
    mutex_unlock(&future->mutex);
}

bool
future_areEqual(
        const Future* future1,
        const Future* future2)
{
    return task_areEqual(future1->task, future2->task);
}
