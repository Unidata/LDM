/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: priority_queue.h
 * @author: Steven R. Emmerson
 *
 * This file ...
 */

#ifndef MISC_PRIORITY_QUEUE_H_
#define MISC_PRIORITY_QUEUE_H_

#include <stdbool.h>

typedef struct priority_queue PriorityQueue;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Returns a new priority queue.
 *
 * @param[in] compare  Comparison function for ordering the elements in the
 *                     queue. Must return a value less than, equal to, or
 *                     greater than 0 as the first argument has a priority that
 *                     is less than, equal to, or greater than the second
 *                     argument's, respectively. NB: Higher priority means
 *                     closer to the head of the queue.
 * @retval    NULL     Out-of-memory.
 */
PriorityQueue*
pq_new(
        int (*const compare)(void*, void*));

/**
 * Indicates if a priority queue is empty.
 *
 * @param[in] pq     The priority queue.
 * @retval    true   The queue is empty.
 * @retval    false  The queue isn't empty.
 */
bool
pq_isEmpty(
        const PriorityQueue* const pq);

/**
 * Adds an element to a priority queue.
 *
 * @param[in] pq   The priority queue.
 * @param[in] elt  The element to be added.
 */
void
pq_add(
        PriorityQueue* const restrict pq,
        void* const                   elt);

/**
 * Returns -- but doesn't remove -- the head element of a priority queue.
 *
 * @param[in] pq    The priority queue.
 * @retval    NULL  The queue is empty.
 * @return          The head element in the queue.
 */
void*
pq_peek(
        const PriorityQueue* const pq);

/**
 * Removes and returns the head element of a priority queue.
 *
 * @param[in] pq    The priority queue.
 * @retval    NULL  The queue is empty.
 * @return          The head element of the queue.
 */
void*
pq_remove(
        PriorityQueue* const pq);

/**
 * Frees the resources of a priority queue.
 *
 * @param[in] pq  The priority queue.
 */
void
pq_free(
        PriorityQueue* const pq);


#ifdef __cplusplus
    }
#endif

#endif /* MISC_PRIORITY_QUEUE_H_ */
