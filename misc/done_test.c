/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file tests the assumption that `pthread_cond_wait()` can be interrupted
 * by a signal.
 *
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "log.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <unistd.h>

static pthread_mutex_t       mutex;
static pthread_cond_t        cond;
static pthread_barrier_t     barrier;
static volatile sig_atomic_t done = 0;

/**
 * Only called once.
 */
static int setup(void)
{
    int status = pthread_mutex_init(&mutex, NULL);

    if (status == 0)
        status = pthread_cond_init(&cond, NULL);

    if (status == 0)
        status = pthread_barrier_init(&barrier, NULL, 2);

    return status;
}

/**
 * Only called once.
 */
static int teardown(void)
{
    int status = pthread_barrier_destroy(&barrier);

    if (status == 0)
        pthread_cond_destroy(&cond);

    if (status == 0)
        status = pthread_mutex_destroy(&mutex);

    return status;
}

static void signal_handler(int sig)
{
    log_notice("Done");
    done = 1;
}

static void* start(void* const arg)
{
    CU_ASSERT_EQUAL(pthread_mutex_lock(&mutex), 0);
        while (!done) {
            static bool synced = false;

            if (!synced) {
                synced = true;
                int status = pthread_barrier_wait(&barrier);
                CU_ASSERT_TRUE(status == PTHREAD_BARRIER_SERIAL_THREAD ||
                        status == 0);
            }

            CU_ASSERT_EQUAL(pthread_cond_wait(&cond, &mutex), 0);
        }
    CU_ASSERT_EQUAL(pthread_mutex_unlock(&mutex), 0);

    return NULL;
}

static void test_done(void)
{
    struct sigaction sigact;

    (void)sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = signal_handler;
    (void)sigaction(SIGTERM, &sigact, NULL);

    pthread_t thread;
    CU_ASSERT_EQUAL(pthread_create(&thread, NULL, start, NULL), 0);

    int status = pthread_barrier_wait(&barrier);
    CU_ASSERT_TRUE(status == PTHREAD_BARRIER_SERIAL_THREAD || status == 0);

    CU_ASSERT_EQUAL(pthread_mutex_lock(&mutex), 0);
        CU_ASSERT_EQUAL(pthread_kill(thread, SIGTERM), 0);
    CU_ASSERT_EQUAL(pthread_mutex_unlock(&mutex), 0);

    void* ptr;
    CU_ASSERT_EQUAL(pthread_join(thread, &ptr), 0);
}

int main(
        const int argc,
        const char* const * argv)
{
    int         exitCode = 1;
    const char* progname = basename((char*) argv[0]);

    if (log_init(progname)) {
        (void) fprintf(stderr, "Couldn't open logging system\n");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_done)
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
