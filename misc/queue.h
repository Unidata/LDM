/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: queue.h
 * @author: Steven R. Emmerson
 *
 * This file defines the API for a simple queue of pointers. The queue is
 * thread-compatible but not thread-safe.
 */

#ifndef QUEUE_H_
#define QUEUE_H_

#include <sys/types.h>

typedef struct queue Queue;

#ifdef __cplusplus
    extern "C" {
#endif

Queue* q_new(void);

int q_enqueue(
        Queue* const restrict q,
        void* const restrict  elt);

void* q_dequeue(
        Queue* const restrict q);

size_t q_size(
        Queue* const restrict q);

void q_free(
        Queue* const q);

#ifdef __cplusplus
    }
#endif

#endif /* QUEUE_H_ */
