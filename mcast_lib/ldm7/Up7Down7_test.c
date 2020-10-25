/**
 * Copyright 2020 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: Up7Down7_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests an upstream LDM-7 sending to a downstream LDM-7.
 */
#include "config.h"

#include "Up7Down7_lib.h"

#include "down7.h"
#include "error.h"
#include "globals.h"
#include "inetutil.h"
#include "LdmConfFile.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "mldm_sender_map.h"
#include "MldmRcvrMemory.h"
#include "pq.h"
#include "prod_index_map.h"
#include "registry.h"
#include "Thread.h"
#include "timestamp.h"
#include "up7.h"
#include "UpMcastMgr.h"
#include "VirtualCircuit.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

#ifndef MAX
    #define MAX(a,b) ((a) >= (b) ? (a) : (b))
#endif

typedef struct {
    SVCXPRT*              xprt;
} MyUp7;

typedef struct {
    pthread_t             thread;
    MyUp7*                myUp7;
    int                   srvrSock;
} Sender;

void sigHandler(
        int sig)
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
		down7_halt();
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
    setQueuePath(UP7_PQ_PATHNAME);

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
	unlink(UP7_PQ_PATHNAME);

    return 0;
}

#if 0
/*
 * The following might be called multiple times.
 */

static void
blockSigCont(
        sigset_t* const oldSigSet)
{
    sigset_t newSigSet;
    (void)sigemptyset(&newSigSet);
    (void)sigaddset(&newSigSet, SIGCONT);
    int status = pthread_sigmask(SIG_BLOCK, &newSigSet, oldSigSet);
    CU_ASSERT_EQUAL_FATAL(status, 0);
}

static void
unblockSigCont(
        sigset_t* const oldSigSet)
{
    sigset_t newSigSet;
    (void)sigemptyset(&newSigSet);
    (void)sigaddset(&newSigSet, SIGCONT);
    int status = pthread_sigmask(SIG_UNBLOCK, &newSigSet, oldSigSet);
    CU_ASSERT_EQUAL_FATAL(status, 0);
}
#endif

/**
 * Initializes a sender. Upon return, listen() will have been called.
 *
 * @param[in] sender  Sender to initialize
 */
static void
sndr_init(Sender* const sender)
{
    int srvrSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CU_ASSERT_NOT_EQUAL_FATAL(srvrSock, -1);

    int                on = 1;
    struct sockaddr_in addr = {};

    (void)setsockopt(srvrSock, SOL_SOCKET, SO_REUSEADDR, (char*)&on,
    		sizeof(on));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(UP7_HOST);
    addr.sin_port = htons(UP7_PORT);

    int status = bind(srvrSock, (struct sockaddr*)&addr, sizeof(addr));
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = listen(srvrSock, 1);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    sender->srvrSock = srvrSock;
    sender->myUp7 = NULL;
}

/**
 * Kills the multicast LDM sender process if it exists.
 */
static void
killMcastSndr(void)
{
    log_debug("Entered");

    pid_t pid = umm_getSndrPid();

    if (pid != 0) {
		log_info("Sending SIGTERM to multicast LDM sender process %ld",
				(long)pid);
		int status = kill(pid, SIGTERM);
		CU_ASSERT_EQUAL(status, 0);

		/* Reap the terminated multicast sender. */
		{
			log_debug("Reaping multicast sender child process");
			const pid_t wpid = waitpid(pid, &status, 0);

			CU_ASSERT_EQUAL(wpid, pid);
			CU_ASSERT_TRUE(wpid > 0);
			CU_ASSERT_TRUE(WIFEXITED(status));
			CU_ASSERT_EQUAL(WEXITSTATUS(status), 0);

			status = umm_terminated(wpid);
			CU_ASSERT_EQUAL(status, 0);
		}
	}

    log_debug("Returning");
}

/**
 * Executes an upstream LDM7 server. Called by `pthread_create()`.
 *
 * @param[in]  arg         Pointer to sender's server-socket.
 * @retval     0           Success
 * @retval     ECONNRESET  Connection reset by client
 * @retval     ETIMEDOUT   Connection timed-out
 */
