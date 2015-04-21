/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: executor.h
 * @author: Steven R. Emmerson
 *
 * This file ...
 */

#ifndef EXECUTOR_H_
#define EXECUTOR_H_

#include <stdbool.h>

#ifdef __cplusplus
    extern "C" {
#endif

typedef struct job Job;

void job_free(
        Job* const job);

bool job_wasCanceled(
        const Job* const job);

void* job_getResult(
        const Job* const job);

typedef struct executor Executor;

Executor* exe_new(void);

int exe_submit(
        Executor* const restrict   exe,
        void*              (*const start)(void*),
        void* restrict             arg,
        void               (*const stop)(void*),
        const Job** const restrict job);

int exe_getCompleted(
        Executor* const restrict exe,
        Job** const restrict     job);

int exe_shutdown(
        Executor* const restrict exe);

int exe_restart(
        Executor* const restrict exe);

void exe_free(
        Executor* const exe);

#ifdef __cplusplus
    }
#endif

#endif /* EXECUTOR_H_ */
