/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: prod_index_map_test.c
 * @author: Steven R. Emmerson
 *
 * This file performs a unit-test of the `prod_index_map` module.
 */

#include "config.h"

#include "ldm.h"
#include "log.h"
#include "prod_index_map.h"

#include <libgen.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <unistd.h>

static const char* const pathname = "prod_index.map";
static const signaturet  signatures[] = {{1}, {2}, {3}, {4}};
static const feedtypet   FEEDTYPE = SPARE;

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

static void closeMap(void)
{
    CU_ASSERT_EQUAL_FATAL(pim_close(), 0);
}

static void deleteMap(void)
{
    CU_ASSERT_EQUAL_FATAL(pim_delete(NULL, FEEDTYPE), 0);
}

static void test_openForWriting_0(
        void)
{
    deleteMap();
    int status = pim_openForWriting(NULL, FEEDTYPE, 0);
    log_clear();
    CU_ASSERT_EQUAL_FATAL(status, LDM7_INVAL);
}

static void test_openForWriting_3(
        void)
{
    int status;

    deleteMap();
    status = pim_openForWriting(NULL, FEEDTYPE, 3);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);

    closeMap();
    deleteMap();
}

static void openForWriting(
        unsigned maxSigs)
{
    int status = pim_openForWriting(NULL, FEEDTYPE, maxSigs);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
}

static void openForReading(void)
{
    int status = pim_openForReading(NULL, FEEDTYPE);
    log_flush_error();
    CU_ASSERT_EQUAL_FATAL(status, 0);
}

static void openNew(
        unsigned maxSigs)
{
    deleteMap();
    openForWriting(maxSigs);
}

static void closeAndUnlink(void)
{
    closeMap();
    deleteMap();
}

static void exists(
        const FmtpProdIndex iProd,
        const int            iSig)
{
    signaturet sig;

    CU_ASSERT_EQUAL_FATAL(pim_get(iProd, &sig), 0);
    CU_ASSERT_NSTRING_EQUAL(sig, signatures[iSig], sizeof(signaturet));
}

static void doesNotExist(
        const FmtpProdIndex iProd)
{
    signaturet sig;

    CU_ASSERT_EQUAL(pim_get(iProd, &sig), LDM7_NOENT);
}

static void test_put(
        void)
{
    openNew(1);

    CU_ASSERT_EQUAL_FATAL(pim_put(0, &signatures[0]), 0);
    exists(0, 0);

    closeAndUnlink();
}

static void put4(void)
{
    for (int i = 0; i < 4; i++)
        CU_ASSERT_EQUAL_FATAL(pim_put(i, &signatures[i]), 0);
}

static void get4(void)
{
    doesNotExist(0);

    exists(1, 1);
    exists(2, 2);
    exists(3, 3);

    doesNotExist(4);
}

static void test_get(
        void)
{
    openNew(3);
    put4();
    get4();
    closeAndUnlink();
}

static void test_persistence(void)
{
    openNew(3);
    put4();
    closeMap();
    openForReading();
    get4();
    closeAndUnlink();
}

static void test_decrease(void)
{
    openNew(3);
    put4();
    closeMap();
    openForWriting(2);

    doesNotExist(1);
    exists(2, 2);
    exists(3, 3);
    doesNotExist(4);

    closeAndUnlink();
}

static void test_putNonSequential(void)
{
    openNew(3);
    put4();
    CU_ASSERT_EQUAL(pim_put(10, &signatures[0]), 0);
    doesNotExist(1);
    doesNotExist(2);
    doesNotExist(3);
    exists(10, 0);
    doesNotExist(11);
    closeAndUnlink();
}

static void test_getNextFileId(void)
{
    FmtpProdIndex iProd;

    openNew(3);
    CU_ASSERT_EQUAL(pim_getNextProdIndex(&iProd), 0);
    CU_ASSERT_EQUAL(iProd, 0);
    CU_ASSERT_EQUAL(pim_put(0, &signatures[0]), 0);
    CU_ASSERT_EQUAL(pim_getNextProdIndex(&iProd), 0);
    CU_ASSERT_EQUAL(iProd, 1);
    closeAndUnlink();

    openNew(3);
    put4();
    CU_ASSERT_EQUAL(pim_getNextProdIndex(&iProd), 0);
    CU_ASSERT_EQUAL(iProd, 4);
    closeAndUnlink();
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;
    const char* progname = basename((char*) argv[0]);

    if (log_init(progname)) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
        exitCode = 1;
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (    CU_ADD_TEST(testSuite, test_openForWriting_0) &&
                        CU_ADD_TEST(testSuite, test_openForWriting_3) &&
                        CU_ADD_TEST(testSuite, test_put) &&
                        CU_ADD_TEST(testSuite, test_get) &&
                        CU_ADD_TEST(testSuite, test_persistence) &&
                        CU_ADD_TEST(testSuite, test_decrease) &&
                        CU_ADD_TEST(testSuite, test_putNonSequential) &&
                        CU_ADD_TEST(testSuite, test_getNextFileId)
                        ) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_suites_failed() +
                    CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }

        log_free();
    }

    return exitCode;
}