static void*
up7Srvr_run(void* const arg)
{
	log_notice("Upstream LDM7 server started");

    int                     status = 0; // Success
    const int               srvrSock = *(int*)arg;
	struct sockaddr_storage addr;
	socklen_t               addrlen = sizeof(addr);

	for (;;) {
        struct pollfd pfd = {.fd=srvrSock, .events=POLLRDNORM};

        log_debug("Calling poll()");
        // (3rd (timeout) argument == -1) => indefinite wait
		CU_ASSERT_EQUAL(poll(&pfd, 1, -1), 1);

		/*
		 * NB: Some poll(2) implementations return `POLLRDNORM` rather than
		 * `POLLERR` and rely on the failure of the subsequent I/O operation.
		 */
		if (pfd.revents & POLLERR) {
			log_error("Error on socket %d", srvrSock);
			break;
		}
		if (pfd.revents & POLLHUP) {
			log_error("Hangup on socket %d", srvrSock);
			break;
		}
		CU_ASSERT_TRUE(pfd.revents == POLLRDNORM);

        log_debug("Calling accept()");
		const int sock = accept(srvrSock, (struct sockaddr*)&addr, &addrlen);

		if (sock == -1) {
			log_notice("accept() failure");
			CU_ASSERT_TRUE(errno == EINTR || errno == EIO);
		}
		else {
			CU_ASSERT_EQUAL_FATAL(addr.ss_family, AF_INET);

			char* rmtId = sockAddrIn_format((struct sockaddr_in*)&addr);
			CU_ASSERT_PTR_NOT_NULL(rmtId);
			log_notice("Accept()ed connection from %s on socket %d", rmtId,
					srvrSock);
			free(rmtId);

			/*
			 * 0 => use default read/write buffer sizes.
			 * `sock` will be closed by `svc_destroy()`.
			 */
			SVCXPRT* xprt = svcfd_create(sock, 0, 0);
			CU_ASSERT_PTR_NOT_NULL_FATAL(xprt);
			xprt->xp_raddr = *(struct sockaddr_in*)&addr;
			xprt->xp_addrlen = sizeof(struct sockaddr_in);

			// Last argument == 0 => don't register with portmapper
			CU_ASSERT_TRUE_FATAL(svc_register(xprt, LDMPROG, 7, ldmprog_7, 0));

			const unsigned TIMEOUT = 2*interval;

			status = one_svc_run(sock, TIMEOUT);

			if (status == ECONNRESET) {
				/*
				 * one_svc_run() called svc_getreqset(), which called
				 * svc_destroy()
				 */
				log_add("Connection with LDM client lost");
			}
			else {
				if (status == ETIMEDOUT)
					log_add("Connection from client LDM silent for %u seconds",
							TIMEOUT);

				svc_destroy(xprt);
				xprt = NULL;
			}

			svc_unregister(LDMPROG, 7);
		} // Connection accepted
	} // Indefinite loop

    log_flush_error();
    log_debug("Returning %d", status);

    return NULL;
}

static void
sndr_fillPq(void)
{
    int             status;
    product         prod;
    prod_info*      info = &prod.info;
    char            ident[80];
    void*           data = NULL;
    unsigned short  xsubi[3] = {(unsigned short)1234567890,
                               (unsigned short)9876543210,
                               (unsigned short)1029384756};
    struct timespec duration;

    info->feedtype = EXP;
    info->ident = ident;
    info->origin = "localhost";
    (void)memset(info->signature, 0, sizeof(info->signature));

    for (int i = 0; i < NUM_PRODS; i++) {
        const unsigned size = MAX_PROD_SIZE*erand48(xsubi) + 0.5;
        const ssize_t  nbytes = snprintf(ident, sizeof(ident), "%d", i);

        CU_ASSERT_TRUE_FATAL(nbytes >= 0 && nbytes < sizeof(ident));
        status = set_timestamp(&info->arrival);
        CU_ASSERT_EQUAL_FATAL(status, 0);
        // Signature == sequence number
        info->seqno = i;
        uint32_t signet = htonl(i); // decoded in `requester_decide()`
        (void)memcpy(info->signature+sizeof(signaturet)-sizeof(signet), &signet,
                sizeof(signet));
        info->sz = size;

        data = realloc(data, size);
        CU_ASSERT_PTR_NOT_NULL(data);
        prod.data = data;

		char buf[LDM_INFO_MAX];
		log_info("Inserting product {index: %d, info: \"%s\"}", i,
				s_prod_info(buf, sizeof(buf), info, log_is_enabled_debug));

		status = pq_insert(pq, &prod);
        CU_ASSERT_EQUAL_FATAL(status, 0);

        usleep(INTER_PRODUCT_GAP);
    }

    free(data);
}

