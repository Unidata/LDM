/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file mldm_sender_map_test.c
 *
 * This file tests the `mldm_sender_map` module.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "mylog.h"
#include "mldm_sender_map.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
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

static void test_msm_init(void)
{
    int status = msm_init();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    mylog_flush_error();

    status = msm_init();
    CU_ASSERT_EQUAL(status, LDM7_INVAL);
    mylog_clear();
}

static void test_locking(void)
{
    CU_ASSERT_EQUAL(msm_lock(true), 0);
    CU_ASSERT_EQUAL(msm_unlock(), 0);
    CU_ASSERT_EQUAL(msm_lock(false), 0);
    CU_ASSERT_EQUAL(msm_unlock(), 0);
}

static void test_msm_put(void)
{
    CU_ASSERT_EQUAL(msm_put(IDS|DDPLUS, 1, 38800), 0);
    CU_ASSERT_EQUAL(msm_put(PPS, 1, 38800), LDM7_DUP);
    CU_ASSERT_EQUAL(msm_put(NEXRAD3, 1, 38800), LDM7_DUP);
    mylog_clear();
    CU_ASSERT_EQUAL(msm_put(NEXRAD3, 2, 38801), 0);
    mylog_clear();
}

static void test_msm_get(void)
{
    pid_t          pid;
    unsigned short port;

    CU_ASSERT_EQUAL(msm_get(NIMAGE, &pid, &port), LDM7_NOENT);
    CU_ASSERT_EQUAL(msm_get(IDS, &pid, &port), 0);
    CU_ASSERT_EQUAL(pid, 1);
    CU_ASSERT_EQUAL(port, 38800);
    CU_ASSERT_EQUAL(msm_get(NEXRAD3, &pid, &port), 0);
    CU_ASSERT_EQUAL(pid, 2);
    CU_ASSERT_EQUAL(port, 38801);
    mylog_clear();
}

static void test_msm_removePid(void)
{
    pid_t          pid;
    unsigned short port;

    CU_ASSERT_EQUAL(msm_remove(5), LDM7_NOENT);
    CU_ASSERT_EQUAL(msm_remove(1), 0);
    CU_ASSERT_EQUAL(msm_get(IDS, &pid, &port), LDM7_NOENT);
    mylog_clear();
}

static void test_msm_destroy(void)
{
    msm_destroy();
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;
    const char* progname = basename((char*) argv[0]);

    if (mylog_init(progname)) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_msm_init)
                        && CU_ADD_TEST(testSuite, test_locking)
                        && CU_ADD_TEST(testSuite, test_msm_put)
                        && CU_ADD_TEST(testSuite, test_msm_get)
                        && CU_ADD_TEST(testSuite, test_msm_removePid)
                        && CU_ADD_TEST(testSuite, test_msm_destroy)
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
