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

#include <log.h>
#include "registry.h"
#include "timestamp.h"
#include <ulog.h>

#define TESTDIR_PATH    "/tmp/testRegistry"


static int
setup(void)
{
    return reg_setPathname(TESTDIR_PATH);
}


static int
teardown(void)
{
    reg_close();
    return 0;
}

static void
test_regString(void)
{
    RegStatus   status;
    char*       value;

    status = reg_putString("/foo key", "foo value 0");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_putString("/foo key", "foo value");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getString("/foo key", &value, "unused default");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(value, "foo value");
    free(value);

    status = reg_getString("/bar key", &value, "default bar value");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(value, "default bar value");
    free(value);

    status = reg_getString("/bof key", &value, NULL);
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

    status = reg_putUint("/fooInt key", 1);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_putUint("/fooInt key", 2);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getUint("/fooInt key", &value, 3);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(value, 2);

    status = reg_getUint("/barInt key", &value, 4);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(value, 4);
}

static void
test_regTime(void)
{
    RegStatus   status;
    timestampt  value;

    status = reg_putTime("/fooTime key", &TS_ENDT);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_putTime("/fooTime key", &TS_ZERO);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getTime("/fooTime key", &value, &TS_NONE);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(d_diff_timestamp(&value, &TS_ZERO), 0.0);

    status = reg_getTime("/barTime key", &value, &TS_ENDT);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(d_diff_timestamp(&value, &TS_ENDT), 0.0);
}

static void
test_regSignature(void)
{
    RegStatus   status;
    signaturet  value;
    signaturet  value1 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    signaturet  value2 =
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    status = reg_putSignature("/fooSignature key", value1);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_putSignature("/fooSignature key", value2);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getSignature("/fooSignature key", &value, value1);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(memcmp(value, value2, sizeof(signaturet)), 0);

    status = reg_getSignature("/barSignature key", &value, value1);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(memcmp(value, value1, sizeof(signaturet)), 0);
}

static void
test_regDelete(void)
{
    RegStatus   status;
    char*       string;
    int         value;

    status = reg_putString("/string key", "string value");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getString("/string key", &string, NULL);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(string, "string value");
    free(string);

    status = reg_deleteValue("/string key");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getString("/string key", &string, "default string value");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_STRING_EQUAL(string, "default string value");
    free(string);

    status = reg_putUint("/int key", -1);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getUint("/int key", &value, 4);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(value, -1);

    status = reg_deleteValue("/int key");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getUint("/int key", &value, 4);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(value, 4);

    status = reg_deleteValue("/nosuch key");
    if (status && ENOENT != status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, ENOENT);
}

static void
test_regNode(void)
{
    RegStatus   status;
    RegNode*    testnode;
    RegNode*    subnode;
    char*       string;
    const char* constString;
    unsigned    uint;
    timestampt  time;
    signaturet  defSig1 = {0};
    signaturet  defSig2 = {1};
    signaturet  sig;

    status = reg_getNode("/test node/subnode", &subnode);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_PTR_NOT_NULL(subnode);

    constString = reg_getNodeName(subnode);
    CU_ASSERT_STRING_EQUAL(constString, "subnode");

    constString = reg_getNodeAbsPath(subnode);
    CU_ASSERT_STRING_EQUAL(constString, "/test node/subnode");

    status = reg_getNodeString(subnode, "string key", &string,
        "default string");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(string, "default string");
    free(string);

    status = reg_putNodeString(subnode, "string key", "string value");
    if (0 != status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getNodeString(subnode, "string key", &string,
        "default string");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(string, "string value");
    free(string);

    status = reg_getNodeUint(subnode, "uint key", &uint, 9);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(uint, 9);

    status = reg_putNodeUint(subnode, "uint key", 5);
    if (0 != status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getNodeUint(subnode, "uint key", &uint, 9);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(uint, 5);

    status = reg_getNodeTime(subnode, "time key", &time, &TS_ENDT);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(d_diff_timestamp(&time, &TS_ENDT), 0.0);

    status = reg_putNodeTime(subnode, "time key", &TS_ZERO);
    if (0 != status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getNodeTime(subnode, "time key", &time, &TS_ENDT);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(d_diff_timestamp(&time, &TS_ZERO), 0.0);

    status = reg_getNodeSignature(subnode, "sig key", &sig, defSig1);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(memcmp(sig, defSig1, sizeof(signaturet)), 0);

    status = reg_putNodeSignature(subnode, "sig key", defSig2);
    if (0 != status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getNodeSignature(subnode, "sig key", &sig, defSig1);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_EQUAL(memcmp(sig, defSig2, sizeof(signaturet)), 0);

    status = reg_deleteNodeValue(subnode, "non-existant key");
    if (status && ENOENT != status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, ENOENT);

    status = reg_deleteNodeValue(subnode, "string key");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = reg_getNodeString(subnode, "string key", &string,
        "default string");
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_STRING_EQUAL(string, "default string");
    free(string);

    status = reg_getNode("/test node", &testnode);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_PTR_NOT_NULL(testnode);

    reg_deleteNode(testnode);

    status = reg_getNodeString(subnode, "string key", &string,
        "default string");
    CU_ASSERT_EQUAL(status, EPERM);

    status = reg_flushNode(testnode);
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
}

static void
test_regRemove(void)
{
    RegStatus   status;

    status = reg_remove();
    if (status)
        log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
}

int
main(
    const int           argc,
    const char* const*  argv)
{
    int         exitCode = EXIT_FAILURE;
    char        cmd[80];

    if (0 != system(strcat(strcpy(cmd, "rm -rf "), TESTDIR_PATH))) {
        (void)fprintf(stderr, "Couldn't remove test-directory \"%s\": %s",
            TESTDIR_PATH, strerror(errno));
    }
    else {
        if (-1 == mkdir(TESTDIR_PATH, S_IRWXU)) {
            (void)fprintf(stderr, "Couldn't create test directory \"%s\": %s\n",
                TESTDIR_PATH, strerror(errno));
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

                    CU_ADD_TEST(testSuite, test_regString);
                    CU_ADD_TEST(testSuite, test_regInt);
                    CU_ADD_TEST(testSuite, test_regTime);
                    CU_ADD_TEST(testSuite, test_regSignature);
                    CU_ADD_TEST(testSuite, test_regDelete);
                    CU_ADD_TEST(testSuite, test_regNode);
                    #if 0
                    CU_ADD_TEST(testSuite, test_regRemove);
                    #endif

                    if (-1 == openulog(progname, 0, LOG_LOCAL0, "-")) {
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

            #if 0
            if (-1 == rmdir(TESTDIR_PATH))
                (void)fprintf(stderr,
                    "Couldn't delete test directory \"%s\": %s\n",
                    TESTDIR_PATH, strerror(errno));
            #endif
        }                               /* test-directory created */
    }                                   /* test-directory removed */

    return exitCode;
}
