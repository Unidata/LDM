/**
 * This file defines a flag that can be accessed atomically.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: AtomicFlag.c
 *  Created on: May 2, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "log.h"
#include "Thread.h"
#include "AtomicFlag.h"

#include <pthread.h>
#include <time.h>

struct atomicFlag {
    pthread_mutex_t mutex;
    bool            isSet;
};

AtomicFlag*
atomicFlag_new(void)
{
    AtomicFlag* flag = log_malloc(sizeof(AtomicFlag), "atomic flag");

    if (flag) {
        flag->isSet = false;

        if (mutex_init(&flag->mutex, PTHREAD_MUTEX_ERRORCHECK, true)) {
            log_add("Couldn't initialize atomic flag");
            free(flag);
            flag = NULL;
        }
    } // Atomic flag allocated

    return flag;
}

void
atomicFlag_free(AtomicFlag* const flag)
{
    if (flag) {
        mutex_destroy(&flag->mutex);
        free(flag);
    }
}

bool
atomicFlag_testAndSet(AtomicFlag* const flag)
{
    (void)mutex_lock(&flag->mutex);
        const bool isSet = flag->isSet;

        if (!isSet)
            flag->isSet = true;
    (void)mutex_unlock(&flag->mutex);

    return isSet;
}
