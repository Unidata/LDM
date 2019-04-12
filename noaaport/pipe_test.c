/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: pipe_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests POSIX pipes. It reveals that pipes buffer.
 */

#include "config.h"

#include "log.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <pthread.h>

/**
 * Only called once.
 */
static int setup(void)
{
    return 0;
}

/**
 * Only called once.
 */
static int teardown(void)
{
    return 0;
}

static char buf[8192];

static void* read_from_pipe(void* arg)
{
    int fd = *(int*)arg;

    for (;;) {
        int nbytes = read(fd, buf, sizeof(buf));
        if (nbytes < 0)
            break;
        (void)printf("Read %d bytes\n", nbytes);
    }
    return NULL;
}

static void test_pipe(
        void)
{
    int fds[2];
    int status = pipe(fds);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    pthread_t thread;
    status = pthread_create(&thread, NULL, read_from_pipe, fds);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = pthread_detach(thread);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    for (int n = 1; n <= sizeof(buf); n <<= 1) {
        (void)printf("Writing %d bytes\n", n);
        status = write(fds[1], buf, n);
        CU_ASSERT_EQUAL_FATAL(status, n);
    }
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;

    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        exitCode = EXIT_FAILURE;
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_pipe)) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }

        log_fini();
    }
    return exitCode;
}
