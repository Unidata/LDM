/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mylog_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the `mylog` module.
 */

#include "config.h"

#include "mylog.h"

#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

static char* progname;
static const char tmpPathname[] = "/tmp/mylog_test.log";

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

static int numLines(
        const char* const pathname)
{
    char cmd[_POSIX_MAX_INPUT];
    (void)snprintf(cmd, sizeof(cmd), "wc -l %s", pathname);
    FILE* pipe = popen(cmd, "r");
    int n = -1;
    (void)fscanf(pipe, "%d", &n);
    (void)fclose(pipe);
    return n;
}

static void logMessages(void)
{
    MYLOG_ERROR("%s(): Error message", __func__);
    MYLOG_WARNING("%s(): Warning", __func__);
    MYLOG_NOTICE("%s(): Notice", __func__);
    MYLOG_INFO("%s(): Informational message", __func__);
    MYLOG_DEBUG("%s(): Debug message", __func__);
}

static void vlogMessage(
        const mylog_level_t level,
        const char* const   format,
        ...)
{
    va_list args;
    va_start(args, format);
    MYLOG_VLOG(level, format, args);
    va_end(args);
}

static void vlogMessages(void)
{
    vlogMessage(MYLOG_LEVEL_ERROR, "%s(): %s", "Error message", __func__);
    vlogMessage(MYLOG_LEVEL_WARNING, "%s(): %s", "Warning", __func__);
    vlogMessage(MYLOG_LEVEL_NOTICE, "%s(): %s", "Notice", __func__);
    vlogMessage(MYLOG_LEVEL_INFO, "%s(): %s", "Informational message", __func__);
    vlogMessage(MYLOG_LEVEL_DEBUG, "%s(): %s", "Debug message", __func__);
}

static void make_expected_id(
        char* const restrict       id,
        const size_t               size,
        const char* const restrict name,
        const bool                 is_feeder)
{
    int status;
#if WANT_LOG4C
    status = snprintf(id, size, "%s.%s.%s", progname,
                    is_feeder ? "feeder" : "notifier", name);
#else
    char tmp_name[_POSIX_HOST_NAME_MAX+1];
    (void)strncpy(tmp_name, name, sizeof(tmp_name));
    for (char* cp = strchr(tmp_name, '.'); cp != NULL; cp = strchr(cp, '.'))
        *cp = '_';
    status = snprintf(id, size, "%s(%s)", name, is_feeder ? "feed" : "noti");
#endif
    CU_ASSERT_TRUE(status > 0);
    CU_ASSERT_TRUE(status < size);
}

