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

#include "mylog.h"
#include "executor.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

static pthread_cond_t           cond;
static pthread_mutex_t          mutex;
static volatile sig_atomic_t    done;

/**
 * Only called once.
 */
static int setup(void)
{
    int status = pthread_cond_init(&cond, NULL);
    mylog_assert(status == 0);
    status = pthread_mutex_init(&mutex, NULL);
    mylog_assert(status == 0);
    return 0;
}

/**
 * Only called once.
 */
static int teardown(void)
{
    (void)pthread_mutex_destroy(&mutex);
    (void)pthread_cond_destroy(&cond);
    return 0;
}

static void* sleep1(
        void* const arg)
{
    sleep(1);
    return arg;
}

static void* waitForStop(
        void* const arg)
{
    while (!done)
        pthread_cond_wait(&cond, &mutex);
    return arg;
}

static void stop(
        void* const arg)
{
    done = 1;
    int status = pthread_cond_signal(&cond);
    mylog_assert(status == 0);
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
    CU_ASSERT_EQUAL(exe_count(exe), 1);
    Job*      completedJob = exe_getCompleted(exe);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL(exe_count(exe), 0);
    CU_ASSERT_PTR_EQUAL(completedJob, job);
    CU_ASSERT_FALSE(job_wasStopped(job));
    CU_ASSERT_EQUAL(job_status(job), 0);
    CU_ASSERT_PTR_EQUAL(job_result(job), &sleep1Result);
    job_free(job);
    CU_ASSERT_EQUAL(status, 0);
    status = exe_free(exe);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_exe_shutdown_empty(void)
{
    Executor* exe = exe_new();
    CU_ASSERT_EQUAL(exe_count(exe), 0);
    int status = exe_shutdown(exe);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(exe_count(exe), 0);
    status = exe_free(exe);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_exe_shutdown(void)
{
    Executor* exe = exe_new();
    Job*      job;
    int       status = exe_submit(exe, waitForStop, NULL, stop, &job);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    sleep(1);
    status = exe_shutdown(exe);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(exe_count(exe), 1);
    Job*      completedJob = exe_getCompleted(exe);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL(exe_count(exe), 0);
    CU_ASSERT_PTR_EQUAL(completedJob, job);
    CU_ASSERT_TRUE(job_wasStopped(job));
    job_free(job);
    CU_ASSERT_EQUAL(status, 0);
    status = exe_free(exe);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_submit_while_shutdown(void)
{
    Executor* exe = exe_new();
    int       status = exe_shutdown(exe);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL(exe_count(exe), 0);
    Job*      job;
    status = exe_submit(exe, waitForStop, NULL, NULL, &job);
    CU_ASSERT_EQUAL_FATAL(status, EINVAL);
    CU_ASSERT_EQUAL(exe_count(exe), 0);
    exe_free(exe);
}

static void test_exe_clear(void)
{
    Executor* exe = exe_new();
    int       status = exe_clear(exe);
    CU_ASSERT_EQUAL_FATAL(status, EINVAL);
    (void)exe_shutdown(exe);
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

    (void)mylog_init(progname);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_exe_new) &&
                    CU_ADD_TEST(testSuite, test_exe_submit) &&
                    CU_ADD_TEST(testSuite, test_exe_shutdown_empty) &&
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

    mylog_free();

    return exitCode;
}
