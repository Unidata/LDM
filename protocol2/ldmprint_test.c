/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: ldmprint_test.c
 * @author: Steven R. Emmerson
 *
 * This file ...
 */

#include "config.h"

#include "ldm.h"
#include "log.h"
#include "ldmprint.h"

#include <stddef.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

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

static void test_feedtype(
        const feedtypet   ft,
        const char* const expect)
{
    char   buf[128];
    size_t len = strlen(expect);
    int    status = ft_format(ft, NULL, 0);
    CU_ASSERT_EQUAL(status, len);

    status = ft_format(ft, buf, 0);
    CU_ASSERT_EQUAL(status, len);

    status = ft_format(ft, buf, 1);
    CU_ASSERT_EQUAL(status, len);
    CU_ASSERT_EQUAL(buf[0], 0);

    status = ft_format(ft, buf, 2);
    CU_ASSERT_EQUAL(status, len);
    CU_ASSERT_EQUAL(buf[0], expect[0]);
    CU_ASSERT_EQUAL(buf[1], 0);

    status = ft_format(ft, buf, sizeof(buf));
    CU_ASSERT_EQUAL(status, len);
    CU_ASSERT_STRING_EQUAL(buf, expect);
}

static void test_ft_format_ids_ddplus(void)
{
    test_feedtype(IDS|DDPLUS, "IDS|DDPLUS");
}

static void test_ft_format_any(void)
{
    test_feedtype(ANY, "ANY");
}

static void test_ft_format_none(void)
{
    test_feedtype(NONE, "NONE");
}

static void test_null_buffer(void)
{
    int status = ft_format(NONE, NULL, 1);
    CU_ASSERT_EQUAL(status, -1);
    status = ft_format(NONE, NULL, 0);
    CU_ASSERT_TRUE(status > 0);
}

static void test_ps_format(void)
{
    char       buf[128];
    prod_spec  ps;
    const char expect[] = "{HDS, \"foo\"}";
    const char nada[] = "(null)";

    ps.feedtype = HDS;
    ps.pattern = "foo";

    int status = ps_format(&ps, NULL, sizeof(buf));
    CU_ASSERT_EQUAL(status, -1);

    status = ps_format(NULL, buf, sizeof(buf));
    CU_ASSERT_EQUAL(status, strlen(nada));
    CU_ASSERT_STRING_EQUAL(buf, nada);

    status = ps_format(&ps, buf, sizeof(buf));
    CU_ASSERT_EQUAL(status, strlen(expect));
    CU_ASSERT_STRING_EQUAL(buf, expect);
}

static void test_ts_format(void)
{
    char        buf[128];
    const char* expect;
    timestampt  ts;

    int status = ts_format(&ts, NULL, sizeof(buf));
    CU_ASSERT_EQUAL(status, -1);

    ts = TS_NONE;
    expect = "TS_NONE";
    status = ts_format(&ts, buf, sizeof(buf));
    CU_ASSERT_EQUAL(status, strlen(expect));
    CU_ASSERT_STRING_EQUAL(buf, expect);

    ts = TS_ZERO;
    expect = "TS_ZERO";
    status = ts_format(&ts, buf, sizeof(buf));
    CU_ASSERT_EQUAL(status, strlen(expect));
    CU_ASSERT_STRING_EQUAL(buf, expect);

    ts = TS_ENDT;
    expect = "TS_ENDT";
    status = ts_format(&ts, buf, sizeof(buf));
    CU_ASSERT_EQUAL(status, strlen(expect));
    CU_ASSERT_STRING_EQUAL(buf, expect);

    ts.tv_sec = 123456789;
    ts.tv_usec = 123456;
    expect = "19731129213309.123";
    status = ts_format(&ts, buf, sizeof(buf));
    CU_ASSERT_EQUAL(status, strlen(expect));
    CU_ASSERT_STRING_EQUAL(buf, expect);
}

static void test_pc_format(void)
{
    char         buf[128];
    prod_spec    ps[2];
    prod_class_t pc;
    const char   expect[] =
            "TS_ZERO TS_ENDT {{HDS, \"foo\"},{IDS|DDPLUS, \"bar\"}}";
    const char   nada[] = "(null)";

    ps[0].feedtype = HDS;
    ps[0].pattern = "foo";
    ps[1].feedtype = IDS|DDPLUS;
    ps[1].pattern = "bar";

    pc.from = TS_ZERO;
    pc.to = TS_ENDT;
    pc.psa.psa_len = 2;
    pc.psa.psa_val = ps;

    int status = pc_format(&pc, NULL, sizeof(buf));
    CU_ASSERT_EQUAL(status, -1);

    status = pc_format(NULL, buf, sizeof(buf));
    CU_ASSERT_EQUAL(status, strlen(nada));
    CU_ASSERT_STRING_EQUAL(buf, nada);

    status = pc_format(&pc, buf, sizeof(buf));
    CU_ASSERT_EQUAL(status, strlen(expect));
    CU_ASSERT_STRING_EQUAL(buf, expect);
    //(void)printf("buf = \"%s\"\n", buf);
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
                if (CU_ADD_TEST(testSuite, test_null_buffer) &&
                        CU_ADD_TEST(testSuite, test_ft_format_none) &&
                        CU_ADD_TEST(testSuite, test_ft_format_any) &&
                        CU_ADD_TEST(testSuite, test_ft_format_ids_ddplus) &&
                        CU_ADD_TEST(testSuite, test_ps_format) &&
                        CU_ADD_TEST(testSuite, test_ts_format) &&
                        CU_ADD_TEST(testSuite, test_pc_format)) {
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
