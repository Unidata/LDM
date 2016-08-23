/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: pq_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests the product-queue API.
 */

#include "config.h"

#include "ldm.h"
#include "ldm_xlen.h"
#include "ldmprint.h"
#include "limits.h"
#include "log.h"
#include "pq.h"
#include "stdbool.h"
#include "xdr.h"

#include <errno.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#define               PQ_PATHNAME   "pq_test.pq"
#define               NUM_PRODS            50000
#define               MAX_PROD_SIZE       200000
#define               PQ_DATA_SIZE     100000000
#define               PQ_SLOT_COUNT         1000
#define               NUM_CHILDREN             3
static unsigned long  num_bytes;
static struct timeval start;
static struct timeval stop;

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

static pqueue* create_pq(void)
{
    pqueue* pq;
    int     status = pq_create(PQ_PATHNAME, 0600, 0, 0, PQ_DATA_SIZE,
            PQ_SLOT_COUNT, &pq);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    return pq;
}

static pqueue* open_pq(
        const bool for_writing)
{
    pqueue* pq;
    int     status = pq_open(PQ_PATHNAME, for_writing ? 0 : PQ_READONLY, &pq);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    return pq;
}

static void close_pq(
        pqueue* const pq)
{
    int status = pq_close(pq);
    CU_ASSERT_EQUAL_FATAL(status, 0);
}

/**
 * Returns the time interval between two times.
 *
 * @param[in] later    The later time
 * @param[in] earlier  The earlier time.
 * @return             The time interval, in seconds, between the two times.
 */
static double duration(
    const struct timeval*   later,
    const struct timeval*   earlier)
{
    return (later->tv_sec - earlier->tv_sec) +
        1e-6*(later->tv_usec - earlier->tv_usec);
}

static int insert_prod_reserve_no_sig(
        pqueue* const restrict  pq,
        product* const restrict prod)
{
    char*     space;
    pqe_index pqe_index;
    size_t    extent = xlen_product(prod);
    int       status = pqe_newDirect(pq, extent, NULL, &space, &pqe_index);
    if (status) {
        log_add("Couldn't reserve space for product");
    }
    else {
        XDR xdrs ;
        xdrmem_create(&xdrs, space, extent, XDR_ENCODE);
        if (!xdr_product(&xdrs, prod)) {
            log_error("xdr_product() failed");
            (void)pqe_discard(pq, pqe_index);
            status = -1;
        }
        else {
            status = pqe_insert(pq, pqe_index);
            if (status)
                log_error("pqe_insert() failed");
        }
    }
    return status;
}

static int insert_prod(
        pqueue* const restrict  pq,
        product* const restrict prod)
{
    int status = pq_insert(pq, prod);
    if (status == PQ_DUP) {
        log_add("Duplicate data-product");
        status = 0;
    }
    return status;
}

static int insert_prod_after_delay(
        pqueue* const restrict  pq,
        product* const restrict prod)
{
    struct timespec duration;
    duration.tv_sec = 0;
    duration.tv_nsec = 1000000; // 1 ms
    int status = nanosleep(&duration, NULL);
    CU_ASSERT_FATAL(status == 0 || errno == EINTR);
    status = pq_insert(pq, prod);
    if (status == PQ_DUP) {
        log_add("Duplicate data-product");
        status = 0;
    }
    return status;
}

static int insert_products(
        pqueue* const pq,
        int         (*insert)(pqueue* restrict pq, product* restrict prod))
{
    static char    data[MAX_PROD_SIZE];
    int            status;
    product        prod;
    prod_info*     info = &prod.info;
    char           ident[80];
    unsigned short xsubi[3] = {(unsigned short)1234567890,
                               (unsigned short)9876543210,
                               (unsigned short)1029384756};
    info->feedtype = EXP;
    info->ident = ident;
    info->origin = "localhost";
    (void)memset(info->signature, 0, sizeof(info->signature));
    prod.data = data;

    num_bytes = 0;
    (void)gettimeofday(&start, NULL);

    for (int i = 0; i < NUM_PRODS; i++) {
        const unsigned size = sizeof(data)*erand48(xsubi) + 0.5;
        const ssize_t  nbytes = snprintf(ident, sizeof(ident), "%d", i);

        CU_ASSERT_TRUE_FATAL(nbytes >= 0 && nbytes < sizeof(ident));
        status = set_timestamp(&info->arrival);
        CU_ASSERT_EQUAL_FATAL(status, 0);
        info->seqno = i;
        uint32_t signet = htonl(i); // decoded in `requester_decide()`
        (void)memcpy(info->signature+sizeof(signaturet)-sizeof(signet), &signet,
                sizeof(signet));
        info->sz = size;

        status = insert(pq, &prod);
        if (status) {
            log_add("Couldn't insert data-product %d into product-queue", i);
            break;
        }
        char buf[LDM_INFO_MAX];
        log_notice("Inserted: prodInfo=\"%s\"",
                s_prod_info(buf, sizeof(buf), info, 1));
        num_bytes += size;
    }
    (void)gettimeofday(&stop, NULL);

    return status;
}

