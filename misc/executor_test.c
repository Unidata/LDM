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

#include <errno.h>
#include <unistd.h>

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

static void* sleep1(
        void* const arg)
{
    sleep(1);
    return arg;
}

static void* waitForCancel(
        void* const arg)
{
    pause();
    return arg;
}

static void test_exe_new(
        void)
{
    Executor* exe = exe_new();
    CU_ASSERT_PTR_NOT_NULL_FATAL(exe);
    exe_free(exe);
}

static void test_exe_submit(void)
{
    int sleep1Result;
    Executor* exe = exe_new();
    Job*      job;
    int       status = exe_submit(exe, sleep1, &sleep1Result, NULL, &job);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    Job*      completedJob;
    status = exe_getCompleted(exe, &completedJob);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_PTR_EQUAL(completedJob, job);
    CU_ASSERT_FALSE(job_wasCanceled(job));
    CU_ASSERT_EQUAL(job_status(job), 0);
    CU_ASSERT_PTR_EQUAL(job_result(job), &sleep1Result);
    status = job_free(job);
    CU_ASSERT_EQUAL(status, 0);
    status = exe_free(exe);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_exe_shutdown(void)
{
    Executor* exe = exe_new();
    Job*      job;
    int       status = exe_submit(exe, waitForCancel, NULL, NULL, &job);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    sleep(1);
    exe_shutdown(exe);
    Job*      completedJob;
    status = exe_getCompleted(exe, &completedJob);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_PTR_EQUAL(completedJob, job);
    CU_ASSERT_TRUE(job_wasCanceled(job));
    status = job_free(job);
    CU_ASSERT_EQUAL(status, 0);
    status = exe_free(exe);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_submit_while_shutdown(void)
{
    Executor* exe = exe_new();
    int       status = exe_shutdown(exe);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    Job*      job;
    status = exe_submit(exe, waitForCancel, NULL, NULL, &job);
    CU_ASSERT_EQUAL_FATAL(status, EINVAL);
    exe_free(exe);
}

static void test_exe_clear(void)
{
    Executor* exe = exe_new();
    int       status = exe_clear(exe);
    CU_ASSERT_EQUAL_FATAL(status, EINVAL);
    status = exe_shutdown(exe);
    status = exe_clear(exe);
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
                        CU_ADD_TEST(testSuite, test_exe_submit) &&
                        CU_ADD_TEST(testSuite, test_exe_shutdown) &&
                        CU_ADD_TEST(testSuite, test_submit_while_shutdown) &&
                        CU_ADD_TEST(testSuite, test_exe_clear)) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }
    }

    log_free();

    return exitCode;
}
