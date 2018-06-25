/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests unsigned arithmetic
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <limits.h>
#include <stdio.h>

static int setup()
{
    return 0;
}

static int teardown()
{
    return 0;
}

static void test_subtraction(void)
{
    unsigned u1 = 1;
    unsigned umax = UINT_MAX;
    unsigned udiff = u1 - umax;

    CU_ASSERT_EQUAL(udiff, 2);
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
                if (CU_ADD_TEST(testSuite, test_subtraction)
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
