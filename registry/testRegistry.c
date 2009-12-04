/*
 * See file ../COPYRIGHT for copying and redistribution conditions.
 */
#include "config.h"

#ifndef _XOPEN_SOURCE
#   define _XOPEN_SOURCE 500
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <syslog.h>
#include <sys/stat.h>
#include <ulog.h>
#include <log.h>

#include "registry.h"

#define TESTDIR_PATH    "/tmp/testRegistry"


static int
setup(void)
{
    int         status = -1;    /* failure */

    status = reg_setPathname(TESTDIR_PATH);

    return status;
}


static int
teardown(void)
{
    return 0;
}

static void
test_regString(void)
{
    RegStatus   status;
    char*       value;

    status = reg_putString("foo key", "foo value 0");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_putString("foo key", "foo value");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getString("foo key", &value, "overridden default");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(value, "foo value");
    free(value);

    status = reg_getString("foo key/sub key", &value, "overridden default");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(value, "foo value");
    free(value);

    status = reg_getString("bar key", &value, "default bar value");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(value, "default bar value");
    free(value);

    status = reg_getString("bof key", &value, NULL);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_PTR_NULL(value);
}

static void
test_regInt(void)
{
    RegStatus   status;
    int         value;

    status = reg_putUint("fooInt key", 1);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_putUint("fooInt key", 2);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getUint("fooInt key", &value, 3);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(value, 2);

    status = reg_getUint("fooInt key/subkey", &value, 3);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(value, 2);

    status = reg_getUint("barInt key", &value, 4);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(value, 4);
}


#if 0
static void
test_regDelete(void)
{
    RegStatus   status;
    char*       string;
    int         value;

    status = reg_putString("string key", "string value");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getString("string key", &string, NULL);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(string, "string value");
    free(string);

    status = reg_delete("string key");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getString("string key", &string, "default string value");
    if (status && RDB_NOENTRY != status)
        log_log(LOG_ERR);
    CU_ASSERT_STRING_EQUAL(string, "default string value");
    free(string);

    status = reg_putUint("int key", -1);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getUint("int key", &value, 4);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(value, -1);

    status = reg_delete("int key");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getUint("int key", &value, 4);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(value, 4);

    status = reg_delete("nosuch key");
    if (status && RDB_NOENTRY != status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, RDB_NOENTRY);
}
#endif


static void
test_regRemove(void)
{
    RegStatus   status;

    status = reg_remove();
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
}

/*
    CU_ASSERT_EQUAL(ut_map_unit_to_name(unit, "name", UT_ASCII), UT_SUCCESS);
    CU_ASSERT_PTR_NOT_NULL_FATAL(unit);
    CU_ASSERT_PTR_NULL(ut_new_base_unit(NULL));
    CU_ASSERT_STRING_EQUAL(buf, "1");
    CU_ASSERT_TRUE_FATAL(nchar > 0);
    CU_ASSERT_TRUE(n > 0);
*/

int
main(
    const int           argc,
    const char* const*  argv)
{
    int exitCode = EXIT_FAILURE;

    if (-1 == mkdir(TESTDIR_PATH, S_IRWXU)) {
        (void)fprintf(stderr, "Couldn't create test directory \"%s\": %s\n",
            TESTDIR_PATH, strerror(errno));
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite*       testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                CU_ADD_TEST(testSuite, test_regString);
                CU_ADD_TEST(testSuite, test_regInt);
                /*
                CU_ADD_TEST(testSuite, test_regDelete);
                */
                CU_ADD_TEST(testSuite, test_regRemove);

                if (-1 == openulog(argv[0], 0, LOG_LOCAL0, "-")) {
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
        }                               /* CUnit registery allocated */

        if (-1 == rmdir(TESTDIR_PATH))
            (void)fprintf(stderr, "Couldn't delete test directory \"%s\": %s\n",
                TESTDIR_PATH, strerror(errno));
    }                                   /* test-directory created */

    return exitCode;
}
