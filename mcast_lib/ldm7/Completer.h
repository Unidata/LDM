/**
 * This file defines an object that decorates an Executor with a queue of
 * completed asynchronous tasks.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Completer.h
 *  Created on: May 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "Executor.h"

#ifndef MCAST_LIB_LDM7_COMPLETER_H_
#define MCAST_LIB_LDM7_COMPLETER_H_

typedef struct completer Completer;

#ifdef __cplusplus
    extern "C" {
#endif

Completer*
completer_new(Executor* const exec);

void
completer_delete(Completer* const comp);

Future*
completer_submit(
        Completer* const comp,
        void* const      obj,
        void*          (*runFunc)(void* obj),
        void           (*haltFunc)(void* obj, pthread_t thread));

/**
 * Returns the future of the oldest completed task.
 *
 * @param[in] comp    Completer
 * @retval    `NULL`  No more tasks to return
 * @return            Future of oldest completed task
 */
Future*
completer_pop(Completer* const comp);

int
completer_shutdown(Completer* const comp);

#ifdef __cplusplus
    }
#endif

#endif /* MCAST_LIB_LDM7_COMPLETER_H_ */
