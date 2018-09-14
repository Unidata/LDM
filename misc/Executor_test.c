/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the `Executor` module.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "Executor.h"
#include "log.h"
#include "StopFlag.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

typedef struct {
    StopFlag stopFlag;
    bool     ran;
} Obj;

static void obj_init(Obj* const obj)
{
    obj->ran = false;
    CU_ASSERT_EQUAL(stopFlag_init(&obj->stopFlag, NULL), 0);
}

static void obj_destroy(Obj* const obj)
{
    stopFlag_destroy(&obj->stopFlag);
}

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

static int retNoResult(
        void* const restrict  arg,
        void** const restrict result)
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

static int noRet(
        void* const restrict  arg,
        void** const restrict result)
{
    pause();
    return 0;
}

static int runObj(
        void* const restrict  arg,
        void** const restrict result)
{
    Obj* obj = (Obj*)arg;

    obj->ran = true;

    return 0;
}

static int badRun(
        void* const restrict  arg,
        void** const restrict result)
{
    Obj* obj = (Obj*)arg;

    obj->ran = true;

    return 1;
}

static int
waitCondRetObj(
        void* const restrict  arg,
        void** const restrict result)
{
    Obj* const obj = (Obj*)arg;

    stopFlag_wait(&obj->stopFlag);

    *result = obj;

    return 0;
}

static int
cancelCondWait(
        void* const     arg,
        const pthread_t thread)
{
    Obj* const obj = (Obj*)arg;

    stopFlag_set(&obj->stopFlag);

    return 0;
}

static void test_construction(void)
{
    Executor* executor = executor_new();
    CU_ASSERT_PTR_NOT_NULL(executor);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_nullJob(void)
{
    Executor* executor = executor_new();
    CU_ASSERT_PTR_NOT_NULL(executor);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    Future* future = executor_submit(executor, NULL, retNoResult, NULL);
    CU_ASSERT_PTR_NOT_NULL(future);

    CU_ASSERT_EQUAL(future_getAndFree(future, NULL), 0);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_returnOne(void)
{
    Executor* executor = executor_new();
    CU_ASSERT_PTR_NOT_NULL(executor);

    Future* future = executor_submit(executor, NULL, retOne, NULL);
    CU_ASSERT_PTR_NOT_NULL(future);

    void* result = NULL;
    CU_ASSERT_EQUAL(future_getAndFree(future, &result), 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(result);
    CU_ASSERT_EQUAL(*(int*)result, 1);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_returnObj(void)
{
    int       obj = 2;

    Executor* executor = executor_new();
    CU_ASSERT_PTR_NOT_NULL(executor);

    Future* future = executor_submit(executor, &obj, retObj, NULL);
    CU_ASSERT_PTR_NOT_NULL(future);

    void* result;
    CU_ASSERT_EQUAL(future_getAndFree(future, &result), 0);
    CU_ASSERT_PTR_EQUAL(result, &obj);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_runObj(void)
{
    Obj obj;
    obj_init(&obj);

    Executor* executor = executor_new();
    Future*   future = executor_submit(executor, &obj, runObj, NULL);

    CU_ASSERT_EQUAL(future_getAndFree(future, NULL), 0);

    CU_ASSERT_TRUE(obj.ran);

    executor_free(executor);
    obj_destroy(&obj);
}

static void test_badRunStatus(void)
{
    Obj obj;
    obj_init(&obj);

    Executor* executor = executor_new();
    Future*   future = executor_submit(executor, &obj, badRun, NULL);

    CU_ASSERT_EQUAL(future_getResult(future, NULL), EPERM);
    CU_ASSERT_EQUAL(future_getRunStatus(future), 1);
    CU_ASSERT_EQUAL(future_free(future), 0);

    CU_ASSERT_TRUE(obj.ran);

    executor_free(executor);
    obj_destroy(&obj);
}

static void test_defaultCancel(void)
{
    Executor* executor = executor_new();
    Future*   future = executor_submit(executor, NULL, noRet, NULL);

    CU_ASSERT_EQUAL(future_cancel(future), 0);

    CU_ASSERT_EQUAL(future_getAndFree(future, NULL), ECANCELED);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_haltCancel(void)
{
    Obj obj;
    obj_init(&obj);

    Executor* executor = executor_new();
    Future*   future = executor_submit(executor, &obj, waitCondRetObj,
            cancelCondWait);

    CU_ASSERT_EQUAL(future_cancel(future), 0);

    CU_ASSERT_EQUAL(future_getAndFree(future, NULL), ECANCELED);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
    obj_destroy(&obj);
}

static void test_shutdown(void)
{
    Executor* executor = executor_new();
    Future*   future1 = executor_submit(executor, NULL, noRet, NULL);

    executor_shutdown(executor, false);

    int     obj = 3;
    Future* future2 = executor_submit(executor, &obj, retObj, NULL);
    CU_ASSERT_PTR_NULL(future2);
    log_clear();

    future_cancel(future1);
    CU_ASSERT_EQUAL(future_getAndFree(future1, NULL), ECANCELED);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static void test_shutdownNow(void)
{
    Executor* executor = executor_new();
    Future*   noRetFuture = executor_submit(executor, NULL, noRet, NULL);

    executor_shutdown(executor, true);

    int     obj = 3;
    Future* retObjFuture = executor_submit(executor, &obj, retObj, NULL);
    CU_ASSERT_PTR_NULL(retObjFuture);
    log_clear();

    CU_ASSERT_EQUAL(future_getAndFree(noRetFuture, NULL), ECANCELED);

    CU_ASSERT_EQUAL(executor_size(executor), 0);

    executor_free(executor);
}

static int afterCompObj = 3;

static int
afterComp(
        void* const restrict   arg,
        Future* const restrict future)
{
    CU_ASSERT_PTR_EQUAL(arg, &afterCompObj);
    CU_ASSERT_PTR_NOT_NULL(future);

    void* result;
    CU_ASSERT_EQUAL(future_getResult(future, &result), 0);
    CU_ASSERT_PTR_EQUAL(result, &afterCompObj);

    return 0;
}

static void test_afterCompletion(void)
{
    Executor* executor = executor_new();
    executor_setAfterCompletion(executor, &afterCompObj, afterComp);

    Future*   future = executor_submit(executor, &afterCompObj, retObj, NULL);
    CU_ASSERT_PTR_NOT_NULL(future);

    void* result;
    CU_ASSERT_EQUAL(future_getAndFree(future, &result), 0);
    CU_ASSERT_PTR_EQUAL(result, &afterCompObj);

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
                        && CU_ADD_TEST(testSuite, test_runObj)
                        && CU_ADD_TEST(testSuite, test_badRunStatus)
                        && CU_ADD_TEST(testSuite, test_defaultCancel)
                        && CU_ADD_TEST(testSuite, test_haltCancel)
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