/**
 * Initializes a sender and starts executing it on a new thread.
 *
 * @param[in,out] sender  Sender object
 * @param[in]     feed    Feed to be sent
 */
static void
sndr_start(
        Sender* const   sender,
        const feedtypet feed)
{
    int status;

    sndr_init(sender);

    // Ensure that the first product-index will be 0
    CU_ASSERT_EQUAL_FATAL(pim_delete(NULL, feed), 0);

    /*
     * The product-queue must be thread-safe because it's accessed on
     * multiple threads:
     *   - The product-insertion thread
     *   - The backlog thread
     *   - The missed-product thread
     * The following also clobbers any existing queue and opens it for writing
     */
    CU_ASSERT_EQUAL_FATAL(pq_create(UP7_PQ_PATHNAME, 0666, PQ_THREADSAFE, 0,
    		PQ_DATA_CAPACITY, NUM_SLOTS, &pq), 0);
    setQueuePath(UP7_PQ_PATHNAME); // For Up7 module

    SepMcastInfo* mcastInfo =
            smi_newFromStr(feed, "224.0.0.1:5173", "127.0.0.1:0");
    CU_ASSERT_PTR_NOT_NULL_FATAL(mcastInfo);

    if (!umm_isInited()) {
        status = umm_init(); // Upstream multicast manager
        if (status) {
            log_flush_error();
            CU_FAIL_FATAL("");
        }
    }

    const unsigned short subnetLen = 24;
    status = umm_addSndr(mcastInfo, 2, subnetLen, localVcEnd, UP7_PQ_PATHNAME);
    if (status) {
        log_flush_error();
        CU_FAIL_FATAL("");
    }

    char* upAddr = ipv4Sock_getLocalString(sender->srvrSock);
    char* mcastInfoStr = smi_toString(mcastInfo);
    char* vcEndPointStr = vcEndPoint_format(localVcEnd);
    log_notice("LDM7 sender starting up: pq=%s, upAddr=%s, mcastInfo=%s, "
            "localVcEnd=%s, subnetLen=%u", getQueuePath(), upAddr, mcastInfoStr,
            vcEndPointStr, subnetLen);
    free(vcEndPointStr);
    free(mcastInfoStr);
    free(upAddr);

    // Starts the sender on a new thread
    log_debug("Starting upstream LDM server on separate thread");
    CU_ASSERT_EQUAL_FATAL(pthread_create(&sender->thread, NULL, up7Srvr_run,
			&sender->srvrSock), 0);

    smi_free(mcastInfo);
}

/**
 * Stops a sender from executing and destroys it.
 *
 * @param[in,out]  Sender to be stopped and destroyed
 */
static void
sndr_stop(Sender* const sender)
{
    log_debug("Entered");

#if 0
    log_info("Canceling sender thread");
    int status = pthread_cancel(sender->thread);
    // Thread might've already terminated
    CU_ASSERT_TRUE(status == 0 || status == ESRCH);
#else
    log_debug("Shutting-down sender's server-socket");
    CU_ASSERT_EQUAL(shutdown(sender->srvrSock, SHUT_RDWR), 0);
#endif

    log_debug("Joining sender thread");
    CU_ASSERT_EQUAL(pthread_join(sender->thread, NULL), 0);

    CU_ASSERT_EQUAL(close(sender->srvrSock), 0);

    log_debug("Destroying Up7 module");
    up7_destroy();

    log_debug("Closing product-queue");
    CU_ASSERT_EQUAL(pq_close(pq), 0);
    pq = NULL;
    CU_ASSERT_EQUAL(unlink(UP7_PQ_PATHNAME), 0);

    log_debug("Deleting product-queue");
    unlink(UP7_PQ_PATHNAME);

    log_debug("Returning");
}

/**
 * Exec's a receiver. Doesn't block. A child process is used because the
 * product-queue supports only one instance per process and the
 * parent process has its own product-queue into which it inserts products.
 */