static void test_init_fini(void)
{
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_open_file(void)
{
    (void)unlink(tmpPathname);
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = mylog_set_output(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);

    logMessages();

    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);

    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_open_stderr(void)
{
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = mylog_set_output("-");
    CU_ASSERT_EQUAL(status, 0);
    const char* actual = mylog_get_output();
    CU_ASSERT_PTR_NOT_NULL(actual);
    CU_ASSERT_STRING_EQUAL(actual, "-");

    logMessages();

    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_open_default(void)
{
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    const char* actual = mylog_get_output();
    CU_ASSERT_PTR_NOT_NULL(actual);
    CU_ASSERT_STRING_EQUAL(actual, "");
    MYLOG_ERROR("test_mylog_open_default() implicit");

    status = mylog_set_output("");
    actual = mylog_get_output();
    CU_ASSERT_PTR_NOT_NULL(actual);
    CU_ASSERT_STRING_EQUAL(actual, "");
    MYLOG_ERROR("test_mylog_open_default() explicit");

    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_levels(void)
{
    int status;
    mylog_level_t mylogLevels[] = {MYLOG_LEVEL_ERROR, MYLOG_LEVEL_WARNING,
            MYLOG_LEVEL_NOTICE, MYLOG_LEVEL_INFO, MYLOG_LEVEL_DEBUG};
    for (int nlines = 1; nlines <= MYLOG_LEVEL_COUNT; nlines++) {
        status = mylog_init(progname);
        CU_ASSERT_EQUAL_FATAL(status, 0);

        (void)unlink(tmpPathname);
        status = mylog_set_output(tmpPathname);
        CU_ASSERT_EQUAL(status, 0);

        mylog_set_level(mylogLevels[nlines-1]);
        logMessages();

        status = mylog_fini();
        CU_ASSERT_EQUAL(status, 0);

        int n = numLines(tmpPathname);
        CU_ASSERT_EQUAL(n, nlines);

        //if (n != nlines)
            //exit(1);
    }
    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_get_level(void)
{
    mylog_level_t mylogLevels[] = {MYLOG_LEVEL_ERROR, MYLOG_LEVEL_WARNING,
            MYLOG_LEVEL_NOTICE, MYLOG_LEVEL_INFO, MYLOG_LEVEL_DEBUG};
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    mylog_level_t level = mylog_get_level();
    CU_ASSERT_EQUAL(level, MYLOG_LEVEL_DEBUG);

    for (int i = 0; i < MYLOG_LEVEL_COUNT; i++) {
        mylog_level_t expected = mylogLevels[i];
        (void)mylog_set_level(expected);
        mylog_level_t actual = mylog_get_level();
        CU_ASSERT_EQUAL(actual, expected);
    }

    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_modify_id(void)
{
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    char expected[256];
    make_expected_id(expected, sizeof(expected), "foo", true);
    (void)mylog_modify_level("foo", true);
    const char* actual = mylog_get_id();
    CU_ASSERT_STRING_EQUAL(actual, expected);

    make_expected_id(expected, sizeof(expected), "bar", false);
    (void)mylog_modify_level("bar", false);
    actual = mylog_get_id();
    CU_ASSERT_STRING_EQUAL(actual, expected);

#if WANT_LOG4C
    make_expected_id(expected, sizeof(expected), "128_117_140_56", false);
#else
    make_expected_id(expected, sizeof(expected), "128.117.140.56", false);
#endif
    (void)mylog_modify_level("128.117.140.56", false);
    actual = mylog_get_id();
    CU_ASSERT_STRING_EQUAL(actual, expected);

    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_roll_level(void)
{
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    mylog_set_level(MYLOG_LEVEL_ERROR);
    mylog_level_t level;

    mylog_roll_level();
    level = mylog_get_level();
    CU_ASSERT_EQUAL(level, MYLOG_LEVEL_WARNING);

    mylog_roll_level();
    level = mylog_get_level();
    CU_ASSERT_EQUAL(level, MYLOG_LEVEL_NOTICE);

    mylog_roll_level();
    level = mylog_get_level();
    CU_ASSERT_EQUAL(level, MYLOG_LEVEL_INFO);

    mylog_roll_level();
    level = mylog_get_level();
    CU_ASSERT_EQUAL(level, MYLOG_LEVEL_DEBUG);

    mylog_roll_level();
    level = mylog_get_level();
    CU_ASSERT_EQUAL(level, MYLOG_LEVEL_ERROR);

    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_vlog(void)
{
    (void)unlink(tmpPathname);
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = mylog_set_output(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);

    vlogMessages();

    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);

    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 5);

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_set_output(void)
{
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    static const char* outputs[] = {"", "-", tmpPathname};
    for (int i = 0; i < sizeof(outputs)/sizeof(outputs[0]); i++) {
        const char* expected = outputs[i];
        (void)mylog_set_output(expected);
        const char* actual = mylog_get_output();
        CU_ASSERT_PTR_NOT_NULL(actual);
        CU_ASSERT_STRING_EQUAL(actual, expected);
    }

    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);
}

static void test_mylog_add(void)
{
    (void)unlink(tmpPathname);
    int status = mylog_init(progname);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = mylog_set_output(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);

    MYLOG_ADD("%s(): LOG_ADD message 1", __func__);
    MYLOG_ADD("%s(): LOG_ADD message 2", __func__);
    MYLOG_ERROR("%s(): LOG_ERROR message", __func__);

    status = mylog_fini();
    CU_ASSERT_EQUAL(status, 0);

    int n = numLines(tmpPathname);
    CU_ASSERT_EQUAL(n, 3);

    status = unlink(tmpPathname);
    CU_ASSERT_EQUAL(status, 0);
}

int main(
        const int    argc,
        char* const* argv)
{
    int exitCode = 1;
    progname = basename(argv[0]);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (       CU_ADD_TEST(testSuite, test_init_fini)
                    && CU_ADD_TEST(testSuite, test_init_fini)
                    && CU_ADD_TEST(testSuite, test_mylog_get_level)
                    && CU_ADD_TEST(testSuite, test_mylog_roll_level)
                    && CU_ADD_TEST(testSuite, test_mylog_modify_id)
                    && CU_ADD_TEST(testSuite, test_mylog_set_output)
                    && CU_ADD_TEST(testSuite, test_mylog_open_stderr)
                    && CU_ADD_TEST(testSuite, test_mylog_open_file)
                    && CU_ADD_TEST(testSuite, test_mylog_open_default)
                    && CU_ADD_TEST(testSuite, test_mylog_levels)
                    && CU_ADD_TEST(testSuite, test_mylog_vlog)
                    && CU_ADD_TEST(testSuite, test_mylog_add)
                    /*
                    */) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        exitCode = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    return exitCode;
}
