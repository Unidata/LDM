/**
 * Copyright 2020 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: Down7_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests a downstream LDM-7.
 */
#include "config.h"

#include "Up7Down7_lib.h"

#include "down7.h"
#include "Executor.h"
#include "globals.h"
#include "ldmprint.h"
#include "log.h"
#include "prod_index_map.h"
#include "Thread.h"

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <unistd.h>

#ifndef MAX
    #define MAX(a,b) ((a) >= (b) ? (a) : (b))
#endif

typedef struct {
    pthread_mutex_t mutex;
    bool            done;
} Requester;

typedef struct {
    Requester             requester;
    Future*               down7Future;
    Future*               requesterFuture;
    McastReceiverMemory*  mrm;
} Receiver;

static unsigned long   numDeletedProds;

static void
sigHandler(int sig)
{
    switch (sig) {
    case SIGIO:
    	log_debug("SIGIO");
        return;
    case SIGPIPE:
    	log_debug("SIGPIPE");
        return;
    case SIGINT:
    	log_debug("SIGINT");
    	down7_halt();
        return;
    case SIGTERM:
    	log_debug("SIGTERM");
    	down7_halt();
        return;
    case SIGHUP:
    	log_debug("SIGHUP");
        return;
    case SIGUSR1:
    	log_debug("SIGUSR1");
        log_refresh();
        return;
    case SIGUSR2:
    	log_debug("SIGUSR2");
        log_refresh();
        return;
    }
}

/**
 * Only called once.
 */
static int
setup(void)
{
    /*
     * The path-prefix of the product-queue is also used to construct the
     * pathname of the product-index map (*.pim).
     */
    setQueuePath(DOWN7_PQ_PATHNAME);

    setLdmLogDir("."); // For LDM-7 receiver session-memory files (*.yaml)

    ud7_init(sigHandler);

    return 0;
}

/**
 * Only called once.
 */
static int
teardown(void)
{
	ud7_free();

    unlink(DOWN7_PQ_PATHNAME);

    return 0;
}

