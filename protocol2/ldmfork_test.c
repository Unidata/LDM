/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: ldmfork_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the `ldmfork` module.
 */

#include "config.h"

#include "ldmfork.h"
#include "log.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <fcntl.h>
#include <unistd.h>

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

static void test_open_on_dev_null_if_closed(
        void)
{
    int status = close(STDERR_FILENO);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = open_on_dev_null_if_closed(STDERR_FILENO, O_RDWR);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_FALSE_FATAL(log_is_stderr_useful());
    CU_ASSERT_TRUE_FATAL(fcntl(STDERR_FILENO, F_GETFD) >= 0);
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;
    log_init(argv[0]);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_open_on_dev_null_if_closed)) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        exitCode = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    log_fini();
    return exitCode;
}
