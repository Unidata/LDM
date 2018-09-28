/**
 * This file implements an integer that can be accessed atomically.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: AtomicInt.c
 *  Created on: May 2, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "log.h"
#include "Thread.h"
#include "AtomicInt.h"

#include <pthread.h>
#include <time.h>

struct atomicInt {
    pthread_mutex_t mutex;
    int             value;
};

AtomicInt*
atomicInt_new(const int initVal)
{
    AtomicInt* atomicInt = log_malloc(sizeof(AtomicInt), "atomic integer");

    if (atomicInt) {
        atomicInt->value = initVal;

        if (mutex_init(&atomicInt->mutex, PTHREAD_MUTEX_ERRORCHECK, true)) {
            log_add("Couldn't initialize atomic integer");
            free(atomicInt);
            atomicInt = NULL;
        }
    } // Atomic integer allocated

    return atomicInt;
}

void
atomicInt_free(AtomicInt* const atomicInt)
{
    if (atomicInt) {
        mutex_destroy(&atomicInt->mutex);
        free(atomicInt);
    }
}

int
atomicInt_set(
        AtomicInt* const atomicInt,
        const int        newVal)
{
    (void)mutex_lock(&atomicInt->mutex);
        const int oldVal = atomicInt->value;

        atomicInt->value = newVal;
    (void)mutex_unlock(&atomicInt->mutex);

    return oldVal;
}

int
atomicInt_get(AtomicInt* const atomicInt)
{
    (void)mutex_lock(&atomicInt->mutex);
        const int value = atomicInt->value;
    (void)mutex_unlock(&atomicInt->mutex);

    return value;
}

int
atomicInt_compareAndSet(
        AtomicInt* const atomicInt,
        const int        expectVal,
        const int        newVal)
{
    (void)mutex_lock(&atomicInt->mutex);
        const int oldVal = atomicInt->value;

        if (oldVal == expectVal)
            atomicInt->value = newVal;
    (void)mutex_unlock(&atomicInt->mutex);

    return oldVal;
}
