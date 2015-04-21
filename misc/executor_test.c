/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: executor_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the module for executing jobs asynchronously.
 */

#include "config.h"

#include "log.h"
#include "executor.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

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

static void test_exe_new(
        void)
{
    Executor* exe = exe_new();
    CU_ASSERT_PTR_NOT_NULL_FATAL(exe);
    exe_free(exe);
}

static void* start1(
        void* arg)
{
    CU_ASSERT_PTR_NULL_FATAL(arg);
    return NULL;
}

static void test_exe_submit(
        void)
{
    Executor* exe = exe_new();
    CU_ASSERT_PTR_NOT_NULL_FATAL(exe);

    const Job* job = NULL;
    int status = exe_submit(exe, start1, NULL, NULL, &job);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    exe_free(exe);
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;
    const char* progname = basename((char*) argv[0]);

    if (-1 == openulog(progname, 0, LOG_LOCAL0, "-")) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
        exitCode = 1;
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_exe_new) &&
                        CU_ADD_TEST(testSuite, test_exe_submit)) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }
    }

    return exitCode;
}
