/*
 * See file ../COPYRIGHT for copying and redistribution conditions.
 */
#include "config.h"

#ifndef _XOPEN_SOURCE
#   define _XOPEN_SOURCE 500
#endif

#include <libgen.h>
#include <unistd.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#include "uldb.h"
#include "log.h"
#include "prod_class.h"

static const prod_spec spec_some = { ANY, "A", 0 };
static const prod_class_t clss_some = { { 0, 0 }, /* TS_ZERO */
{ 0x7fffffff, 999999 }, /* TS_ENDT */
{ 1, (prod_spec *) &spec_some /* cast away const */
} };

/**
 * Only called once.
 */
static int setup(
        void)
{
    int status = uldb_delete();

    if (status) {
        if (ULDB_EXIST == status) {
            log_clear();
        }
        else {
            LOG_ADD0("Couldn't delete database");
            log_log(LOG_ERR);
        }
    }

    if (status = uldb_create(0)) {
        LOG_ADD0("Couldn't create database");
        log_log(LOG_ERR);
    }

    return status;
}

/**
 * Only called once.
 */
static int teardown(
        void)
{
    int status = uldb_close();

    if (status) {
        LOG_ADD0("Couldn't close database");
        log_log(LOG_ERR);
    }

    if (status = uldb_delete()) {
        LOG_ADD0("Couldn't delete database");
        log_log(LOG_ERR);
    }

    return status;
}

static void test_nil(
        void)
{
    int status;
    struct sockaddr_in sockAddr;
    unsigned count;

    (void) memset(&sockAddr, 0, sizeof(sockAddr));

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 0);

    status = uldb_addFeeder(-1, 6, &sockAddr, &_clss_all);
    CU_ASSERT_EQUAL(status, ULDB_ARG);
}

static void populate(
        void)
{
    int status;
    struct sockaddr_in sockAddr;
    unsigned count;

    (void) memset(&sockAddr, 0, sizeof(sockAddr));

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 0);

    status = uldb_addFeeder(1, 6, &sockAddr, &_clss_all);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 1);

    status = uldb_addFeeder(1, 6, &sockAddr, &_clss_all);
    CU_ASSERT_EQUAL(status, ULDB_EXIST);

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 1);

    status = uldb_addNotifier(1, 5, &sockAddr, &_clss_all);
    CU_ASSERT_EQUAL(status, ULDB_EXIST);

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 1);

    status = uldb_addNotifier(2, 5, &sockAddr, &_clss_all);
    CU_ASSERT_EQUAL(status, ULDB_DISALLOWED);
    log_clear();

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 1);

    status = uldb_addNotifier(2, 5, &sockAddr, &clss_some);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 2);
}

static void clear(
        void)
{
    int status;
    unsigned count;
    uldb_Iter* iter;
    const uldb_Entry* entry;

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);

    status = uldb_getIterator(&iter);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_PTR_NOT_NULL(iter);

    for (entry = uldb_iter_firstEntry(iter); NULL != entry; entry =
            uldb_iter_nextEntry(iter)) {
        const pid_t pid = uldb_entry_getPid(entry);
        status = uldb_remove(pid);
        CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
        count--;
    }

    CU_ASSERT_EQUAL(count, 0);

    uldb_iter_free(iter);

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 0);
}

static void test_2(
        void)
{
    populate();
    clear();
}

static void test_iterator(
        void)
{
    int status;
    unsigned count;
    uldb_Iter* iter;
    const uldb_Entry* entry;
    prod_class* prodClass;

    status = uldb_getIterator(&iter);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_PTR_NOT_NULL(iter);

    entry = uldb_iter_firstEntry(iter);
    CU_ASSERT_PTR_NULL(entry);

    uldb_iter_free(iter);

    populate();

    status = uldb_getIterator(&iter);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_PTR_NOT_NULL(iter);

    entry = uldb_iter_firstEntry(iter);
    CU_ASSERT_PTR_NOT_NULL_FATAL(entry);
    CU_ASSERT_EQUAL(uldb_entry_getPid(entry), 1);
    CU_ASSERT_EQUAL(uldb_entry_getProtocolVersion(entry), 6);
    CU_ASSERT_EQUAL(uldb_entry_isNotifier(entry), 0);
    status = uldb_entry_getProdClass(entry, &prodClass);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_TRUE(clss_eq(&_clss_all, prodClass));
    free_prod_class(prodClass);

    entry = uldb_iter_nextEntry(iter);
    CU_ASSERT_PTR_NOT_NULL_FATAL(entry);
    CU_ASSERT_EQUAL(uldb_entry_getPid(entry), 2);
    CU_ASSERT_EQUAL(uldb_entry_getProtocolVersion(entry), 5);
    CU_ASSERT_EQUAL(uldb_entry_isNotifier(entry), 1);
    status = uldb_entry_getProdClass(entry, &prodClass);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_TRUE(clss_eq(&clss_some, prodClass));
    free_prod_class(prodClass);

    entry = uldb_iter_nextEntry(iter);
    CU_ASSERT_PTR_NULL(entry);

    uldb_iter_free(iter);

    clear();
}

