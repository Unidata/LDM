/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: pipe_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the socketpair() function and stream pipes.
 */

#include "config.h"

#include "log.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

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

static char buf[8192];

static void* read_from_fd(void* arg)
{
    int fd = *(int*)arg;

    //for (int n = 1; n <= sizeof(buf); n <<= 1) {
    for (;;) {
        int nbytes = read(fd, buf, sizeof(buf));
        if (nbytes <= 0) {
            log_syserr_q("read() failure");
            break;
        }
        (void)printf("Read %d bytes\n", nbytes);
    }
    return NULL;
}

static void test_socketpair(
        void)
{
    int fds[2];
    int status = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    pthread_t thread;
    status = pthread_create(&thread, NULL, read_from_fd, fds);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    for (int n = 1; n <= sizeof(buf); n <<= 1) {
        (void)printf("Writing %d bytes\n", n);
        status = write(fds[1], buf, n); // Can't be fsync()ed
        CU_ASSERT_EQUAL_FATAL(status, n);
    }

    /*
     * Closing fds[1] may cause read_from_fd() to return before last record is
     * read because the file-descriptor is no longer valid.
     */
    status = shutdown(fds[0], 0);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = pthread_join(thread, NULL);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    close(fds[0]);
    close(fds[1]);
}

int main(
        const int argc,
        const char* const * argv)
{
    int exitCode = 1;
    log_init(argv[0]);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_socketpair)) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        exitCode = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    log_fini();
    return exitCode;
}
