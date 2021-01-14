/**
 * This file defines a "stop" flag for asynchronous tasks.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: StopFlag.c
 *  Created on: May 2, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "log.h"
#include "Thread.h"
#include "StopFlag.h"

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

int stopFlag_init(
        StopFlag* const        stopFlag,
        bool                 (*done)(void))
{
    stopFlag->isSet = false;
    stopFlag->done = done;

    int status = mutex_init(&stopFlag->mutex, PTHREAD_MUTEX_ERRORCHECK, true);

    if (status == 0) {
        status = pthread_cond_init(&stopFlag->cond, NULL);

        if (status) {
            log_add_errno(status, "Couldn't initialize condition-variable");

            (void)pthread_mutex_destroy(&stopFlag->mutex);
        }
    } // `stopFlag->mutex` initialized

    return status;
}

void stopFlag_destroy(StopFlag* const stopFlag)
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

static bool isDone(StopFlag* const stopFlag)
{
    return stopFlag->isSet || (stopFlag->done && stopFlag->done());
}

bool stopFlag_shouldStop(StopFlag* const stopFlag)
{
    lock(stopFlag);
        bool shouldStop = isDone(stopFlag);
    unlock(stopFlag);
    return shouldStop;
}

void stopFlag_wait(StopFlag* const stopFlag)
{
    lock(stopFlag);
        while (!isDone(stopFlag)) {
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
        while (!isDone(stopFlag)) {
            int status = pthread_cond_timedwait(&stopFlag->cond,
                    &stopFlag->mutex, when);

            if (status == ETIMEDOUT)
                break;

            log_assert(status == 0);
        }
    unlock(stopFlag);
}
