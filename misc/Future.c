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
#include "Thread.h"

#include "log.h"
#include <errno.h>
#include <signal.h>

/******************************************************************************
 * Asynchronous task:
 ******************************************************************************/

typedef struct {
    void*   obj;
    int   (*run)(void* obj);
    int   (*cancel)(void* obj, pthread_t thread);
    int   (*get)(void* obj, void** result);
    void  (*free)(void* obj);
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
        int       (*run)(void* obj),
        int       (*cancel)(void* obj, pthread_t thread),
        int       (*get)(void* obj, void** result))
{
    Task* task = log_malloc(sizeof(Task), "asynchronous task");

    if (task != NULL) {
        task->obj = obj;
        task->run = run;
        task->cancel = cancel ? cancel : task_defaultCancelFunc;
        task->get = get;
        task->free = NULL;
    }

    return task;
}

inline static void
task_free(Task* const task)
{
    if (task->free)
        task->free(task->obj);
    free(task);
}

inline static int
task_run(Task* const restrict  task)
{
    return task->run(task->obj); // Blocks while executing
}

inline static int
task_cancel(
        Task* const     task,
        const pthread_t thread)
{
    return task->cancel(task->obj, thread);
}

inline static int
task_getResult(
        Task* const restrict  task,
        void** const restrict result)
{
    return task->get ? task->get(task->obj, result) : 0;
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
} State;

struct future {
    Task*           task;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    pthread_t       thread;
    State           state;
    int             runFuncStatus;
    bool            wasCanceled;
    bool            haveThread;
    bool            runFuncCalled;
};

inline static void
future_assertLocked(Future* const future)
{
#ifndef NDEBUG
    int status = pthread_mutex_trylock(&future->mutex);
    log_assert(status != 0);
#endif
}

inline static void
future_assertUnlocked(Future* const future)
{
#ifndef NDEBUG
    int status = pthread_mutex_trylock(&future->mutex);
    log_assert(status == 0);
    (void)pthread_mutex_unlock(&future->mutex);
#endif
}

/**
 * Locks a future.
 *
 * @param[in] future  Future
 * @threadsafety      Safe
 */
inline static void
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
inline static void
future_unlock(Future* const future)
{
    int status = pthread_mutex_unlock(&future->mutex);
    log_assert(status == 0);
}

/**
 * Sets the state of a future and signals its condition-variable.
 *
 * @pre                     `future->mutex` is locked
 * @param[in,out] future    Future
 * @param[in]     newState  New state for future
 * @retval        0         Success
 * @post                    `future->mutex` is locked
 */
inline static int
future_setState(
        Future* const future,
        const State   newState)
{
    future_assertLocked(future);

    future->state = newState;

    int status = pthread_cond_broadcast(&future->cond);

    if (status)
        log_add_errno(status, "Couldn't signal condition-variable");

    return status;
}

/**
 * Compares and sets the state of a future.
 *
 * @pre                     Future is unlocked
 * @param[in,out] future    Future
 * @param[in]     expect    Expected state of future
 * @param[in]     newState  New state for future if current state equals
 *                          expected state
 * @retval        `true`    Future was in expected state
 * @retval        `false`   Future was not in expected state
 * @post                    Future is unlocked
 * @threadsafety            Safe
 */
static bool
future_cas(    Future* const future,
        const State   expect,
        const State   newState)
{
    future_assertUnlocked(future);

    future_lock(future);
        const bool wasExpected = future->state == expect;

        if (wasExpected)
            future_setState(future, newState);
    future_unlock(future);

    return wasExpected;
}

/**
 * Initializes a future object.
 *
 * @param[out] future  Future to be initialized
 * @param[in]  obj     Object to be executed
 * @param[in]  run     Function to execute
 * @param[in]  halt    Function to stop execution
 * @param[in]  get     Function to get future's result or NULL for no result
 * @retval     0       Success
 * @retval     EAGAIN  System lacked necessary resources. `log_add()` called.
 * @retval     ENOMEM  Out of memory. `log_add()` called.
 */
