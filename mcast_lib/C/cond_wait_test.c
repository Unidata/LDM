/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: cond_wait_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests various aspects of `pthread_cond_wait()`.
 */



#include "config.h"

#include <log.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

static pthread_mutex_t       mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t        cond = PTHREAD_COND_INITIALIZER;
static volatile sig_atomic_t done;
static volatile sig_atomic_t sigHandlerCalled;

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

static void* signalCond(
        void* const arg)
{
    pthread_t* const thread = (pthread_t*)arg;
    int              status = pthread_mutex_lock(&mutex);
    log_assert(status == 0);

    // Try a signal first. This doesn't work
    status = pthread_kill(*thread, SIGINT);
    log_assert(status == 0);

    sleep(1);

    done = 1;

    status = pthread_cond_signal(&cond);
    log_assert(status == 0);

    status = pthread_mutex_unlock(&mutex);
    log_assert(status == 0);

    return NULL;
}

static void sigHandler(
        int sig)
{
    sigHandlerCalled = 1;
}

static void test_cond_wait(void)
{
    sighandler_t prevHandler = signal(SIGINT, sigHandler);
    CU_ASSERT_NOT_EQUAL_FATAL(prevHandler, SIG_ERR);

    int status = pthread_mutex_lock(&mutex);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    pthread_t thread;
    status = pthread_create(&thread, NULL, signalCond, &thread);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    while (!done) {
        status = pthread_cond_wait(&cond, &mutex);
        CU_ASSERT_EQUAL_FATAL(status, 0);
        // NB: pthread_cond_wait() doesn't return due to the pthread_kill().
        CU_ASSERT_TRUE(done);
    }

    CU_ASSERT_TRUE(sigHandlerCalled);

    status = pthread_join(thread, NULL);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = pthread_mutex_unlock(&mutex);
    CU_ASSERT_EQUAL_FATAL(status, 0);
}

int main(
        const int          argc,
        const char* const* argv)
{
    int exitCode = 1;

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_cond_wait)) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void)CU_basic_run_tests();
            }
        }

        exitCode = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    return exitCode;
}
