/**
 * This file defines a model for Coverity Scan to eliminate false positives.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Coverity_model.c
 *  Created on: Jun 25, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

void exit(const int code)
{
    __coverity_panic__();
}

void abort(void)
{
    __coverity_panic__();
}

void* malloc(const size_t nbytes)
{
    __coverity_alloc__();
    return NULL; // Eclipse wants to see a return
}

void* calloc(
        const size_t nelem,
        const size_t eltsize)
{
    __coverity_alloc__();
    return NULL; // Eclipse wants to see a return
}

void free(void* const ptr)
{
    __coverity_free__();
}

int pthread_mutex_lock(pthread_mutex_t* const mutex)
{
    __coverity_exclusive_lock_acquire__(mutex);
    return 0; // Eclipse wants to see a return
}

int pthread_mutex_unlock(pthread_mutex_t* const mutex)
{
    __coverity_exclusive_lock_release__(mutex);
    return 0; // Eclipse wants to see a return
}

unsigned sleep(const unsigned seconds)
{
    __coverity_sleep__();
    return 0; // Eclipse wants to see a return
}

int usleep(const uint32_t usec)
{
    __coverity_sleep__();
    return 0; // Eclipse wants to see a return
}

int nanosleep(
        const struct timespec* const request,
        struct timespec* const       remain)
{
    __coverity_sleep__();
    return 0; // Eclipse wants to see a return
}
