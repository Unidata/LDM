/**
 * Copyright 2019 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the `ChildCommand` module.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "log.h"
#include "ChildCmd.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <libgen.h>
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

static void test_true(void)
{
    const char* const cmdVec[] = {"true", NULL};
    ChildCmd*         cmd = childCmd_execvp(cmdVec[0], cmdVec);
    CU_ASSERT_PTR_NOT_NULL(cmd);

    int exitStatus;
    int status = childCmd_reap(cmd, &exitStatus);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(exitStatus, 0);
}

static void test_false(void)
{
    const char* const cmdVec[] = {"false", NULL};
    ChildCmd*         cmd = childCmd_execvp(cmdVec[0], cmdVec);
    CU_ASSERT_PTR_NOT_NULL(cmd);

    int exitStatus;
    int status = childCmd_reap(cmd, &exitStatus);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(exitStatus, 1);
}

static void test_echoToStdOut(void)
{
    const char string[] = "Hello, world!";
    const char* const cmdVec[] = {"echo", string, NULL};
    ChildCmd*         cmd = childCmd_execvp(cmdVec[0], cmdVec);
    CU_ASSERT_PTR_NOT_NULL(cmd);

    char*   line = NULL;
    size_t  size = 0;
    ssize_t nbytes = childCmd_getline(cmd, &line, &size);
    CU_ASSERT_EQUAL(nbytes, strlen(string)+1); // Plus newline
    line[nbytes-1] = 0;
    CU_ASSERT_STRING_EQUAL(line, string);

    int exitStatus;
    int status = childCmd_reap(cmd, &exitStatus);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(exitStatus, 0);
}

static void test_writeToStdErr(void)
{
    const char* const cmdVec[] = {"ls", "/foo.bar", NULL};
    ChildCmd*         cmd = childCmd_execvp(cmdVec[0], cmdVec);
    CU_ASSERT_PTR_NOT_NULL(cmd);

    int exitStatus;
    int status = childCmd_reap(cmd, &exitStatus);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_TRUE(exitStatus > 0);
}

static void test_getCmd(void)
{
    const char* const cmdVec[] = {"true", "arg", "split arg", NULL};
    ChildCmd*         cmd = childCmd_execvp(cmdVec[0], cmdVec);
    CU_ASSERT_PTR_NOT_NULL(cmd);

    const char* const cmdStr = childCmd_getCmd(cmd);
    CU_ASSERT_STRING_EQUAL(cmdStr, "true arg 'split arg'");

    int exitStatus;
    int status = childCmd_reap(cmd, &exitStatus);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(exitStatus, 0);
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
                if (       CU_ADD_TEST(testSuite, test_true)
                        && CU_ADD_TEST(testSuite, test_false)
                        && CU_ADD_TEST(testSuite, test_echoToStdOut)
                        && CU_ADD_TEST(testSuite, test_writeToStdErr)
                        && CU_ADD_TEST(testSuite, test_getCmd)
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
