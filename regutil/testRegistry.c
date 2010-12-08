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
test_regMissing(void)
{
    RegStatus   status;
    char*       value;

    /* The registry shouldn't exist. */
    status = reg_getString("/foo_key", &value);
    CU_ASSERT_EQUAL(status, EIO);
    log_clear();
}

static void
test_keyWithSpace(void)
{
    RegStatus   status;
    char*       value;

    status = reg_putString("/foo key", "foo value 0");
    CU_ASSERT_EQUAL(status, EINVAL);
    log_clear();
}

static void
test_regString(void)
{
    RegStatus   status;
    char*       value;

    status = reg_putString("/foo_key", "foo value 0");
    if (status) {
        log_add("test_regString(): Couldn't put string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_putString("/foo_key", "foo value");
    if (status) {
        log_add("test_regString(): Couldn't replace string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getString("/foo_key", &value);
    if (status) {
        log_add("test_regString(): Couldn't get string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_STRING_EQUAL(value, "foo value");
        free(value);
    }

    status = reg_getString("/bar_key", &value);
    if (status && ENOENT != status) {
        log_add("test_regString(): Couldn't add second string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }
}

static void
test_regInt(void)
{
    RegStatus   status;
    int         value;

    status = reg_putUint("/fooInt_key", 1);
    if (status) {
        log_add("test_regInt(): Couldn't add int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_putUint("/fooInt_key", 2);
    if (status) {
        log_add("test_regInt(): Couldn't replace int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getUint("/fooInt_key", &value);
    if (status) {
        log_add("test_regInt(): Couldn't get int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_EQUAL(value, 2);
    }

    status = reg_getUint("/barInt_key", &value);
    if (status && ENOENT != status) {
        log_add("test_regInt(): Couldn't put second int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }
}

static void
test_regTime(void)
{
    RegStatus   status;
    timestampt  value;

    status = reg_putTime("/fooTime_key", &TS_ENDT);
    if (status) {
        log_add("test_regTime(): Couldn't put time");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_putTime("/fooTime_key", &TS_ZERO);
    if (status) {
        log_add("test_regTime(): Couldn't replace time");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getTime("/fooTime_key", &value);
    if (status) {
        log_add("test_regTime(): Couldn't get time");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_EQUAL(d_diff_timestamp(&value, &TS_ZERO), 0.0);
    }

    status = reg_getTime("/barTime_key", &value);
    if (status && ENOENT != status) {
        log_add("test_regTime(): Couldn't put second time");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }
}

static void
test_regSignature(void)
{
    RegStatus   status;
    signaturet  value;
    signaturet  value1 = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    signaturet  value2 =
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

    status = reg_putSignature("/fooSignature_key", value1);
    if (status) {
        log_add("test_regSignature(): Couldn't put signature");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_putSignature("/fooSignature_key", value2);
    if (status) {
        log_add("test_regSignature(): Couldn't replace signature");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getSignature("/fooSignature_key", &value);
    if (status) {
        log_add("test_regSignature(): Couldn't get signature");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_EQUAL(memcmp(value, value2, sizeof(signaturet)), 0);
    }

    status = reg_getSignature("/barSignature_key", &value);
    if (status && ENOENT != status) {
        log_add("test_regSignature(): Couldn't put second signature");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }
}

static void
test_regSubkeys(void)
{
    RegStatus   status;
    char*       value;

    status = reg_putString("/subkey/string_key", "string_value");
    if (status) {
        log_add("test_subkeys(): Couldn't put string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_putString("/subkey/string_key", "string value 2");
    if (status) {
        log_add("test_subkeys(): Couldn't replace string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getString("/subkey/string_key", &value);
    if (status) {
        log_add("test_regString(): Couldn't get string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_STRING_EQUAL(value, "string value 2");
        free(value);
    }

    status = reg_putString("/subkey/string_key2", "string_value2");
    if (status) {
        log_add("test_subkeys(): Couldn't put second string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getString("/subkey/nonexistant_key", &value);
    if (status && ENOENT != status) {
        log_add("test_regString(): Couldn't verify non-existant value");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }
}

static void
test_regDelete(void)
{
    RegStatus   status;
    char*       string;
    int         value;

    status = reg_putString("/string_key", "string value");
    if (status) {
        log_add("test_regDelete(): Couldn't put string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getString("/string_key", &string);
    if (status) {
        log_add("test_regDelete(): Couldn't get string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_STRING_EQUAL(string, "string value");
        free(string);
    }

    status = reg_deleteValue("/string_key");
    if (status) {
        log_add("test_regDelete(): Couldn't delete string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getString("/string_key", &string);
    if (status && ENOENT != status) {
        log_add("test_regDelete(): Couldn't verify string deletion");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }

    status = reg_putUint("/int_key", -1);
    if (status) {
        log_add("test_regDelete(): Couldn't put int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getUint("/int_key", &value);
    if (status) {
        log_add("test_regDelete(): Couldn't get int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_EQUAL(value, -1);
    }

    status = reg_deleteValue("/int_key");
    if (status) {
        log_add("test_regDelete(): Couldn't delete int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getUint("/int_key", &value);
    if (status && ENOENT != status) {
        log_add("test_regDelete(): Couldn't verify int deletion");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }

    status = reg_deleteValue("/nosuch_key");
    if (status && ENOENT != status) {
        log_add("test_regDelete(): Couldn't verify no-such-value deletion");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }
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

    status = reg_getNode("/test_node/subnode", &subnode, 1);
    if (status) {
        log_add("test_regNode(): Couldn't get subnode");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL_FATAL(status, 0);
        CU_ASSERT_PTR_NOT_NULL(subnode);
    }

    constString = reg_getNodeName(subnode);
    CU_ASSERT_STRING_EQUAL(constString, "subnode");

    constString = reg_getNodeAbsPath(subnode);
    CU_ASSERT_STRING_EQUAL(constString, "/test_node/subnode");

    status = reg_getNodeString(subnode, "string_key", &string);
    if (status && ENOENT != status) {
        log_add("test_regNode(): Couldn't verify non-existant subnode string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }

    status = reg_putNodeString(subnode, "string_key", "string value");
    if (0 != status) {
        log_add("test_regNode(): Couldn't add subnode string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getNodeString(subnode, "string_key", &string);
    if (status) {
        log_add("test_regNode(): Couldn't get subnode string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_STRING_EQUAL(string, "string value");
        free(string);
    }

    status = reg_getNodeUint(subnode, "uint_key", &uint);
    if (status && ENOENT != status) {
        log_add("test_regNode(): Couldn't verify non-existant subnode int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }

    status = reg_putNodeUint(subnode, "uint_key", 5);
    if (0 != status) {
        log_add("test_regNode(): Couldn't put subnode int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getNodeUint(subnode, "uint_key", &uint);
    if (status) {
        log_add("test_regNode(): Couldn't get subnode int");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_EQUAL(uint, 5);
    }

    status = reg_getNodeTime(subnode, "time_key", &time);
    if (status && ENOENT != status) {
        log_add("test_regNode(): Couldn't verify non-existant subnode time");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }

    status = reg_putNodeTime(subnode, "time_key", &TS_ZERO);
    if (0 != status) {
        log_add("test_regNode(): Couldn't put subnode time");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getNodeTime(subnode, "time_key", &time);
    if (status) {
        log_add("test_regNode(): Couldn't get subnode time");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_EQUAL(d_diff_timestamp(&time, &TS_ZERO), 0.0);
    }

    status = reg_getNodeSignature(subnode, "sig_key", &sig);
    if (status && ENOENT != status) {
        log_add("test_regNode(): Couldn't verify non-existant subnode sig");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }

    status = reg_putNodeSignature(subnode, "sig_key", defSig2);
    if (0 != status) {
        log_add("test_regNode(): Couldn't put subnode sig");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getNodeSignature(subnode, "sig_key", &sig);
    if (status) {
        log_add("test_regNode(): Couldn't get subnode sig");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_EQUAL(memcmp(sig, defSig2, sizeof(signaturet)), 0);
    }

    status = reg_deleteNodeValue(subnode, "non-existant_key");
    if (status && ENOENT != status) {
        log_add("test_regNode(): Couldn't verify non-existant subnode value deletion");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }

    status = reg_deleteNodeValue(subnode, "string_key");
    if (status) {
        log_add("test_regNode(): Couldn't delete subnode value");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    status = reg_getNodeString(subnode, "string_key", &string);
    if (status && ENOENT != status) {
        log_add("test_regNode(): Couldn't verify non-existant subnode string");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, ENOENT);
    }

    status = reg_getNode("/test_node", &testnode, 1);
    if (status) {
        log_add("test_regNode(): Couldn't get subnode");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL_FATAL(status, 0);
        CU_ASSERT_PTR_NOT_NULL(testnode);
    }

    status = reg_flushNode(testnode);
    if (status) {
        log_add("test_regNode(): Couldn't flush node");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }

    rn_free(testnode);

    {
        status = reg_getNode("/test_node2", &testnode, 1);
        if (status) {
            log_add("test_regNode(): Couldn't get temporary node");
            log_log(LOG_ERR);
        }
        else {
            CU_ASSERT_EQUAL_FATAL(status, 0);
            CU_ASSERT_PTR_NOT_NULL(testnode);
        }

        status = reg_putNodeString(testnode, "string_key", "string value");
        if (0 != status) {
            log_add("test_regNode(): Couldn't add temporary node string");
            log_log(LOG_ERR);
        }
        else {
            CU_ASSERT_EQUAL(status, 0);
        }

        status = reg_flushNode(testnode);
        if (status) {
            log_add("test_regNode(): Couldn't flush temporary node");
            log_log(LOG_ERR);
        }
        else {
            CU_ASSERT_EQUAL(status, 0);
        }

        reg_deleteNode(testnode);

        status = reg_flushNode(testnode);
        if (status) {
            log_add("test_regNode(): Couldn't delete temporary node");
            log_log(LOG_ERR);
        }
        else {
            CU_ASSERT_EQUAL(status, 0);
        }

        rn_free(testnode);

        status = reg_getNode("/test_node2", &testnode, 0);
        if (status && ENOENT != status) {
            log_add("test_regNode(): Couldn't verify temporary node deletion");
            log_log(LOG_ERR);
        }
        else {
            CU_ASSERT_EQUAL(status, ENOENT);
        }
    }
}

static void
test_regReset(void)
{
    RegStatus   status;

    status = reg_reset();
    if (status) {
        log_add("test_regReset(): Couldn't reset registry");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }
}


static void
test_regRemove(void)
{
    RegStatus   status;

    status = reg_remove();
    if (status) {
        log_add("test_regRemove(): Couldn't remove registry");
        log_log(LOG_ERR);
    }
    else {
        CU_ASSERT_EQUAL(status, 0);
    }
}

int
main(
    const int           argc,
    const char* const*  argv)
{
    int         exitCode = EXIT_FAILURE;
    char        cmd[80];
    char*       regPath = "/tmp/testRegistry";

    if (0 != system(strcat(strcpy(cmd, "rm -rf "), regPath))) {
        (void)fprintf(stderr, "Couldn't remove test-directory \"%s\": %s",
            regPath, strerror(errno));
    }
    else {
        if (-1 == mkdir(regPath, S_IRWXU)) {
            (void)fprintf(stderr, "Couldn't create test directory \"%s\": %s\n",
                regPath, strerror(errno));
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

                    CU_ADD_TEST(testSuite, test_regMissing);
                    CU_ADD_TEST(testSuite, test_keyWithSpace);
                    CU_ADD_TEST(testSuite, test_regString);
                    CU_ADD_TEST(testSuite, test_regInt);
                    CU_ADD_TEST(testSuite, test_regTime);
                    CU_ADD_TEST(testSuite, test_regSignature);
                    CU_ADD_TEST(testSuite, test_regSubkeys);
                    CU_ADD_TEST(testSuite, test_regDelete);
                    CU_ADD_TEST(testSuite, test_regNode);
                    CU_ADD_TEST(testSuite, test_regReset);
                    #if 0
                    CU_ADD_TEST(testSuite, test_regRemove);
                    #endif

                    if (-1 == openulog(progname, 0, LOG_LOCAL0, "-")) {
                        (void)fprintf(stderr, "Couldn't open logging system\n");
                    }
                    else {
                        reg_setDirectory(regPath);

                        if (CU_basic_run_tests() == CUE_SUCCESS) {
                            if (0 == CU_get_number_of_failures())
                                exitCode = EXIT_SUCCESS;
                        }
                    }
                }

                CU_cleanup_registry();
            }                           /* CUnit registery allocated */

            #if 0
            if (-1 == rmdir(regPath))
                (void)fprintf(stderr,
                    "Couldn't delete test directory \"%s\": %s\n",
                    regPath, strerror(errno));
            #endif
        }                               /* test-directory created */
    }                                   /* test-directory removed */

    return exitCode;
}
