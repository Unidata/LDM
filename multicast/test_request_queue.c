/*
 * Copyright 2014 University Corporation for Atmospheric Research.
 *
 * See file COPYRIGHT in the top-level source-directory for copying and
 * redistribution conditions.
 */
#include "config.h"

#include "request_queue.h"
#include "log.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

static RequestQueue* rq;

static int
setup(void)
{
    if (NULL == (rq = rq_new())) {
        (void)fprintf(stderr, "Couldn't create request-queue\n");
        return -1;
    }
    return 0;
}

static int
teardown(void)
{
    rq_free(rq);
    return 0;
}

static void
test_invalid_get(void)
{
    signaturet sig;
    int        status = rq_remove(NULL, &sig);

    CU_ASSERT_EQUAL_FATAL(status, EINVAL);

    status = rq_remove(rq, NULL);
    CU_ASSERT_EQUAL_FATAL(status, EINVAL);
}

static void
test_invalid_add(void)
{
    signaturet sig;
    int        status = rq_add(rq, NULL);

    CU_ASSERT_EQUAL_FATAL(status, EINVAL);

    status = rq_add(NULL, &sig);
    CU_ASSERT_EQUAL_FATAL(status, EINVAL);
}

static void
test_get_empty(void)
{
    signaturet sig;
    int        status = rq_remove(rq, &sig);

    CU_ASSERT_EQUAL_FATAL(status, ENOENT);
}

static void
test_add_get(void)
{
    signaturet sigA;
    signaturet sigB;
    int        status;

    (void)memset(sigA, 1, sizeof(signaturet));

    status = rq_add(rq, &sigA);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = rq_remove(rq, &sigB);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(memcmp(sigA, sigB, sizeof(signaturet)), 0);
}

static void
test_order(void)
{
    signaturet sigA;
    signaturet sigB;
    signaturet sigC;
    signaturet sigD;
    int        status;

    (void)memset(sigA, 1, sizeof(signaturet));
    (void)memset(sigB, 2, sizeof(signaturet));
    (void)memset(sigC, 3, sizeof(signaturet));

    status = rq_add(rq, &sigA);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = rq_add(rq, &sigB);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = rq_add(rq, &sigC);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = rq_remove(rq, &sigD);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(memcmp(sigA, sigD, sizeof(signaturet)), 0);

    status = rq_remove(rq, &sigD);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(memcmp(sigB, sigD, sizeof(signaturet)), 0);

    status = rq_remove(rq, &sigD);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(memcmp(sigC, sigD, sizeof(signaturet)), 0);
}

int
main(
    const int           argc,
    const char* const*  argv)
{
    int         exitCode = EXIT_FAILURE;

    if (-1 == openulog(basename(argv[0]), 0, LOG_LOCAL0, "-")) {
        (void)fprintf(stderr, "Couldn't initialize logging system\n");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite*       testSuite = CU_add_suite(__FILE__, setup,
                teardown);

            if (NULL != testSuite) {
                CU_ADD_TEST(testSuite, test_invalid_get);
                CU_ADD_TEST(testSuite, test_invalid_add);
                CU_ADD_TEST(testSuite, test_get_empty);
                CU_ADD_TEST(testSuite, test_add_get);
                CU_ADD_TEST(testSuite, test_get_empty);
                CU_ADD_TEST(testSuite, test_order);
                CU_ADD_TEST(testSuite, test_get_empty);

                if (CU_basic_run_tests() == CUE_SUCCESS)
                    exitCode = CU_get_number_of_failures();
            }

            CU_cleanup_registry();
        } /* CUnit registery allocated */
    } /* logging system initialized */

    return exitCode;
}
