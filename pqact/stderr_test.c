/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: stderr_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests some aspects of standard error.
 */

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <unistd.h>

int main(
        int   ac,
        char* av[])
{
    char buf;
    printf("read(STDERR_FILENO,&buf,1)=%d\n", read(STDERR_FILENO,&buf,1));
    return 0;
}
