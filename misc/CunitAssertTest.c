/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the `Foo` module.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "log.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <pthread.h>
#include <stdbool.h>

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

static void subFunc(void)
{
    CU_ASSERT_TRUE(0);
}

static void test_assertInSubFunc(void)
{
    subFunc();
}

static void* run(void* arg)
{
    CU_ASSERT_TRUE(0);
}

static void test_assertInThread(void)
{
    pthread_t thread;
    int status = pthread_create(&thread, NULL, run, NULL);
    CU_ASSERT_EQUAL(status, 0);
    status = pthread_join(thread, NULL);
    CU_ASSERT_EQUAL(status, 0);
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
                if (CU_ADD_TEST(testSuite, test_assertInSubFunc) &&
                    CU_ADD_TEST(testSuite, test_assertInThread)
                        ) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }
    }

    return exitCode == 2 ? 0 : exitCode;
}
