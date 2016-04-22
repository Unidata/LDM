/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the components of the NOAAPort Broadcast System.
 */

#include "config.h"

#include "dynabuf.h"
#include "gini.h"
#include "log.h"
#include "nbs_application.h"
#include "nbs_presentation.h"
#include "nbs_transport.h"
#include "pq.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <fcntl.h>
#include <limits.h>
#include <nbs_link.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static char* pathname = "SUPER-NATIONAL_8km_IR_20160422_1915.gini";

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

typedef struct {
    pqueue* pq;
    int     fd;
} recv_arg_t;

/**
 * Creates an NBS stack that receives NBS products from a file descriptor,
 * converts them into LDM data-products, and inserts them into an LDM
 * product-queue. Called by pthread_create().
 *
 * @param[in,out] arg  File descriptor and product-queue
 * @retval &0          Success
 */
void* start_recv_processing(void* const arg)
{
    recv_arg_t* recv_arg = arg;
    int       status;

    nbsa_t* nbsa;
    status = nbsa_new(&nbsa);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = nbsa_set_pq(nbsa, recv_arg->pq);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    nbsp_t* nbsp;
    status = nbsp_new(&nbsp);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = nbsp_set_application_layer(nbsp, nbsa);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    nbst_t* nbst;
    status = nbst_new(&nbst);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = nbst_set_presentation_layer(nbst, nbsp);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    nbsl_t* nbsl;
    status = nbsl_new(&nbsl);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = nbsl_set_transport_layer(nbsl, nbst);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = nbsl_set_recv_file_descriptor(nbsl, recv_arg->fd);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = nbsl_execute(nbsl);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    nbsl_free(nbsl);
    nbst_free(nbst);
    nbsp_free(nbsp);
    nbsa_free(nbsa);

    log_free();

    static int zero;
    return &zero;
}

/**
 * Starts a thread for receiving NBS products and inserting them into an LDM
 * product-queue.
 *
 * @param[in]  recv_arg  Argument for start_up_processing(). Must persist.
 * @param[out] thread    Thread on which processing occurs
 * @retval 0             Success
 */
static int exec_recv_processing(
        recv_arg_t* const restrict recv_arg,
        pthread_t* const restrict  thread)
{
    int status = pthread_create(thread, NULL, start_recv_processing, recv_arg);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    return status;
}

/**
 * Returns a GINI image.
 *
 * @retval  GINI image
 */
static gini_t* get_gini_in(dynabuf_t* const dynabuf)
{
    int fd = open(pathname, O_RDONLY);
    CU_ASSERT_NOT_EQUAL_FATAL(fd, -1);

    struct stat finfo;
    int status = fstat(fd, &finfo);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    uint8_t* data = mmap(NULL, finfo.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(data);

    gini_t* gini;
    status = gini_new(&gini, dynabuf);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = gini_deserialize(gini, data, finfo.st_size);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = close(fd);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    return gini;
}

/**
 * Sends a GINI image via an NBS stack.
 *
 * @param[in]     gini  GINI image
 * @param[in,out] fd    File descriptor
 * @retval 0            Success
 */
static int send_gini(
        gini_t* const restrict gini,
        const int              fd)
{
    int status;

    nbsl_t* nbsl;
    status = nbsl_new(&nbsl);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = nbsl_set_send_file_descriptor(nbsl, fd);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    nbst_t* nbst;
    status = nbst_new(&nbst);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = nbst_set_link_layer(nbst, nbsl);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    nbsp_t* nbsp;
    status = nbsp_new(&nbsp);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = nbsp_set_transport_layer(nbsp, nbst);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = nbsp_send_gini(nbsp, gini);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    nbsp_free(nbsp);
    nbst_free(nbst);
    nbsl_free(nbsl);

    return status;
}

/**
 * Tests transferring a GINI image using two NBS stacks.
 */
static void test_gini(
        void)
{
    int fds[2];
    int status = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    static char pq_pathname[] = "nbs_test.pq";
    pqueue* pq;
    status = pq_create(pq_pathname, 0666, 0, 0, 5000000, 50, &pq);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(pq);

    pthread_t thread;
    recv_arg_t  recv_arg;
    recv_arg.pq = pq;
    recv_arg.fd = fds[0];
    status = exec_recv_processing(&recv_arg, &thread);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    dynabuf_t* dynabuf;
    status = dynabuf_new(&dynabuf, NBS_MAX_FRAME_SIZE);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    gini_t* const gini_in = get_gini_in(dynabuf);
    CU_ASSERT_PTR_NOT_NULL_FATAL(gini_in);

    status = send_gini(gini_in, fds[1]);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    //sleep(UINT_MAX);
    status = shutdown(fds[0], 0); // close() can result in unread frames
    CU_ASSERT_EQUAL_FATAL(status, 0);

    gini_free(gini_in);
    dynabuf_free(dynabuf);

    void* value_ptr;
    status = pthread_join(thread, &value_ptr);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_EQUAL_FATAL(*(int*)value_ptr, 0);

    (void)pq_close(pq);
    //(void)unlink(pq_pathname);
    (void)close(fds[0]);
    (void)close(fds[1]);
}

int main(
        int argc,
        char* const * argv)
{
    int exitCode = 1;
    log_init(argv[0]);
    //log_set_level(LOG_LEVEL_DEBUG);

    if (argv[1])
        pathname = argv[1];

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_gini)) {
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
