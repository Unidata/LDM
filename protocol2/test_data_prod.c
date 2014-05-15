/*
 * Copyright 2013 University Corporation for Atmospheric Research. All rights
 * reserved. See file COPYRIGHT in the top-level source-directory for copying
 * and redistribution conditions.
 */
#include "config.h"

#ifndef _XOPEN_SOURCE
#   define _XOPEN_SOURCE 500
#endif

#include <libgen.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#include "data_prod.h"
#include "log.h"

/**
 * Only called once.
 */
static int setup(
        void)
{
    return 0;
}

/**
 * Only called once.
 */
static int teardown(
        void)
{
    return 0;
}

static void test_nil(
        void)
{
    const product* const    prod = dp_getNil();

    CU_ASSERT_PTR_NOT_NULL(prod);
    CU_ASSERT_TRUE(dp_isNil(prod));
    CU_ASSERT_TRUE(dp_equals(prod, prod));
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode;
    const char* progname = basename((char*) argv[0]);

    if (-1 == openulog(progname, 0, LOG_LOCAL0, "-")) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
        exitCode = 1;
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_nil)) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            CU_cleanup_registry();
        }

        exitCode = CU_get_error();
    }

    return exitCode;
}
