/*
 * Copyright 2011 University Corporation for Atmospheric Research.
 *
 * See file COPYRIGHT in the top-level source-directory for copying and
 * redistribution conditions.
 */
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <syslog.h>
#include <sys/types.h>

#include "child_map.h"
#include "log.h"

static ChildMap*                childMap = NULL;
static pid_t                    pidCounter = 1;
static pid_t                    PID;
static const char* const        COMMAND = "foo bar";
static const char* const        ARGV[] = {"foo", "bar", NULL};

static int
setup(void)
{
    return 0;
}


static int
teardown(void)
{
    return 0;
}

static void
test_no_entry(void)
{
    CU_ASSERT_EQUAL(cm_get_command(childMap, pidCounter), NULL);
}

static void
test_add_string_null_map(void)
{
    int status = cm_add_string(NULL, pidCounter, COMMAND);

    CU_ASSERT_EQUAL(status, 1);
    log_clear();
}

static void
test_add_string_null_command(void)
{
    int status = cm_add_string(childMap, pidCounter, NULL);

    CU_ASSERT_EQUAL(status, 1);
    log_clear();
}

static void
test_add_string(void)
{
    unsigned    count = cm_count(childMap);
    int         status = cm_add_string(childMap, pidCounter, COMMAND);

    PID = pidCounter++;

    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(cm_count(childMap), count+1);
    log_clear();
}

static void
test_count_null_map(void)
{
    CU_ASSERT_EQUAL(cm_count(NULL), 0);
    log_clear();
}

static void
test_contains_null_map(void)
{
    int status = cm_contains(NULL, PID);

    CU_ASSERT_EQUAL(status, 0);
    log_clear();
}

static void
test_contains_no_entry(void)
{
    unsigned    count = cm_count(childMap);
    int         status = cm_contains(childMap, pidCounter);

    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(cm_count(childMap), count);
    log_clear();
}

static void
test_contains(void)
{
    unsigned    count = cm_count(childMap);
    int         status = cm_contains(childMap, PID);

    CU_ASSERT_EQUAL(status, 1);
    CU_ASSERT_EQUAL(cm_count(childMap), count);
    log_clear();
}

static void
test_get_command_null_map(void)
{
    const char* command = cm_get_command(NULL, PID);

    CU_ASSERT_EQUAL(command, NULL);
    log_clear();
}

static void
test_get_command_no_entry(void)
{
    const char* command = cm_get_command(childMap, pidCounter);

    CU_ASSERT_EQUAL(command, NULL);
    log_clear();
}

static void
test_get_command(void)
{
    const char* command = cm_get_command(childMap, PID);

    CU_ASSERT_EQUAL(strcmp(command, COMMAND), 0);
    log_clear();
}

static void
test_remove_null_map(void)
{
    int status = cm_remove(NULL, PID);

    CU_ASSERT_EQUAL(status, 1);
    log_clear();
}

static void
test_remove_no_entry(void)
{
    unsigned    count = cm_count(childMap);
    int         status = cm_remove(childMap, pidCounter);

    CU_ASSERT_EQUAL(status, 2);
    CU_ASSERT_EQUAL(cm_count(childMap), count);
    log_clear();
}

static void
test_remove(void)
{
    unsigned    count = cm_count(childMap);
    int         status = cm_remove(childMap, PID);

    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(cm_count(childMap), count-1);
    log_clear();
}

static void
test_add_argv_null_map(void)
{
    int status = cm_add_argv(NULL, pidCounter, ARGV);

    CU_ASSERT_EQUAL(status, 1);
    log_clear();
}

static void
test_add_argv_null_command(void)
{
    int status = cm_add_argv(childMap, pidCounter, NULL);

    CU_ASSERT_EQUAL(status, 1);
    log_clear();
}

static void
test_add_argv(void)
{
    unsigned    count = cm_count(childMap);
    int         status = cm_add_argv(childMap, pidCounter, ARGV);
    const char* cmd;

    PID = pidCounter++;

    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(cm_count(childMap), count+1);
    cmd = cm_get_command(childMap, PID);
    CU_ASSERT_EQUAL(strcmp(cmd, COMMAND), 0);
    log_clear();
}


int
main(
    const int           argc,
    const char* const*  argv)
{
    int         exitCode = EXIT_FAILURE;

    if (NULL == (childMap = cm_new())) {
        (void)fprintf(stderr, "Couldn't create child-map");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite*       testSuite = CU_add_suite(__FILE__, setup,
                teardown);

            if (NULL != testSuite) {
                const char*       progname = strrchr(argv[0], '/');

                progname = (NULL == progname)
                    ? argv[0]
                    : progname + 1;

                CU_ADD_TEST(testSuite, test_no_entry);
                CU_ADD_TEST(testSuite, test_add_string_null_map);
                CU_ADD_TEST(testSuite, test_add_string_null_command);
                CU_ADD_TEST(testSuite, test_add_string);
                CU_ADD_TEST(testSuite, test_count_null_map);
                CU_ADD_TEST(testSuite, test_contains_null_map);
                CU_ADD_TEST(testSuite, test_contains_no_entry);
                CU_ADD_TEST(testSuite, test_contains);
                CU_ADD_TEST(testSuite, test_get_command_null_map);
                CU_ADD_TEST(testSuite, test_get_command_no_entry);
                CU_ADD_TEST(testSuite, test_get_command);
                CU_ADD_TEST(testSuite, test_remove_null_map);
                CU_ADD_TEST(testSuite, test_remove_no_entry);
                CU_ADD_TEST(testSuite, test_remove);
                CU_ADD_TEST(testSuite, test_add_argv_null_map);
                CU_ADD_TEST(testSuite, test_add_argv_null_command);
                CU_ADD_TEST(testSuite, test_add_argv);

                if (log_init(progname)) {
                    (void)fprintf(stderr, "Couldn't open logging system\n");
                }
                else {
                    if (CU_basic_run_tests() == CUE_SUCCESS) {
                        if (0 == CU_get_number_of_failures())
                            exitCode = EXIT_SUCCESS;
                    }
                }
            }

            CU_cleanup_registry();
        }                           /* CUnit registery allocated */

        cm_free(childMap);
    }                                   /* "childMap" allocated */

    return exitCode;
}