static int read_prod(
        const prod_info* const restrict info,
        const void* const restrict      data,
        void* const restrict            xprod,
        const size_t                    size,
        void* const restrict            arg)
{
    bool* const done = arg;
    *done = info->seqno == NUM_PRODS - 1;
    return 0;
}

static int read_products(
        pqueue* const pq)
{
    int status = 0;

    for (bool done = false; !done;) {
        status = pq_sequence(pq, TV_GT, PQ_CLASS_ALL, read_prod, &done);
        if (status == PQUEUE_END) {
            (void)pq_suspend(5); // Unblocks SIGCONT
        }
        else {
            CU_ASSERT_EQUAL_FATAL(status, 0);
        }
    }

    return status;
}

static int create_insert_close(
        void)
{
    pqueue* pq = create_pq();
    int status = insert_products(pq, insert_prod);
    close_pq(pq);
    return status;
}

static void test_pq_insert(
        void)
{
    pqueue* const pq = create_pq();
    CU_ASSERT_NOT_EQUAL_FATAL(pq, NULL);
    int status = insert_products(pq, insert_prod);
    CU_ASSERT_EQUAL(status, 0);
    double dur = duration(&stop, &start);
    log_notice("Elapsed time       = %g s", dur);
    log_notice("Number of bytes    = %lu", num_bytes);
    log_notice("Number of products = %lu", NUM_PRODS);
    log_notice("Mean product size  = %lu", num_bytes / NUM_PRODS);
    log_notice("Product rate       = %g/s", NUM_PRODS/dur);
    log_notice("Byte rate          = %g/s", num_bytes/dur);
    log_notice("Bit rate           = %g/s", CHAR_BIT*num_bytes/dur);
    close_pq(pq);
}

static void test_pq_insert_reserve_no_sig(
        void)
{
    pqueue* const pq = create_pq();
    CU_ASSERT_NOT_EQUAL_FATAL(pq, NULL);
    int status = insert_products(pq, insert_prod_reserve_no_sig);
    CU_ASSERT_EQUAL(status, 0);
    double dur = duration(&stop, &start);
    log_notice("Elapsed time       = %g s", dur);
    log_notice("Number of bytes    = %lu", num_bytes);
    log_notice("Number of products = %lu", NUM_PRODS);
    log_notice("Mean product size  = %lu", num_bytes / NUM_PRODS);
    log_notice("Product rate       = %g/s", NUM_PRODS/dur);
    log_notice("Byte rate          = %g/s", num_bytes/dur);
    log_notice("Bit rate           = %g/s", CHAR_BIT*num_bytes/dur);
    close_pq(pq);
}

static void test_pq_insert_children(void)
{
    int status;
    pqueue* pq = create_pq();
    CU_ASSERT_NOT_EQUAL_FATAL(pq, NULL);
    close_pq(pq);

    for (int i = 0; i < NUM_CHILDREN; i++) {
        int pid = fork();
        CU_ASSERT_NOT_EQUAL(pid, -1);
        if (pid == 0) {
            pq = open_pq(true);
            status = insert_products(pq, insert_prod);
            CU_ASSERT_EQUAL(status, 0);
            close_pq(pq);
            exit(0);
        }
    }

    for (;;) {
        int child_status;
        status = wait(&child_status);
        if (status == -1 && errno == ECHILD)
            break; // No unwaited-for child processes
        CU_ASSERT_NOT_EQUAL_FATAL(status, -1);
        CU_ASSERT_TRUE(WIFEXITED(child_status));
        CU_ASSERT_EQUAL(WEXITSTATUS(child_status), 0);
    }
}

static void test_pq_sequence(void)
{
    pqueue* pq = create_pq();
    CU_ASSERT_PTR_NOT_NULL_FATAL(pq);
    close_pq(pq);

    int status;
    int pid = fork();
    CU_ASSERT_NOT_EQUAL(pid, -1);
    if (pid == 0) {
        pq = open_pq(true);
        status = insert_products(pq, insert_prod_after_delay);
        CU_ASSERT_EQUAL(status, 0);
        close_pq(pq);
        exit(0);
    }
    pq = open_pq(false);
    CU_ASSERT_PTR_NOT_NULL_FATAL(pq);
    status = read_products(pq);
    CU_ASSERT_EQUAL(status, 0);
    close_pq(pq);

    int child_status;
    status = wait(&child_status);
    CU_ASSERT_EQUAL(status, pid);
    CU_ASSERT_TRUE(WIFEXITED(child_status));
    CU_ASSERT_EQUAL(WEXITSTATUS(child_status), 0);
}

int main(
        const int          argc,
        const char* const* argv)
{
    int exitCode = 1;

    if (log_init(argv[0])) {
        (void) fprintf(stderr, "Couldn't initialize logging\n");
        exitCode = 1;
    }
    else {
        log_set_destination("pq_test.out");
        if (CUE_SUCCESS == CU_initialize_registry()) {
            CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

            if (NULL != testSuite) {
                if (//CU_ADD_TEST(testSuite, test_pq_insert_reserve_no_sig) &&
                        CU_ADD_TEST(testSuite, test_pq_insert) &&
                        CU_ADD_TEST(testSuite, test_pq_sequence) &&
                        CU_ADD_TEST(testSuite, test_pq_insert_children)
                        ) {
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
