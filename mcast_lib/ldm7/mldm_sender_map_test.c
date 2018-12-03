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

#include "log.h"
#include "mldm_sender_map.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <stdbool.h>

/**
 * Only called once.
 */
static int setup(void)
{
    int status = msm_init();

    if (status)
        log_flush_error();

    status = msm_init();

    if (status != LDM7_LOGIC) {
        log_flush_error();
    }
    else {
        status = 0;
        log_clear();
    }

    return status;
}

/**
 * Only called once.
 */
static int teardown(void)
{
    msm_destroy(true);
    log_flush_error();

    return 0;
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
    CU_ASSERT_EQUAL(msm_lock(true), 0);
    CU_ASSERT_EQUAL(msm_put(IDS|DDPLUS, 1, 38800, 48800), 0);
    CU_ASSERT_EQUAL(msm_put(PPS, 1, 38800, 48800), LDM7_DUP);
    CU_ASSERT_EQUAL(msm_put(NEXRAD3, 1, 38800, 48800), LDM7_DUP);
    log_clear();
    CU_ASSERT_EQUAL(msm_put(NEXRAD3, 2, 38801, 48801), 0);
    log_clear();
    CU_ASSERT_EQUAL(msm_unlock(), 0);
}

static void test_msm_get(void)
{
    pid_t          pid;
    unsigned short port;
    in_port_t      mldmSrvrPort;

    CU_ASSERT_EQUAL(msm_lock(false), 0);
    CU_ASSERT_EQUAL(msm_get(NIMAGE, &pid, &port, &mldmSrvrPort), LDM7_NOENT);
    CU_ASSERT_EQUAL(msm_get(IDS, &pid, &port, &mldmSrvrPort), 0);
    CU_ASSERT_EQUAL(pid, 1);
    CU_ASSERT_EQUAL(port, 38800);
    CU_ASSERT_EQUAL(msm_get(NEXRAD3, &pid, &port, &mldmSrvrPort), 0);
    CU_ASSERT_EQUAL(pid, 2);
    CU_ASSERT_EQUAL(port, 38801);
    log_clear();
    CU_ASSERT_EQUAL(msm_unlock(), 0);
}

static void test_msm_removePid(void)
{
    pid_t          pid;
    unsigned short port;
    in_port_t      mldmSrvrPort;

    CU_ASSERT_EQUAL(msm_lock(true), 0);
    CU_ASSERT_EQUAL(msm_remove(5), LDM7_NOENT);
    CU_ASSERT_EQUAL(msm_remove(1), 0);
    CU_ASSERT_EQUAL(msm_get(IDS, &pid, &port, &mldmSrvrPort), LDM7_NOENT);
    log_clear();
    CU_ASSERT_EQUAL(msm_unlock(), 0);
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
                if (       CU_ADD_TEST(testSuite, test_locking)
                        && CU_ADD_TEST(testSuite, test_msm_put)
                        && CU_ADD_TEST(testSuite, test_msm_get)
                        && CU_ADD_TEST(testSuite, test_msm_removePid)
                        ) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_suites_failed() +
                    CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }
    }

    log_fini();
    return exitCode;
}
