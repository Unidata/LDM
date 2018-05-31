/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the `Executor` module.
 *
 * @author: Steven R. Emmerson
 */

#include "../../misc/Executor.h"

#include "config.h"

#include "log.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

static void sigTermHandler(int sig)
{
    if (sig == SIGTERM) {
        log_notice_q("SIGTERM");
    }
    else {
        log_notice_q("Signal %d", sig);
    }
}

static int
setSigTermHandler(void)
{
    struct sigaction sigact;
    int              status = sigemptyset(&sigact.sa_mask);

    if (status == 0) {
        sigact.sa_flags = 0;
        sigact.sa_handler = sigTermHandler;
        status = sigaction(SIGTERM, &sigact, NULL);
    }

    return status;
}

/**
 * Only called once.
 */
static int setup(void)
{
    return setSigTermHandler();
}

/**
 * Only called once.
 */
static int teardown(void)
{
    return 0;
}

static void test_construction(void)
{
    Executor* executor = executor_new();
    CU_ASSERT_PTR_NOT_NULL(executor);
    executor_free(executor);
}

static int trivialRun(void* const restrict arg)
{
    return 0;
}

static int retOne(
        void* const restrict  arg,
        void** const restrict result)
{
    static int one = 1;
    *result = &one;
    return 0;
}

static int retObj(
        void* const restrict  arg,
        void** const restrict result)
{
    if (result != NULL)
        *result = arg;
    return 0;
}

static int noRet(void* const restrict arg)
{
    pause();
    return 0;
}

static void test_nullJob(void)
{
    Executor* executor = executor_new();
    CU_ASSERT_PTR_NOT_NULL(executor);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    Future* future = executor_submit(executor, NULL, trivialRun, NULL, NULL);
    CU_ASSERT_PTR_NOT_NULL(future);

    CU_ASSERT_EQUAL(future_getAndFree(future, NULL), 0);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_returnOne(void)
{
    Executor* executor = executor_new();
    CU_ASSERT_PTR_NOT_NULL(executor);

    Future* future = executor_submit(executor, NULL, trivialRun, NULL, retOne);
    CU_ASSERT_PTR_NOT_NULL(future);

    void* result;
    CU_ASSERT_EQUAL(future_getAndFree(future, &result), 0);
    CU_ASSERT_PTR_NOT_NULL(result);
    CU_ASSERT_EQUAL(*(int*)result, 1);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_returnObj(void)
{
    int       obj = 2;

    Executor* executor = executor_new();
    CU_ASSERT_PTR_NOT_NULL(executor);

    Future* future = executor_submit(executor, &obj, trivialRun, NULL, retObj);
    CU_ASSERT_PTR_NOT_NULL(future);

    void* result;
    CU_ASSERT_EQUAL(future_getAndFree(future, &result), 0);
    CU_ASSERT_PTR_EQUAL(result, &obj);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_cancel(void)
{
    Executor* executor = executor_new();
    Future*   noRetFuture = executor_submit(executor, NULL, noRet, NULL, NULL);

    CU_ASSERT_EQUAL(future_cancel(noRetFuture), 0);

    CU_ASSERT_EQUAL(future_getAndFree(noRetFuture, NULL), ECANCELED);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_shutdown(void)
{
    Executor* executor = executor_new();
    Future*   noRetFuture = executor_submit(executor, NULL, noRet, NULL, NULL);

    executor_shutdown(executor, false);

    int     obj = 3;
    Future* retObjFuture = executor_submit(executor, &obj, trivialRun, NULL,
            retObj);
    CU_ASSERT_PTR_NULL(retObjFuture);
    log_clear();

    future_cancel(noRetFuture);
    CU_ASSERT_EQUAL(future_getAndFree(noRetFuture, NULL), ECANCELED);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_shutdownNow(void)
{
    Executor* executor = executor_new();
    Future*   noRetFuture = executor_submit(executor, NULL, noRet, NULL, NULL);

    executor_shutdown(executor, true);

    int     obj = 3;
    Future* retObjFuture = executor_submit(executor, &obj, trivialRun, NULL,
            retObj);
    CU_ASSERT_PTR_NULL(retObjFuture);
    log_clear();

    CU_ASSERT_EQUAL(future_getAndFree(noRetFuture, NULL), ECANCELED);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static int submitObj = 3;

static int
afterComp(
        void* const restrict   arg,
        Future* const restrict future)
{
    CU_ASSERT_PTR_NULL(arg);
    CU_ASSERT_PTR_NOT_NULL(future);

    int* value = (int*)future_getObj(future);
    CU_ASSERT_PTR_NOT_NULL(value);
    CU_ASSERT_PTR_EQUAL(value, &submitObj);

    return 0;
}

static void test_afterCompletion(void)
{
    Executor* executor = executor_new();
    executor_setAfterCompletion(executor, NULL, afterComp);

    Future*   future = executor_submit(executor, &submitObj, trivialRun, NULL,
            retObj);
    CU_ASSERT_PTR_NOT_NULL(future);

    void* result;
    CU_ASSERT_EQUAL(future_getAndFree(future, &result), 0);
    CU_ASSERT_PTR_EQUAL(result, &submitObj);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
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
                if (CU_ADD_TEST(testSuite, test_construction)
                        && CU_ADD_TEST(testSuite, test_nullJob)
                        && CU_ADD_TEST(testSuite, test_returnOne)
                        && CU_ADD_TEST(testSuite, test_returnObj)
                        && CU_ADD_TEST(testSuite, test_cancel)
                        && CU_ADD_TEST(testSuite, test_shutdown)
                        && CU_ADD_TEST(testSuite, test_shutdownNow)
                        && CU_ADD_TEST(testSuite, test_afterCompletion)
                        ) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }

        log_fini();
    }

    return exitCode;
}