static void test_remove(
        void)
{
    int status;
    unsigned count;
    pid_t pid;
    uldb_Iter* iter;
    const uldb_Entry* entry;
    prod_class* prodClass;

    populate();

    status = uldb_remove(1);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 1);

    status = uldb_getIterator(&iter);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_PTR_NOT_NULL(iter);

    entry = uldb_iter_firstEntry(iter);
    CU_ASSERT_PTR_NOT_NULL(entry);
    CU_ASSERT_EQUAL(uldb_entry_getPid(entry), 2);
    CU_ASSERT_EQUAL(uldb_entry_getProtocolVersion(entry), 5);
    CU_ASSERT_EQUAL(uldb_entry_isNotifier(entry), 1);
    status = uldb_entry_getProdClass(entry, &prodClass);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_TRUE(clss_eq(&clss_some, prodClass));
    free_prod_class(prodClass);

    entry = uldb_iter_nextEntry(iter);
    CU_ASSERT_PTR_NULL(entry);

    uldb_iter_free(iter);

    clear();
}

static void test_fork(
        void)
{
    int status;
    unsigned count;
    pid_t pid;

    populate();

    status = uldb_getSize(&count);
    CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
    CU_ASSERT_EQUAL(count, 2);

    pid = fork();
    CU_ASSERT_NOT_EQUAL_FATAL(pid, -1);
    if (0 == pid) {
        /* Child */
        status = uldb_remove(1);
        CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
        exit(ULDB_SUCCESS == status ? 0 : 1);
    }
    else {
        /* Parent */
        int stat;
        uldb_Iter* iter;
        const uldb_Entry* entry;
        prod_class* prodClass;

        status = waitpid(pid, &stat, 0);
        CU_ASSERT_NOT_EQUAL(status, -1);
        CU_ASSERT_TRUE(WIFEXITED(stat));
        CU_ASSERT_EQUAL(WEXITSTATUS(stat), 0);

        status = uldb_getSize(&count);
        CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
        CU_ASSERT_EQUAL(count, 1);

        status = uldb_getIterator(&iter);
        CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
        CU_ASSERT_PTR_NOT_NULL(iter);

        entry = uldb_iter_firstEntry(iter);
        CU_ASSERT_PTR_NOT_NULL(entry);
        CU_ASSERT_EQUAL(uldb_entry_getPid(entry), 2);
        CU_ASSERT_EQUAL(uldb_entry_getProtocolVersion(entry), 5);
        CU_ASSERT_EQUAL(uldb_entry_isNotifier(entry), 1);
        status = uldb_entry_getProdClass(entry, &prodClass);
        CU_ASSERT_EQUAL(status, ULDB_SUCCESS);
        CU_ASSERT_TRUE(clss_eq(&clss_some, prodClass));
        free_prod_class(prodClass);

        entry = uldb_iter_nextEntry(iter);
        CU_ASSERT_PTR_NULL(entry);

        uldb_iter_free(iter);
    }
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode;
    const char* progname = basename((char*) argv[0]);

    if (-1 == openulog(progname, 0, LOG_LOCAL0, "-")) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
        exitCode = 1;
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_nil)
                        && CU_ADD_TEST(testSuite, test_2)
                        && CU_ADD_TEST(testSuite, test_iterator)
                        && CU_ADD_TEST(testSuite, test_remove)
                        && CU_ADD_TEST(testSuite, test_fork)) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            CU_cleanup_registry();
        }

        exitCode = CU_get_error();
    }

    return exitCode;
}
