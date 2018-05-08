/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the `Task` module.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "log.h"
#include "Task.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

static void signal_handler(int sig)
{
    CU_ASSERT_EQUAL(sig, SIGTERM);
}

/**
 * Only called once.
 */
static int setup(void)
{
    struct sigaction sigact;
    int              status = sigemptyset(&sigact.sa_mask);

    if (status == 0) {
        sigact.sa_flags = 0;
        sigact.sa_handler = signal_handler;

        status = sigaction(SIGTERM, &sigact, NULL);
    }

    return status;
}

/**
 * Only called once.
 */
static int teardown(void)
{
    return 0;
}

static void*
retArg(void* arg)
{
    return arg;
}

static void*
doNotReturn(void* arg)
{
    pause();
    return NULL;
}

static void returnArg(void* arg)
{
    Task* task = task_create(retArg, arg, NULL);

    void* ptr;
    CU_ASSERT_EQUAL(task_destroy(task, &ptr), 0);
    CU_ASSERT_PTR_EQUAL(ptr, arg);
}

static void test_retNull(void)
{
    returnArg(NULL);
}

static void test_retval(void)
{
    int one = one;
    returnArg(&one);
}

static void test_kill(void)
{
    Task* task = task_create(NULL, doNotReturn, NULL);

    void* ptr;
    CU_ASSERT_EQUAL(task_destroy(task, &ptr), 0);
    CU_ASSERT_PTR_NULL(ptr);
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;
    const char* progname = basename((char*) argv[0]);

    if (log_init(progname)) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_retNull)
                    && CU_ADD_TEST(testSuite, test_retval)
                    && CU_ADD_TEST(testSuite, test_kill)
                        ) {
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
