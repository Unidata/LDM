/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mutex.c
 * @author: Steven R. Emmerson
 *
 * This file implements the API for a mutual-exclusion lock.
 */

#include "config.h"
#include "mutex.h"

/**
 * Initializes a mutual-exclusion lock.
 *
 * @param[in,out] mutex        The mutual-exclusion lock.
 * @param[in]     recursive    Whether a thread that holds a lock on the mutex
 *                             can lock it again without error.
 * @param[in]     inheritable  Whether the thread that holds a lock on the mutex
 *                             should run at the priority of a higher-priority
 *                             thread that is attempting to acquire the mutex.
 * @retval        0            Success.
 * @retval        ENOMEM       Out-of-memory.
 */
int mutex_init(
        mutex_t* const mutex,
        const bool     recursive,
        const bool     inheritable)
{
    pthread_mutexattr_t mutexAttr;
    int status = pthread_mutexattr_init(&mutexAttr);
    if (status == 0) {
        if (recursive)
            (void)pthread_mutexattr_settype(&mutexAttr,
                    PTHREAD_MUTEX_RECURSIVE);
        if (inheritable)
            (void)pthread_mutexattr_setprotocol(&mutexAttr,
                    PTHREAD_PRIO_INHERIT);
        status = pthread_mutex_init(mutex, &mutexAttr);
        (void)pthread_mutexattr_destroy(&mutexAttr);
    }
    return status;
}
