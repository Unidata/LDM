/**
 * This file defines stuff in support of multi-threaded programming.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Thread.h
 *  Created on: May 7, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include <pthread.h>
#include <stdbool.h>

#ifndef MCAST_LIB_LDM7_THREAD_H_
#define MCAST_LIB_LDM7_THREAD_H_


#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Initializes a mutex.
 *
 * @param[out] mutex        Mutex to be initialized
 * @param[in]  type         One of PTHREAD_MUTEX_NORMAL,
 *                          PTHREAD_MUTEX_ERRORCHECK, PTHREAD_MUTEX_RECURSIVE,
 *                          or PTHREAD_MUTEX_DEFAULT
 * @param[in]  inherit      Whether or not to inherit priority
 * @retval     0            Success
 * @retval     EAGAIN       Out of resources. `log_add()` called.
 * @retval     ENOMEM       Out of memory. `log_add()` called.
 * @threadsafety            Safe
 */
int
mutex_init(
        pthread_mutex_t* const mutex,
        const int              type,
        const bool             inherit);

/**
 * Deinitializes a mutex. Calls `log_assert()` to assert success.
 *
 * @param[in,out] mutex  Mutex.
 */
int
mutex_destroy(pthread_mutex_t* const mutex);

/**
 * Locks a mutex. Calls `log_assert()` to assert success.
 *
 * @param[in,out] mutex  Mutex
 * @asyncsignalsafety    Unsafe
 */
int
mutex_lock(pthread_mutex_t* const mutex);

/**
 * Unlocks a mutex. Calls `log_assert()` to assert success.
 *
 * @param[in,out] mutex  Mutex
 */
int
mutex_unlock(pthread_mutex_t* const mutex);

#ifdef __cplusplus
    }
#endif

#endif /* MCAST_LIB_LDM7_THREAD_H_ */
