/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: frame_queue_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the frame queue module (frame_queue.c).
 */

#include "config.h"

#include "log.h"
#include "frame_queue.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

#if 1
#define MAX_FRAME_SIZE   5000
#define NUM_QUEUE_FRAMES  200
#define NUM_FILLS         500
#else
#define MAX_FRAME_SIZE      1
#define NUM_QUEUE_FRAMES    1
#define NUM_FILLS           1
#endif
#define QUEUE_SIZE       (NUM_QUEUE_FRAMES*MAX_FRAME_SIZE)

static unsigned short        xsubi[3] = {(unsigned short)1234567890,
                                         (unsigned short)9876543210,
                                         (unsigned short)1029384756};
static fq_t*                 fq;

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

static void test_fq_new(void)
{
    fq = NULL;
    int   status = fq_new(&fq, QUEUE_SIZE);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_PTR_NOT_NULL(fq);
    fq_free(fq);
}

static void test_fq_shutdown(void)
{
    fq = NULL;
    int   status = fq_new(&fq, QUEUE_SIZE);
    CU_ASSERT_EQUAL(status, 0);
    CU_ASSERT_PTR_NOT_NULL(fq);

    status = fq_shutdown(NULL);
    CU_ASSERT_EQUAL(status, FQ_STATUS_INVAL);

    status = fq_shutdown(fq);
    CU_ASSERT_EQUAL(status, 0);

    uint8_t*      data = NULL;
    unsigned      nread = 0;
    status = fq_peek(fq, &data, &nread);
    CU_ASSERT_EQUAL(status, FQ_STATUS_SHUTDOWN);

    fq_free(fq);
}

static void test_write_then_read(void)
{
    fq = NULL;
    int   status = fq_new(&fq, QUEUE_SIZE);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(fq);

    for (int n = 0; n < NUM_FILLS*NUM_QUEUE_FRAMES; ) {
        const unsigned nwrite = MAX_FRAME_SIZE*erand48(xsubi) + 0.5;
        if (nwrite) {
            uint8_t*            write_data = NULL;
            // log_notice_q("Writing %u-byte frame", nwrite);
            status = fq_reserve(fq, &write_data, nwrite);
            CU_ASSERT_EQUAL(status, 0);
            CU_ASSERT_PTR_NOT_NULL_FATAL(write_data);
            int j;
            for (j = 0; j < nwrite; j++)
                write_data[j] = n % UCHAR_MAX;
            status = fq_release(fq, nwrite);
            CU_ASSERT_EQUAL(status, 0);

            uint8_t*      read_data = NULL;
            unsigned      nread = 0;
            status = fq_peek(fq, &read_data, &nread);
            CU_ASSERT_EQUAL(status, 0);
            CU_ASSERT_EQUAL(nread, nwrite);
            CU_ASSERT_PTR_EQUAL(read_data, write_data);
            for (j = 0; j < nread && read_data[j] == n % UCHAR_MAX; j++)
                ;
            CU_ASSERT_EQUAL(j, nread);
            status = fq_remove(fq);
            CU_ASSERT_EQUAL(status, 0);

            n++;
        }
    }

    fq_free(fq);
}

static void* start_writer(void* barrier)
{
    int status = pthread_barrier_wait(barrier);
    CU_ASSERT_FATAL(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);

    struct timespec sleepInterval;
    sleepInterval.tv_sec = 0;

    uint8_t* data;
    static   int n = 0;
    while (n < NUM_FILLS*NUM_QUEUE_FRAMES) {
        const unsigned nwrite = MAX_FRAME_SIZE*erand48(xsubi) + 0.5;
        // log_notice_q("Writing %u-byte frame", nwrite);
        data = NULL;
        status = fq_reserve(fq, &data, nwrite);
        CU_ASSERT_EQUAL(status, 0);
        CU_ASSERT_TRUE((nwrite && data) || (nwrite == 0 && data == NULL));

        int j;
        for (j = 0; j < nwrite; j++)
            data[j] = n % UCHAR_MAX;

        status = fq_release(fq, nwrite);
        CU_ASSERT_EQUAL(status, 0);

        if (nwrite)
            n++;
    }

    status = fq_shutdown(fq);
    CU_ASSERT_EQUAL(status, 0);

    return &n; // Number of frames written
}

static void* start_reader(void* barrier)
{
    int status = pthread_barrier_wait(barrier);
    CU_ASSERT_FATAL(status == 0 || status == PTHREAD_BARRIER_SERIAL_THREAD);

    static int n = 0;
    do {
        unsigned nread = 0;
        uint8_t* data = NULL;
        status = fq_peek(fq, &data, &nread);
        if (status == 0) {
            int j;
            for (j = 0; j < nread && data[j] == n % UCHAR_MAX; j++)
                ;
            CU_ASSERT_EQUAL(j, nread);
            status = fq_remove(fq);
            CU_ASSERT_EQUAL(status, 0);
            n++;
        }
    } while (status == 0);
    CU_ASSERT_EQUAL(status, FQ_STATUS_SHUTDOWN);

    return &n; // Number of frames read
}

static void test_concurrency(void)
{
    fq = NULL;
    int   status = fq_new(&fq, QUEUE_SIZE);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(fq);

    pthread_barrier_t barrier;
    status = pthread_barrier_init(&barrier, NULL, 2);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    pthread_t writer;
    status = pthread_create(&writer, NULL, start_writer, &barrier);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    pthread_t reader;
    status = pthread_create(&reader, NULL, start_reader, &barrier);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    void* result;
    status = pthread_join(writer, &result);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    int nwritten = *(int*)result;

    status = pthread_join(reader, &result);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    int nread = *(int*)result;

    CU_ASSERT_EQUAL(nread, nwritten);

    status = pthread_barrier_destroy(&barrier);
    CU_ASSERT_EQUAL(status, 0);

    fq_free(fq);
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1; // Failure default
    int status = log_init(argv[0]);

    if (status) {
        (void) fprintf(stderr, "Couldn't initialize logging module");
    }
    else {
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (CU_ADD_TEST(testSuite, test_fq_new) &&
                        CU_ADD_TEST(testSuite, test_fq_shutdown) &&
                        CU_ADD_TEST(testSuite, test_write_then_read) &&
                        CU_ADD_TEST(testSuite, test_concurrency)) {
                    CU_basic_set_mode(CU_BRM_VERBOSE);
                    (void) CU_basic_run_tests();
                }
            }

            exitCode = CU_get_number_of_tests_failed();
            CU_cleanup_registry();
        }
    }

    log_fini();
    return exitCode;
}