static void
thread_blockSigTerm()
{
    sigset_t mask;

    (void)sigemptyset(&mask);
    (void)sigaddset(&mask, SIGTERM);
    (void)pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

static int
createEmptyProductQueue(const char* const pathname)
{
    int status = pq_create(pathname, 0666, PQ_DEFAULT, 0, PQ_DATA_CAPACITY,
            NUM_SLOTS, &pq); // PQ_DEFAULT => clobber existing

    if (status) {
        log_add_errno(status, "pq_create(\"%s\") failure", pathname);
    }
    else {
        CU_ASSERT_EQUAL_FATAL(pq_close(pq), 0);
		pq = NULL;
    }
    return status;
}

static bool
isOnline(const InetSockAddr* sockId)
{
	bool          online = false;
    CU_ASSERT_PTR_NOT_NULL_FATAL(sockId);

    struct sockaddr addr;
    int             status = isa_initSockAddr(sockId, AF_INET, false, &addr);
    CU_ASSERT_EQUAL_FATAL(status, 0);

	const int fd = socket(addr.sa_family, SOCK_STREAM, IPPROTO_TCP);
    CU_ASSERT_NOT_EQUAL_FATAL(fd, -1);

	online = connect(fd, &addr, sizeof(addr)) == 0;

	(void)close(fd);

    return online;
}

typedef struct {
    signaturet sig;
    bool       delete;
} RequestArg;

static bool
rqstr_isDone(Requester* const requester)
{
    bool done;

    if (requester->done) {
        done = true;
    }
    else {
        sigset_t sigSet;

        (void)sigpending(&sigSet);

        done = sigismember(&sigSet, SIGTERM);
    }

    return done;
}

static void
rqstr_subDecide(
        RequestArg* const reqArg,
        const signaturet  sig)
{
    static unsigned short    xsubi[3] = {(unsigned short)1234567890,
                                         (unsigned short)9876543210,
                                         (unsigned short)1029384756};
    if (erand48(xsubi) >= REQUEST_RATE) {
        reqArg->delete = false;
    }
    else {
        reqArg->delete = true;
        (void)memcpy(reqArg->sig, sig, sizeof(signaturet));
    }
}

static int
rqstr_decide(
        const prod_info* const restrict info,
        const void* const restrict      data,
        void* const restrict            xprod,
        const size_t                    size,
        void* const restrict            arg)
{
    char infoStr[LDM_INFO_MAX];
    log_debug("Entered: info=\"%s\"",
            s_prod_info(infoStr, sizeof(infoStr), info, 1));
    static FmtpProdIndex maxProdIndex;
    static bool          maxProdIndexSet = false;
    FmtpProdIndex        prodIndex;
    RequestArg* const    reqArg = (RequestArg*)arg;

    /*
     * The monotonicity of the product-index is checked so that only the most
     * recently-created data-product is eligible for deletion.
     *
     * Assumption: product index == sequence number == signature
     */
    prodIndex = info->seqno;
    if (maxProdIndexSet && prodIndex <= maxProdIndex) {
        reqArg->delete = false;
    }
    else {
        rqstr_subDecide(reqArg, info->signature);
        maxProdIndex = prodIndex;
        maxProdIndexSet = true;
    }

    char buf[2*sizeof(signaturet)+1];
    sprint_signaturet(buf, sizeof(buf), info->signature);
    log_debug("Returning %s: prodIndex=%lu",
            reqArg->delete ? "delete" : "don't delete",
            (unsigned long)prodIndex);
    return 0; // necessary for `pq_sequence()`
}

/**
 * @retval    0            Success.
 * @retval    PQ_CORRUPT   The product-queue is corrupt.
 * @retval    PQ_LOCKED    The data-product was found but is locked by another
 *                         process.
 * @retval    PQ_NOTFOUND  The data-product wasn't found.
 * @retval    PQ_SYSTEM    System error. Error message logged.
 */
static inline int // inline because only called in one place
rqstr_delAndReq(const signaturet sig)
{
    FmtpProdIndex  prodIndex;
    (void)memcpy(&prodIndex, sig + sizeof(signaturet) - sizeof(FmtpProdIndex),
        sizeof(FmtpProdIndex));
    prodIndex = ntohl(prodIndex); // encoded in `sender_insertProducts()`

    char buf[2*sizeof(signaturet)+1];
    int  status = pq_deleteBySignature(pq, sig);

    if (status) {
        (void)sprint_signaturet(buf, sizeof(buf), sig);
        log_error_q("Couldn't delete data-product: pq=%s, prodIndex=%lu, sig=%s",
                pq_getPathname(pq), (unsigned long)prodIndex, buf);
    }
    else {
        if (log_is_enabled_info) {
            (void)sprint_signaturet(buf, sizeof(buf), sig);
            log_info("Deleted data-product: prodIndex=%lu, sig=%s",
                    (unsigned long)prodIndex, buf);
        }

        numDeletedProds++;

        down7_requestProduct(prodIndex);
    }

    return status;
}

/**
 * Executes a requester to test the "backstop" mechanism. Selected data-products
 * are deleted from the downstream product-queue and then requested from the
 * upstream LDM via the downstream LDM7.
 *
 * @param[in,out] arg   Pointer to requester object
 */
static void
rqstr_run(Requester* const requester)
{
    log_debug("Entered");

    thread_blockSigTerm();

    while (!rqstr_isDone(requester)) {
        RequestArg reqArg;
        int        status = pq_sequence(pq, TV_GT, PQ_CLASS_ALL,
                rqstr_decide, &reqArg);

        if (status == PQUEUE_END) {
            static int unblockSigs[] = {SIGTERM};

            // Temporarily unblocks SIGCONT as well
            (void)pq_suspendAndUnblock(30, unblockSigs, 1);
        }
        else {
            CU_ASSERT_EQUAL_FATAL(status, 0);

            if (reqArg.delete) {
                /*
                 * The data-product is deleted here rather than in
                 * `requester_decide()` because in that function, the
                 * product's region is locked, deleting it attempts to lock it
                 * again, and deadlock results.
                 */
                CU_ASSERT_EQUAL_FATAL(rqstr_delAndReq(reqArg.sig), 0);
            }
        }
    }

    // Because end-of-thread
    log_flush_error();
    log_debug("Returning");
}

static void
rqstr_halt(
        Requester* const requester,
        const pthread_t  thread)
{

    mutex_lock(&requester->mutex);
        requester->done = true;
        int status = pthread_kill(thread, SIGTERM);
		CU_ASSERT_TRUE(status == 0 || status == ESRCH);
    mutex_unlock(&requester->mutex);
}

/**
 * Executes a requester to test the "backstop" mechanism. Selected data-products
 * are deleted from the downstream product-queue and then requested from the
 * upstream LDM.
 */
static void
rqstr_init(Requester* const requester)
{
    int status = mutex_init(&requester->mutex, PTHREAD_MUTEX_ERRORCHECK, true);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    requester->done = false;
}

static void
rqstr_destroy(Requester* const requester)
{
    int status = mutex_destroy(&requester->mutex);
	CU_ASSERT_EQUAL_FATAL(status, 0);
}

/**
 * Initializes a receiver.
 *
 * @param[in,out] receiver  Receiver object
 * @param[in]     srvrAddr  Address of server's socket
 * @param[in]     port      Port number of sender in host byte-order
 * @param[in]     feedtype  The feedtype to which to subscribe
 */
static void
rcvr_init(
        Receiver* const restrict receiver,
		InetSockAddr* const      srvrAddr,
        const feedtypet          feed)
{
    CU_ASSERT_EQUAL_FATAL(createEmptyProductQueue(DOWN7_PQ_PATHNAME), 0)

    /*
     * The product-queue is opened thread-safe because it's accessed on
     * multiple threads:
     * - Multicast data-block insertion
     * - Unicast missed data-block insertion
     * - Unicast missed or backlog product insertion
     * - "Backstop" simulation via product-deletion and request
     */
    CU_ASSERT_EQUAL_FATAL(pq_open(DOWN7_PQ_PATHNAME, PQ_THREADSAFE, &pq), 0);

    // Ensure no memory from a previous session
    CU_ASSERT_EQUAL_FATAL(mrm_delete(srvrAddr, feed), true);
    receiver->mrm = mrm_open(srvrAddr, feed);
    CU_ASSERT_PTR_NOT_NULL_FATAL(receiver->mrm);

    numDeletedProds = 0;

	int status = down7_init(srvrAddr, feed, "dummy", localVcEnd, pq,
			receiver->mrm);
	if (status)
		log_flush_error();
	CU_ASSERT_EQUAL_FATAL(status, 0);
}

/**
 * Destroys a receiver.
 */
static void
rcvr_destroy(Receiver* const recvr)
{
    down7_destroy();

    int status = mrm_close(recvr->mrm);
	CU_ASSERT_TRUE(status);

	CU_ASSERT_EQUAL(pq_close(pq), 0);
	pq = NULL;

    status = unlink(DOWN7_PQ_PATHNAME);
	CU_ASSERT_EQUAL(status, 0);
}

static int
rcvr_executeRequester(
        void* const restrict  arg,
        void** const restrict result)
{
    Receiver* receiver = (Receiver*)arg;

    rqstr_run(&receiver->requester);

    log_flush_error();

    return 0;
}

static int
rcvr_haltRequester(
        void* const     arg,
        const pthread_t thread)
{
    Receiver* receiver = (Receiver*)arg;

    rqstr_halt(&receiver->requester, thread);

    return 0;
}

/**
 * @retval 0  Success
 */
static unsigned long
rcvr_getNumProds()
{
	size_t numProd;
    CU_ASSERT_EQUAL(pq_stats(pq, &numProd, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL), 0);
    return numProd;
}

/**
 * Executes a receiver. Blocks.
 *
 * This implementation access the product-queue on 4 threads:
 * - Multicast data-block receiver thread
 * - Missed data-block receiver thread
 * - Product deletion and request thread (simulates FMTP layer missing products
 * - Missed products reception thread.
 */
static void
test_down7(void)
{
    // Block pq-used `SIGALRM` and `SIGCONT` to prevent `sleep()` returning
    sigset_t sigMask, prevSigMask;
    sigemptyset(&sigMask);
    sigaddset(&sigMask, SIGALRM);
    sigaddset(&sigMask, SIGCONT); // No effect if all threads block
    int status = pthread_sigmask(SIG_BLOCK, &sigMask, &prevSigMask);
    CU_ASSERT_EQUAL(status, 0);
    /*
    sigset_t oldSigSet;
    blockSigCont(&oldSigSet);
    */

#if RUN_REQUESTER
    rqstr_init(&recvr->requester);

    /**
     * Starts a data-product requester on a separate thread to test the
     * "backstop" mechanism. Selected data-products are deleted from the
     * downstream product-queue and then requested from the upstream LDM.
     */
    recvr->requesterFuture = executor_submit(executor, recvr,
            rcvr_executeRequester, rcvr_haltRequester);
    CU_ASSERT_PTR_NOT_NULL_FATAL(recvr->requesterFuture );
#endif

    InetSockAddr* sockId = isa_newFromId(UP7_HOST, UP7_PORT);
    CU_ASSERT_PTR_NOT_NULL_FATAL(sockId);

	Receiver rcvr;
	rcvr_init(&rcvr, sockId, ANY);

    if (isOnline(sockId)) {
		CU_ASSERT_EQUAL(down7_run(), 0);
		log_flush_error();

		log_notice("%u sender product-queue insertions", NUM_PRODS);
		unsigned long numDownInserts = down7_getNumProds();
		log_notice("%lu receiver product-queue insertions", numDownInserts);
		log_notice("%ld outstanding product reservations", pqe_get_count(pq));
		CU_ASSERT_EQUAL(numDownInserts, NUM_PRODS);
    }

    rcvr_destroy(&rcvr);
    isa_free(sockId);

	// Restore signal mask
    status = pthread_sigmask(SIG_SETMASK, &prevSigMask, NULL);
    CU_ASSERT_EQUAL(status, 0);
}

/**
 * Executes this program.
 *
 *     Down7_test [-v|-x] [-l <em>log_dest</em>]
 *
 * where:
 *     -v        Verbose logging
 *     -x        Debug logging
 *     <em>log_dest</em>  Logging destination. Pathname of log file or
 *                 "-"  Standard error stream
 *                 ""   System logging daemon
 *               Default depends on existence of controlling terminal:
 *                 Exists:         Standard error stream
 *                 Doesn't exist:  LDM log file
 *
 * @param[in] argc  Number of arguments
 * @param[in] argv  Argument vector
 * @return          Exit code
 * @retval    0     Success
 */
int main(
        const int    argc,
        char* const* argv)
{
    int status = 1; // Failure

    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }
    //log_set_level(LOG_LEVEL_DEBUG);

    opterr = 1; // Prevent getopt(3) from printing error messages
    for (int ch; (ch = getopt(argc, argv, "l:vx")) != EOF; ) {
        switch (ch) {
            case 'l': {
                if (log_set_destination(optarg)) {
                    log_syserr("Couldn't set logging destination to \"%s\"",
                            optarg);
                    exit(1);
                }
                break;
            }
            case 'v':
                if (!log_is_enabled_info)
                    log_set_level(LOG_LEVEL_INFO);
                break;
            case 'x':
                if (!log_is_enabled_debug)
                    log_set_level(LOG_LEVEL_DEBUG);
                break;
            default: {
                log_add("Unknown option: \"%c\"", optopt);
                return 1;
            }
        }
    }

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_down7)) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        status = CU_get_number_of_suites_failed() +
                CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    return status;
}
