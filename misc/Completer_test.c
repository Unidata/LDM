/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the `Completer` module.
 *
 * @author: Steven R. Emmerson
 */

#include "../../misc/Completer.h"

#include "config.h"

#include "log.h"
#include "Thread.h"

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

static int trivialRun(void* const arg)
{
    return 0;
}

static int retNull(
        void* const restrict  arg,
        void** const restrict result)
{
    if (result)
        *result = NULL;
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
    *result = arg;
    return 0;
}

static int noRet(void* const restrict  arg)
{
    pause();
    return 0;
}

typedef struct pauseTask {
    pthread_mutex_t       mutex;
    pthread_cond_t        cond;
    bool                  cue;
} PauseTask;

static void
pauseTask_init(PauseTask* const task)
{
    task->cue = false;
    CU_ASSERT_EQUAL(mutex_init(&task->mutex, PTHREAD_MUTEX_ERRORCHECK, true),
            0);
    CU_ASSERT_EQUAL(pthread_cond_init(&task->cond, NULL), 0);
}

static void
pauseTask_destroy(PauseTask* const task)
{
    CU_ASSERT_EQUAL(pthread_cond_destroy(&task->cond), 0);
    CU_ASSERT_EQUAL(pthread_mutex_destroy(&task->mutex), 0);
}

static int
pauseTask_run(void* const  arg)
{
    PauseTask* task = (PauseTask*)arg;

    CU_ASSERT_EQUAL(pthread_mutex_lock(&task->mutex), 0);
        while (!task->cue)
            CU_ASSERT_EQUAL(pthread_cond_wait(&task->cond, &task->mutex), 0);
    CU_ASSERT_EQUAL(pthread_mutex_unlock(&task->mutex), 0);

    return 0;
}

static int
pauseTask_cancel(
        void* const     arg,
        const pthread_t thread)
{
    PauseTask* task = (PauseTask*)arg;

    CU_ASSERT_EQUAL(pthread_mutex_lock(&task->mutex), 0);
        task->cue = true;
        CU_ASSERT_EQUAL(pthread_cond_signal(&task->cond), 0);
    CU_ASSERT_EQUAL(pthread_mutex_unlock(&task->mutex), 0);

    return 0;
}

static void test_construction(void)
{
    Completer* comp = completer_new();

    CU_ASSERT_PTR_NOT_NULL(comp);

    completer_free(comp);
}

static void test_nullJob(void)
{
    Completer* comp = completer_new();

    Future* submitFuture = completer_submit(comp, NULL, trivialRun, NULL,
            retNull);
    CU_ASSERT_PTR_NOT_NULL(submitFuture);

    Future* takeFuture = completer_take(comp);
    CU_ASSERT_PTR_EQUAL(takeFuture, submitFuture);

    CU_ASSERT_EQUAL(future_getAndFree(takeFuture, NULL), 0);

    completer_free(comp);
}

static void test_returnOne(void)
{
    Completer* completer = completer_new();

    (void)completer_submit(completer, NULL, trivialRun, NULL, retOne);
    Future* future = completer_take(completer);

    void* result;
    CU_ASSERT_EQUAL(future_getAndFree(future, &result), 0);
    CU_ASSERT_PTR_NOT_NULL(result);
    CU_ASSERT_EQUAL(*(int*)result, 1);

    completer_free(completer);
}

static void test_returnObj(void)
{
    int obj = 2;

    Completer* completer = completer_new();

    (void)completer_submit(completer, &obj, trivialRun, NULL, retObj);
    Future*    future = completer_take(completer);

    void* result;
    CU_ASSERT_EQUAL(future_getAndFree(future, &result), 0);
    CU_ASSERT_PTR_EQUAL(result, &obj);

    completer_free(completer);
}

static void test_cancelFuture(void)
{
    Completer* completer = completer_new();

    Future* submitFuture = completer_submit(completer, NULL, noRet, NULL, NULL);

    CU_ASSERT_EQUAL(future_cancel(submitFuture), 0);

    Future* takeFuture = completer_take(completer);
    CU_ASSERT_PTR_EQUAL(takeFuture, submitFuture);
    CU_ASSERT_EQUAL(future_getAndFree(takeFuture, NULL), ECANCELED);

    completer_free(completer);
}

static void test_shutdown(void)
{
    Completer* completer = completer_new();
    Future*    noRetFuture = completer_submit(completer, NULL, noRet, NULL,
            NULL);
    CU_ASSERT_PTR_NOT_NULL(noRetFuture);

    CU_ASSERT_EQUAL(completer_shutdown(completer, false), 0);

    int     obj = 3;
    Future* retObjFuture = completer_submit(completer, &obj, trivialRun, NULL,
            retObj);
    CU_ASSERT_PTR_NULL(retObjFuture);
    log_clear();

    future_cancel(noRetFuture);

    Future* takeFuture = completer_take(completer);
    CU_ASSERT_PTR_EQUAL(takeFuture, noRetFuture);

    CU_ASSERT_EQUAL(future_getAndFree(takeFuture, NULL), ECANCELED);

    completer_free(completer);
}

static void test_shutdownNow(void)
{
    PauseTask pauseTask;
    pauseTask_init(&pauseTask);

    Completer* completer = completer_new();
    Future*    noRetFuture = completer_submit(completer, &pauseTask,
            pauseTask_run, pauseTask_cancel, NULL);
    CU_ASSERT_PTR_NOT_NULL(noRetFuture);

    CU_ASSERT_EQUAL(completer_shutdown(completer, true), 0);

    int     obj = 3;
    Future* retObjFuture = completer_submit(completer, &obj, trivialRun, NULL,
            retObj);
    CU_ASSERT_PTR_NULL(retObjFuture);
    log_clear();

    Future* takeFuture = completer_take(completer);
    CU_ASSERT_PTR_EQUAL(takeFuture, noRetFuture);

    CU_ASSERT_EQUAL(future_getAndFree(takeFuture, NULL), ECANCELED);

    completer_free(completer);

    pauseTask_destroy(&pauseTask);
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
        log_set_level(LOG_LEVEL_DEBUG);

        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_construction)
                        && CU_ADD_TEST(testSuite, test_nullJob)
                        && CU_ADD_TEST(testSuite, test_returnOne)
                        && CU_ADD_TEST(testSuite, test_returnObj)
                        && CU_ADD_TEST(testSuite, test_cancelFuture)
                        && CU_ADD_TEST(testSuite, test_shutdown)
                        && CU_ADD_TEST(testSuite, test_shutdownNow)
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
