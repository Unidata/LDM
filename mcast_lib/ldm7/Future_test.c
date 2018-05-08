/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the `Future` module.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "log.h"
#include "Future.h"
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

static Obj            obj;
static pthread_attr_t threadAttr;

static void obj_init(Obj* const obj)
{
    obj->ran = false;
    CU_ASSERT_EQUAL(stopFlag_init(&obj->stopFlag), 0);
}

static void obj_deinit(Obj* const obj)
{
    stopFlag_deinit(&obj->stopFlag);
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
    int status = pthread_attr_init(&threadAttr);

    if (status == 0) {
        status = pthread_attr_setdetachstate(&threadAttr,
                PTHREAD_CREATE_DETACHED);

        if (status == 0)
            status = setSigTermHandler();
    }

    return status;
}

/**
 * Only called once.
 */
static int teardown(void)
{
    return 0;
}

static void*
runRetObj(void* const arg)
{
    Obj* const obj = (Obj*)arg;

    obj->ran = true;

    return obj;
}

static bool
trivialCancel(
        void* const     arg,
        const pthread_t thread)
{
    return true;
}

static void*
runCondWait(void* const arg)
{
    Obj* const obj = (Obj*)arg;

    stopFlag_wait(&obj->stopFlag);

    return NULL;
}

static bool
cancelCondWait(
        void* const     arg,
        const pthread_t thread)
{
    Obj* const obj = (Obj*)arg;

    stopFlag_set(&obj->stopFlag);

    return true;
}

static void*
runPause(void* const arg)
{
    pause();
    return NULL;
}

static void test_initialization(void)
{
    Obj obj;
    obj_init(&obj);

    Future* const future = future_new(&obj, runRetObj, trivialCancel);
    CU_ASSERT_PTR_NOT_NULL_FATAL(future);
    CU_ASSERT_EQUAL(future_delete(future), 0);
    CU_ASSERT_FALSE(obj.ran);

    obj_deinit(&obj);
}

static void* runFuture(void* const arg)
{
    Future* const future = (Future*)arg;
    CU_ASSERT_EQUAL(future_run(future), 0);
    log_free();
    return NULL;
}

static void test_execution(void)
{
    Obj obj;
    obj_init(&obj);

    Future* const future = future_new(&obj, runRetObj, trivialCancel);
    CU_ASSERT_PTR_NOT_NULL_FATAL(future);

    pthread_t thread;
    CU_ASSERT_EQUAL(pthread_create(&thread, &threadAttr, runFuture, future), 0);

    void* result;
    CU_ASSERT_EQUAL(future_wait(future, &result), 0);
    CU_ASSERT_PTR_EQUAL(result, &obj);
    CU_ASSERT_TRUE(obj.ran);

    CU_ASSERT_EQUAL(future_delete(future), 0);

    obj_deinit(&obj);
}

static void test_cancellation(void)
{
    Obj obj;
    obj_init(&obj);

    Future* const future = future_new(&obj, runCondWait, cancelCondWait);
    CU_ASSERT_PTR_NOT_NULL_FATAL(future);

    pthread_t thread;
    CU_ASSERT_EQUAL(pthread_create(&thread, &threadAttr, runFuture, future), 0);

    CU_ASSERT_TRUE(future_cancel(future));

    void* result;
    CU_ASSERT_EQUAL(future_wait(future, &result), ECANCELED);
    CU_ASSERT_FALSE(obj.ran);

    CU_ASSERT_EQUAL(future_delete(future), 0);

    obj_deinit(&obj);
}

static void test_defaultCancellation(void)
{
    Obj obj;
    obj_init(&obj);

    Future* const future = future_new(&obj, runPause, NULL);
    CU_ASSERT_PTR_NOT_NULL_FATAL(future);

    pthread_t thread;
    CU_ASSERT_EQUAL(pthread_create(&thread, &threadAttr, runFuture, future), 0);

    CU_ASSERT_TRUE(future_cancel(future));

    void* result;
    CU_ASSERT_EQUAL(future_wait(future, &result), ECANCELED);
    CU_ASSERT_FALSE(obj.ran);

    CU_ASSERT_EQUAL(future_delete(future), 0);

    obj_deinit(&obj);
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
                if (CU_ADD_TEST(testSuite, test_initialization)
                        && CU_ADD_TEST(testSuite, test_execution)
                        && CU_ADD_TEST(testSuite, test_cancellation)
                        && CU_ADD_TEST(testSuite, test_defaultCancellation)
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
