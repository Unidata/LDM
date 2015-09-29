/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: timer.c
 * @author: Steven R. Emmerson
 *
 * This file defines a singleton, thread-safe timer module in which callers can
 * register functions to be called at specific times in the future with specific
 * arguments on detached threads.
 */

#include "timer.h"
#include "priority_queue.h"

#include <pthread.h>
#include <stdlib.h>
#include <time.h>

static PriorityQueue*      pq;
static pthread_cond_t      cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t     mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t      once_control = PTHREAD_ONCE_INIT;

typedef struct {
    struct timespec when;
    void          (*func)(void*);
    void*           arg;
} Tuple;

/**
 * Returns a new tuple.
 *
 * @param[in] when  The time when the function should be called.
 * @param[in] func  The function to be called.
 * @param[in] arg   The optional argument to pass to the function or NULL.
 * @retval    NULL  Out-of-memory.
 * @return          The new tuple.
 */
static Tuple*
tuple_new(
        struct timespec* const restrict when,
        void           (*const restrict func)(void*),
        void* const restrict            arg)
{
    Tuple* tuple = malloc(sizeof(Tuple));
    tuple->when = when;
    tuple->func = func;
    tuple->arg = arg;
    return tuple;
}

/**
 * Frees the resources of a tuple.
 *
 * @param[in] tuple  The tuple to be freed.
 */
static void
tuple_free(
        Tuple* const tuple)
{
    free(tuple);
}

/**
 * Compares two tuples. Returns a value less than, equal to, or greater than 0
 * as the first tuple argument has a callback time that is later than, equal
 * to, or earlier than that of the second tuple argument, respectively.
 *
 * @param[in] a1  The first tuple argument.
 * @param[in] a2  The second tuple argument.
 * @retval    -1  The first argument has a callback time that is later than
 *                the second's.
 * @retval    0   The first argument has a callback time that is equal to the
 *                second's.
 * @retval    1   The first argument has a callback time that is earlier than
 *                the second's.
 */
static int
tuple_compare(
        void* const restrict a1,
        void* const restrict a2)
{
    const struct timespec* const t1 = ((Tuple*)a1)->when;
    const struct timespec* const t2 = ((Tuple*)a2)->when;
    double diff = ((double)t1->tv_sec - (double)t2->tv_sec) +
            ((double)t1->tv_nsec - (double)t2->tv_nsec);
    return diff < 0
            ? 1
            : diff > 0
              ? -1
              : a1 < a2
                ? 1
                : a1 > a2
                  ? -1
                  : 0;
}

/**
 * Runs the timer thread.
 *
 * @param[in] arg  Ignored.
 */
static void
timer(
        void* const arg)
{
    for (;;) {
        pthread_mutex_lock(&mutex);
        while (pq_isEmpty(pq))
            pthread_cond_wait(&cond, &mutex);
        Tuple* tuple = pq_peek(pq);
        int status = pthread_cond_timedwait(&cond, &mutex, &tuple->when);
        if (status) {
            // Something added to queue
            pthread_mutex_unlock(&mutex);
        }
        else {
            // Timed-out
            (void)pq_remove(pq);
            pthread_mutex_unlock(&mutex);
            pthread_t thread;
            pthread_create(&thread, NULL, tuple->func, tuple->arg);
            /*
             * The thread is detached to prevent this module from preventing
             * process termination and because the caller can't call
             * `pthread_join(thread)`.
             */
            pthread_detach(thread);
            tuple_free(tuple);
        }
    }
}

/**
 * Frees the priority queue.
 */
static void
freePriorityQueue(void)
{
    pq_free(pq);
}

/**
 * Initializes the timer.
 */
static void
timer_init(void)
{
    pq = pq_new(tuple_compare);
    atexit(freePriorityQueue); // To silence valgrind(1)
    pthread_t thread;
    pthread_create(&thread, NULL, timer, NULL);
    pthread_detach(thread); // So this module won't prevent process termination
}

/**
 * Adds a function to be called with an argument at a particular time. The
 * function will be called on a detached thread.
 *
 * @param[in] when  When the function should be called.
 * @param[in] func  The function to be called.
 * @param[in] arg   The argument to be passed to the function.
 */
void
timer_add(
        struct timespec* const restrict when,
        void           (*const restrict func)(void*),
        void* const restrict            arg)
{
    pthread_once(&once_control, timer_init);
    Tuple* tuple = tuple_new(when, func, arg);
    pthread_mutex_lock(&mutex);
    pq_add(tuple);
    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}
