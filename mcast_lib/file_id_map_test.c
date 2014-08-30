/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: file_id_map_test.c
 * @author: Steven R. Emmerson
 *
 * This file performs a unit-test of the `file_id_map` module.
 */


#include "config.h"

#include "ldm.h"
#include "log.h"
#include "file_id_map.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <unistd.h>

static const char* const pathname = "file_id.map";

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

static void test_fim_openForWriting_0(
        void)
{
    (void)unlink(pathname);
    int status = fim_openForWriting(pathname, 0);
    log_clear();
    CU_ASSERT_EQUAL_FATAL(status, LDM7_INVAL);
}

static void test_fim_openForWriting_3(
        void)
{
    signaturet signature1 = {1};

    int status = fim_openForWriting(pathname, 3);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    CU_ASSERT_EQUAL(fim_put(1, &signature1), 0);

    CU_ASSERT_EQUAL_FATAL(fim_close(), 0);
    (void)unlink(pathname);
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
                if (    CU_ADD_TEST(testSuite, test_fim_openForWriting_0) &&
                        CU_ADD_TEST(testSuite, test_fim_openForWriting_3)) {
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
