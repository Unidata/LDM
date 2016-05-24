/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: timestamp_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the timestamp module.
 */

#include "config.h"

#include "timestamp.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

struct timeval duration;

/**
 * Only called once.
 */
static int setup(void)
{
    struct timeval earlier, later;
    earlier.tv_sec = 0;
    earlier.tv_usec = 999999;
    later.tv_sec = 1;
    later.tv_usec = 0;
    timeval_init_from_difference(&duration, &later, &earlier);
    return 0;
}

/**
 * Only called once.
 */
static int teardown(void)
{
    return 0;
}

static void test_timeval_init_from_difference(
        void)
{
    CU_ASSERT_EQUAL(duration.tv_sec, 0);
    CU_ASSERT_EQUAL(duration.tv_usec, 1);
}

static void test_timeval_format_duration(void)
{
    char buf[TIMEVAL_FORMAT_DURATION];
    timeval_format_duration(buf, &duration);
    CU_ASSERT_STRING_EQUAL(buf, "PT0.000001S");
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_timeval_init_from_difference) &&
                CU_ADD_TEST(testSuite, test_timeval_format_duration)) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        exitCode = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    return exitCode;
}
