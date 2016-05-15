/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: freopen_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests whether or not freopen() reuses its file descriptor
 */

#define _XOPEN_SOURCE 600

#undef  NDEBUG
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(
        const int          argc,
        const char* const* argv)
{
    int status;

#if 0
    status = close(STDIN_FILENO);
#else
    status = fclose(stdin);
#endif
    assert(status == 0);
    status = fcntl(STDIN_FILENO, F_GETFD);
    assert(status == -1);

#if 1
    status = close(STDOUT_FILENO);  // Causes assertion success
#else
    status = fclose(stdout);        // Causes assertion failure
#endif
    assert(status == 0);
    status = fcntl(STDOUT_FILENO, F_GETFD);
    assert(status == -1);

    FILE* stream = freopen("/dev/null", "w", stdout);
    assert(stream);
    status = printf("fileno(stdout)=%d\n", fileno(stdout));
    assert(fileno(stdout) != STDIN_FILENO);
    assert(fileno(stdout) == STDOUT_FILENO);

    return 0;
}
