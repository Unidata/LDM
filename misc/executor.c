/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: executor.c
 * @author: Steven R. Emmerson
 *
 * This file ...
 */


#include "config.h"

#include "executor.h"
#include "log.h"

#include <stdlib.h>

typedef enum {
    EXE_READY,
    EXE_SHUTDOWN
} ExeState;

struct executor {
    ExeState state;
};

Executor* exe_new(void)
{
    Executor* const exe = LOG_MALLOC(sizeof(Executor), "job executor");

    if (exe != NULL)
        exe->state = EXE_READY;

    return exe;
}

void exe_free(
        Executor* const exe)
{
    free(exe); // `NULL` safe
}
