/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: lock.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for a mutual-exclusion lock.
 */

#ifndef MYLOG_LOCK_H_
#define MYLOG_LOCK_H_

#include <stdbool.h>
#include <pthread.h>

typedef pthread_mutex_t mutex_t;


#ifdef __cplusplus
    extern "C" {
#endif

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
        const bool     inheritable);

/**
 * Finalizes a mutual-exclusion lock.
 *
 * @param[in,out] mutex   The mutex to be destroyed.
 * @retval        0       Success.
 * @retval        EBUSY   The mutex is in use by another thread.
 * @retval        EINVAL  The mutex is invalid.
 */
static inline int mutex_fini(
        mutex_t* const mutex)
{
    return pthread_mutex_destroy(mutex);
}

/**
 * Locks a mutual-exclusion lock.
 *
 * @param[in,out] mutex    The mutex to be locked.
 * @retval        0        Success.
 * @retval        EINVAL   The mutex is invalid.
 * @retval        EAGAIN   The mutex could not be acquired because the maximum
 *                         number of recursive locks for mutex has been
 *                         exceeded.
 * @retval        EDEADLK  A deadlock condition was detected or the current
 *                         thread already owns the mutex.
 */
static inline int mutex_lock(
        mutex_t* const mutex)
{
    return pthread_mutex_lock(mutex);
}

/**
 * Unlocks a mutual-exclusion lock.
 *
 * @param[in,out] mutex    The mutex to be unlocked.
 * @retval        0        Success.
 * @retval        EINVAL   The mutex is invalid.
 * @retval        EPERM    The current thread does not own the mutex.
 */
static inline int mutex_unlock(
        mutex_t* const mutex)
{
    return pthread_mutex_unlock(mutex);
}


#ifdef __cplusplus
    }
#endif

#endif /* MYLOG_LOCK_H_ */