static int
future_init(   Future* const future,
        void* const   obj,
        int         (*run)(void* obj),
        int         (*halt)(void* obj, pthread_t thread),
        int         (*get)(void* obj, void** result))
{
    int status;

    (void)memset(&future->thread, 0, sizeof(future->thread));

    future->state = STATE_INITIALIZED;
    future->wasCanceled = false;
    future->runFuncCalled = false;
    future->runFuncStatus = 0;
    future->task = task_new(obj, run, halt, get);

    if (future->task == NULL) {
        log_add("Couldn't create new task");
        status = ENOMEM;
    }
    else {
        status = mutex_init(&future->mutex, PTHREAD_MUTEX_ERRORCHECK, true);

        if (status) {
            task_free(future->task);
        }
        else {
            status = pthread_cond_init(&future->cond, NULL);

            if (status) {
                log_add_errno(status, "Couldn't initialize condition variable");
                (void)pthread_mutex_destroy(&future->mutex);
            }
        } // `future->mutex` initialized
    } // `future->task` created

    return status;
}

/**
 * Destroys a future.
 *
 * @pre                   Future is unlocked
 * @param[in,out] future  Future to destroy
 * @post                  Future is unlocked
 */
static void
future_destroy(Future* const future)
{
    future_assertUnlocked(future);

    future_lock(future);
        (void)pthread_cond_destroy(&future->cond);
        task_free(future->task);
    future_unlock(future);

    int status = pthread_mutex_destroy(&future->mutex);
    log_assert(status == 0);
}

/**
 * Waits until a future has completed -- either normally or by being canceled.
 *
 * @pre                        future is locked
 * @param[in] future           Future
 * @param[in] desired          Desired state
 * @retval    0                Success. Future is in desired state.
 * @retval    ENOTRECOVERABLE  State protected by mutex is not recoverable.
 *                             `log_add()` called.
 * @retval    EPERM            Current thread doesn't own `future->mutex`.
 *                             `log_add()` called.
 * @post                       future is locked
 */
static int
future_wait(Future* const future)
{
    future_assertLocked(future);

    int status;

    while (future->state != STATE_COMPLETED) {
        status = pthread_cond_wait(&future->cond, &future->mutex);

        if (status) {
            log_add_errno(status, "Couldn't wait on condition-variable");
            break;
        }
    }

    return status;
}

Future*
future_new(
        void*   obj,
        int   (*run)(void* obj),
        int   (*halt)(void* obj, pthread_t thread),
        int   (*get)(void* obj, void** result))
{
    Future* future = log_malloc(sizeof(Future), "future of asynchronous task");

    if (future) {
        int status = future_init(future, obj, run, halt, get);

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
    /*
     * This function and `future_run()` won't execute simultaneously on the same
     * thread unless the caller has contrived a task that frees its own future.
     */

    int status;

    if (future != NULL) {
        if (future_cas(future, STATE_RUNNING, STATE_RUNNING)) {
            log_add("Future is being executed");
            status = EINVAL;
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
future_setFree(
        Future* const future,
        void        (*free)(void* obj))
{
    future_lock(future);
        future->task->free = free;
    future_unlock(future);
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
        else if (future->state == STATE_RUNNING) {
            future_unlock(future);
            status = EINVAL;
        }
        else {
            // log_assert(future->state == STATE_INITIALIZED);

            future->thread = pthread_self();

            if (future->wasCanceled) {
                future_setState(future, STATE_COMPLETED);
                status = 0;
            }
            else {
                future->runFuncCalled = true;

                future_setState(future, STATE_RUNNING);
                future_unlock(future);
                    status = task_run(future->task);
                future_lock(future);
                    future->runFuncStatus = status;
                    status = future_setState(future, STATE_COMPLETED);

                    if (status)
                        log_add("Couldn't set state of future");
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
        else if (future->state == STATE_COMPLETED) {
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
    future_lock(future);
        int runStatus = future->wasCanceled
                ? ECANCELED
                : future->runFuncStatus;

        int getStatus = task_getResult(future->task, result);
    future_unlock(future);

    return runStatus ? runStatus : getStatus;
}

int
future_getResult(
        Future* const restrict future,
        void** const restrict  result)
{
    // NB: This function can be called before `future_run()` due to asynchrony
    // in thread creation

    int status;

    future_lock(future);
        switch (future->state) {
        case STATE_INITIALIZED:
        case STATE_RUNNING:
            status = future_wait(future);

            if (status) {
                future_unlock(future);
                log_add("Couldn't wait until future's task completed");
                break;
            }
            /* no break */
        case STATE_COMPLETED:
        default:
            future_unlock(future);

            status = future_getResultNoWait(future, result);
        }

    return status;
}

int
future_getAndFree(
        Future* const restrict future,
        void** const restrict  result)
{
    // NB: This function can be called before `future_run()` due to asynchrony
    // in thread creation

    int status = future_getResult(future, result);

    (void)future_free(future); // Future can't be executing

    return status;
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
