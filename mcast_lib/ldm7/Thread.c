/**
 * This file defines stuff in support of multi-threaded programming.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Thread.c
 *  Created on: May 7, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "log.h"
#include "Thread.h"

int
mutex_init(
        pthread_mutex_t* const mutex,
        const int              type,
        const bool             inherit)
{
    pthread_mutexattr_t mutexAttr;
    int                 status = pthread_mutexattr_init(&mutexAttr);

    if (status) {
        log_add_errno(status, "Couldn't initialize mutex attributes");
    }
    else {
        status = pthread_mutexattr_settype(&mutexAttr, type);

        if (status) {
            log_add_errno(status, "Couldn't set mutex type to %d", type);
        }
        else {
            if (inherit)
                (void)pthread_mutexattr_setprotocol(&mutexAttr,
                        PTHREAD_PRIO_INHERIT);

            status = pthread_mutex_init(mutex, &mutexAttr);

            if (status)
                log_add_errno(status, "Couldn't initialize mutex");
        }

        (void)pthread_mutexattr_destroy(&mutexAttr);
    } // `mutexAttr` initialized

    return status;
}
