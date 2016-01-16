/*
 * Copyright 2014 University Corporation for Atmospheric Research.
 *
 * See file COPYRIGHT in the top-level source-directory for copying and
 * redistribution conditions.
 */
#include "config.h"

#include "ldm.h"
#include "log.h"
#include "prod_index_queue.h"
#include "mcast.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static ProdIndexQueue* rq;

static int
setup(void)
{
    if (NULL == (rq = piq_new())) {
        (void)fprintf(stderr, "Couldn't create request-queue\n");
        return -1;
    }
    return 0;
}

static int
teardown(void)
{
    piq_free(rq);
    return 0;
}

static void
test_add_get(void)
{
    VcmtpProdIndex     fileA = 1;
    VcmtpProdIndex     fileB;
    int                status;

    status = piq_add(rq, fileA);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL(piq_count(rq), 1);

    status = piq_removeNoWait(rq, &fileB);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(fileB, fileA);
    CU_ASSERT_EQUAL(piq_count(rq), 0);
}

static void
test_order(void)
{
    VcmtpProdIndex fileA = 1;
    VcmtpProdIndex fileB = 2;
    VcmtpProdIndex fileC = 3;
    VcmtpProdIndex fileD;
    int            status;

    status = piq_add(rq, fileA);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL(piq_count(rq), 1);
    status = piq_add(rq, fileB);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL(piq_count(rq), 2);
    status = piq_add(rq, fileC);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL(piq_count(rq), 3);

    status = piq_removeNoWait(rq, &fileD);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(fileD, fileA);
    CU_ASSERT_EQUAL(piq_count(rq), 2);

    status = piq_removeNoWait(rq, &fileD);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(fileD, fileB);
    CU_ASSERT_EQUAL(piq_count(rq), 1);

    status = piq_removeNoWait(rq, &fileD);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(fileD, fileC);
    CU_ASSERT_EQUAL(piq_count(rq), 0);
}

int
main(
    const int           argc,
    const char* const*  argv)
{
    int         exitCode = EXIT_FAILURE;

    if (log_init(argv[0])) {
        (void)fprintf(stderr, "Couldn't initialize logging system\n");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite*       testSuite = CU_add_suite(__FILE__, setup,
                teardown);

            if (NULL != testSuite) {
                CU_ADD_TEST(testSuite, test_add_get);
                CU_ADD_TEST(testSuite, test_order);

                if (CU_basic_run_tests() == CUE_SUCCESS)
                    exitCode = CU_get_number_of_failures();
            }

            CU_cleanup_registry();
        } /* CUnit registery allocated */

        log_free();
    } /* logging system initialized */

    return exitCode;
}
