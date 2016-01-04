/*
 * Copyright 2012 University Corporation for Atmospheric Research. All rights
 * reserved.
 *
 * See file COPYRIGHT in the top-level source-directory for legal conditions.
 */

#include <config.h>

#include <libgen.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <sys/wait.h>
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#include <mylog.h>
#include <globals.h>

#include "semRWLock.h"

static key_t    key;

static int setup(
        void)
{
    key = ftok(getQueuePath(), 2);
    return 0;
}

static int teardown(
        void)
{
    return 0;
}

static void test_create(
        void)
{
    srwl_Lock* lock;
    srwl_Status status = srwl_create(key, &lock);

    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_delete(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_delete(lock);
    CU_ASSERT_EQUAL(status, RWL_INVALID);

    status = srwl_free(lock);
    CU_ASSERT_EQUAL(status, RWL_INVALID);
}

static void test_get(
        void)
{
    srwl_Lock* lock;
    srwl_Status status;

    status = srwl_get(key, &lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    status = srwl_create(key, &lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_delete(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);
}

static void test_write_lock(
        void)
{
    srwl_Lock* lock;
    int       status;
    pid_t pid;

    status = srwl_create(key, &lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_writeLock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_free(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    status = srwl_readLock(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    pid = fork();
    CU_ASSERT_NOT_EQUAL(pid, -1);
    if (0 == pid) {
        /* Child */
        srwl_Status status = srwl_readLock(lock);
        exit(RWL_SUCCESS == status ? 0 : 1);
    }
    else {
        /* Parent */
        int stat;

        sleep(1);

        status = srwl_unlock(lock);
        CU_ASSERT_EQUAL(status, RWL_SUCCESS);

        status = waitpid(pid, &stat, 0);
        CU_ASSERT_NOT_EQUAL(status, -1);
        CU_ASSERT_TRUE(WIFEXITED(stat));
        CU_ASSERT_EQUAL(WEXITSTATUS(stat), 0);

        status = srwl_unlock(lock);
        CU_ASSERT_EQUAL(status, RWL_SUCCESS);
    }

    status = srwl_delete(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);
}

static void test_read_lock(
        void)
{
    srwl_Lock* lock;
    int       status;
    pid_t pid;

    status = srwl_create(key, &lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_readLock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_free(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    status = srwl_writeLock(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    pid = fork();
    CU_ASSERT_NOT_EQUAL(pid, -1);
    if (0 == pid) {
        /* Child */
        exit(RWL_SUCCESS == srwl_writeLock(lock) ? 0 : 1);
    }
    else {
        /* Parent */
        int stat;

        sleep(1);

        status = srwl_unlock(lock);
        CU_ASSERT_EQUAL(status, RWL_SUCCESS);

        status = waitpid(pid, &stat, 0);
        CU_ASSERT_NOT_EQUAL(status, -1);
        CU_ASSERT_TRUE(WIFEXITED(stat));
        CU_ASSERT_EQUAL(WEXITSTATUS(stat), 0);

        status = srwl_unlock(lock);
        CU_ASSERT_EQUAL(status, RWL_SUCCESS);
    }

    status = srwl_delete(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);
}

static void test_multiple_write(void) {
    srwl_Lock* lock;
    int       status;

    status = srwl_create(key, &lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_writeLock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_writeLock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_readLock(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    status = srwl_unlock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_readLock(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    status = srwl_unlock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_readLock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_free(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    status = srwl_unlock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_delete(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);
}

static void test_multiple_read(void) {
    srwl_Lock* lock;
    int       status;

    status = srwl_create(key, &lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_readLock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_readLock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_writeLock(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    status = srwl_unlock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_writeLock(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    status = srwl_unlock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_writeLock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_free(lock);
    CU_ASSERT_EQUAL(status, RWL_EXIST);

    status = srwl_unlock(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);

    status = srwl_delete(lock);
    CU_ASSERT_EQUAL(status, RWL_SUCCESS);
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1; /* failure */

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            const char* progname = basename((char*)argv[0]);

            CU_ADD_TEST(testSuite, test_create);
            CU_ADD_TEST(testSuite, test_get);
            CU_ADD_TEST(testSuite, test_write_lock);
            CU_ADD_TEST(testSuite, test_read_lock);
            CU_ADD_TEST(testSuite, test_multiple_write);
            CU_ADD_TEST(testSuite, test_multiple_read);

            if (mylog_init(progname)) {
                (void) fprintf(stderr, "Couldn't open logging system\n");
            }
            else {
                if (CU_basic_run_tests() == CUE_SUCCESS) {
                    if (0 == CU_get_number_of_failures())
                        exitCode = 0; /* success */
                }
            }
        }

        CU_cleanup_registry();
    } /* CUnit registry allocated */

    return exitCode;
}
