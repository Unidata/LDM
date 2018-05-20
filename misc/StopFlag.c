/**
 * This file defines a "stop" flag.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: StopFlag.c
 *  Created on: May 2, 2018
 *      Author: Steven R. Emmerson
 */
#include "../../misc/StopFlag.h"

#include "config.h"

#include "log.h"

#include <pthread.h>
#include <time.h>

static void
lock(StopFlag* const stopFlag)
{
    int status = pthread_mutex_lock(&stopFlag->mutex);
    log_assert(status == 0);
}

static void
unlock(StopFlag* const stopFlag)
{
    int status = pthread_mutex_unlock(&stopFlag->mutex);
    log_assert(status == 0);
}

static void
condBroadcast(StopFlag* const stopFlag)
{
    int status = pthread_cond_broadcast(&stopFlag->cond);
    log_assert(status == 0);
}

/**
 * Initializes a mutex.
 * @param[out] mutex        Mutex to be initialized
 * @retval     0            Success
 * @retval     ENOMEM       Out-of-memory. `log_add()` called.
 * @retval     EAGAIN       Insufficient resources. `log_add()` called.
 */
static int
initMutex(pthread_mutex_t* const mutex)
{
    pthread_mutexattr_t mutexAttr;
    int                 status = pthread_mutexattr_init(&mutexAttr);

    if (status) {
        log_add_errno(status, "Couldn't initialize attributes of mutex");
    }
    else {
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_ERRORCHECK );

        status = pthread_mutex_init(mutex, &mutexAttr);

        if (status)
            log_add_errno(status, "Couldn't initialize mutex");

        (void)pthread_mutexattr_destroy(&mutexAttr);
    } // `mutexAttr` initialized

    return status;
}

int stopFlag_init(StopFlag* const stopFlag)
{
    stopFlag->isSet = false;

    int status = initMutex(&stopFlag->mutex);

    if (status == 0) {
        status = pthread_cond_init(&stopFlag->cond, NULL);

        if (status) {
            log_add_errno(status, "Couldn't initialize condition-variable");

            (void)pthread_mutex_destroy(&stopFlag->mutex);
        }
    } // `stopFlag->mutex` initialized

    return status;
}

void stopFlag_deinit(StopFlag* const stopFlag)
{
    int status;

    status = pthread_cond_destroy(&stopFlag->cond);
    if (status) {
        log_error_q("status=%d", status);
        log_assert(status == 0);
    }

    status = pthread_mutex_destroy(&stopFlag->mutex);
    if (status) {
        log_error_q("status=%d", status);
        log_assert(status == 0);
    }
}

void stopFlag_set(StopFlag* const stopFlag)
{
    lock(stopFlag);
        stopFlag->isSet = true;
        condBroadcast(stopFlag);
    unlock(stopFlag);
}

bool stopFlag_isSet(StopFlag* const stopFlag)
{
    lock(stopFlag);
        bool isSet = stopFlag->isSet;
    unlock(stopFlag);
    return isSet;
}

void stopFlag_wait(StopFlag* const stopFlag)
{
    lock(stopFlag);
        while (!stopFlag->isSet) {
            int status = pthread_cond_wait(&stopFlag->cond, &stopFlag->mutex);
            log_assert(status == 0);
        }
    unlock(stopFlag);
}

void stopFlag_timedWait(
        StopFlag* const restrict              stopFlag,
        const struct timespec* const restrict when)
{
    lock(stopFlag);
        while (!stopFlag->isSet) {
            int status = pthread_cond_timedwait(&stopFlag->cond,
                    &stopFlag->mutex, when);

            if (status == ETIMEDOUT) {
                status = 0;
                break;
            }

            log_assert(status == 0);
        }
    unlock(stopFlag);
}
