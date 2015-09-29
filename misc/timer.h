/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: timer.h
 * @author: Steven R. Emmerson
 *
 * This file ...
 */

#ifndef MISC_TIMER_H_
#define MISC_TIMER_H_


#ifdef __cplusplus
    extern "C" {
#endif

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
        void* const restrict            arg);

#ifdef __cplusplus
    }
#endif

#endif /* MISC_TIMER_H_ */