static pid_t
rcvr_exec(void)
{
	const pid_t pid = fork();
	CU_ASSERT_NOT_EQUAL(pid, -1);

	if (pid == 0) {
		// Child process
		log_debug("Executing Down7_test");

		char* argv[5];
		int   argc = 0;
		argv[argc++] = "Down7_test";
		if (log_is_enabled_debug) {
			argv[argc++] = "-x";
		}
		else if (log_is_enabled_info) {
			argv[argc++] = "-v";
		}
        argv[argc++] = "-l";
        argv[argc++] = strdup(log_get_destination());
        argv[argc++] = NULL;

        execvp("./Down7_test", argv);
		CU_FAIL("execlp() failure");
		exit(1);
	}

	log_notice("Exec'ed receiver process %lu", (unsigned long)pid);

    return pid;
}

/**
 * Stops the receiver.
 */
static int
rcvr_term(const pid_t rcvrPid)
{
    log_debug("Sending SIGTERM to receiver process %ld", (long)rcvrPid);
    CU_ASSERT_EQUAL(kill(rcvrPid, SIGTERM), 0);
    int status;
    pid_t pid = waitpid(rcvrPid, &status, 0);
    if (pid == (pid_t)-1) {
 	   log_syserr("waitpid(%ld) returned -1", (long)rcvrPid);
    }
    else {
 	   log_debug("waitpid(%ld) returned %ld", (long)rcvrPid, (long)pid);
    }
    CU_ASSERT_EQUAL(pid, rcvrPid);
	CU_ASSERT_TRUE(WIFEXITED(status));
	return WEXITSTATUS(status);
}

static void
test_up7(
        void)
{
    Sender   sender;

    sndr_start(&sender, ANY);
    log_flush_error();

    sndr_stop(&sender);
    log_clear();

    umm_destroy(true);
    log_flush_error();
}

static void
test_up7Down7(
        void)
{
    Sender   sender;
    int      status;

    // Block pq-used `SIGALRM` and `SIGCONT` to prevent `sleep()` returning
    sigset_t sigMask, prevSigMask;
    sigemptyset(&sigMask);
    sigaddset(&sigMask, SIGALRM);
    sigaddset(&sigMask, SIGCONT); // No effect if all threads block
    status = pthread_sigmask(SIG_BLOCK, &sigMask, &prevSigMask);
    CU_ASSERT_EQUAL(status, 0);
    /*
    sigset_t oldSigSet;
    blockSigCont(&oldSigSet);
    */

    umm_setRetxTimeout(5); // SWAG

    status = lcf_init(LDM_PORT, NULL);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    host_set* hostSet = lcf_newHostSet(HS_DOTTED_QUAD, UP7_HOST, NULL);
    CU_ASSERT_PTR_NOT_NULL_FATAL(hostSet);
    ErrorObj* errObj = lcf_addAllow(ANY, hostSet, ".*", NULL);
    CU_ASSERT_PTR_NULL_FATAL(errObj);

    sndr_start(&sender, ANY); // Blocks until sender's server is listening
    log_flush_error();

    // Exec's a receiver in a child process (because one product-queue per process)
    pid_t rcvrPid = rcvr_exec();

    CU_ASSERT_EQUAL(sleep(1), 0);
    sndr_fillPq();
    CU_ASSERT_EQUAL(sleep(1), 0);

    log_notice("Stopping receiver");
    CU_ASSERT_EQUAL(rcvr_term(rcvrPid), 0); // Bad exit code if not all received

    log_notice("Stopping sender");
    sndr_stop(&sender);

    killMcastSndr();
    lcf_destroy(true);

    status = pthread_sigmask(SIG_SETMASK, &prevSigMask, NULL);
    CU_ASSERT_EQUAL(status, 0);
}

int main(
        const int    argc,
        char* const* argv)
{
    int status = 1; // Failure

    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }
    log_set_level(LOG_LEVEL_NOTICE);

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
	//log_set_level(LOG_LEVEL_DEBUG);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (    CU_ADD_TEST(testSuite, test_up7) &&
                    CU_ADD_TEST(testSuite, test_up7Down7)
                    ) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        status = CU_get_number_of_suites_failed() +
                CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    log_flush_error();

    return status;
}
