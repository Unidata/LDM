/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: up7_down7_test.c
 * @author: Steven R. Emmerson
 *
 * This file tests an upstream LDM-7 sending to a downstream LDM-7.
 */

#include "config.h"

#include "down7.h"
#include "error.h"
#include "Executor.h"
#include "globals.h"
#include "inetutil.h"
#include "ldm_config_file.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "mldm_receiver_memory.h"
#include "mldm_sender_map.h"
#include "pq.h"
#include "prod_index_map.h"
#include "timestamp.h"
#include "up7.h"
#include "UpMcastMgr.h"
#include "VirtualCircuit.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
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
    pthread_mutex_t       mutex;
    pthread_cond_t        cond;
    Future*               future;
    MyUp7*                myUp7;
    int                   sock;
    bool                  executing;
    volatile sig_atomic_t done;
} Sender;

typedef struct {
    Down7*                down7;
    volatile sig_atomic_t done;
} Requester;

typedef struct {
    Requester             requester;
    Future*               down7Future;
    Future*               requesterFuture;
    Down7*                down7;
    McastReceiverMemory*  mrm;
} Receiver;

/*
 * Proportion of data-products that the receiving LDM-7 will delete from the
 * product-queue and request from the sending LDM-7 to simulate network
 * problems.
 */
#define                  REQUEST_RATE 0.2
// Maximum size of a data-product in bytes
#define                  MAX_PROD_SIZE 1000000
// Approximate number of times the product-queue will be "filled".
#define                  NUM_TIMES 5
// Duration, in nanoseconds, before the next product is inserted (i.e., gap
// duration)
#define                  INTER_PRODUCT_INTERVAL 50000000 // 50 ms
/*
 * Mean residence-time, in seconds, of a data-product. Also used to compute the
 * FMTP retransmission timeout.
 */
#define                  MEAN_RESIDENCE_TIME 2

// Derived values:

// Mean product size in bytes
#define                  MEAN_PROD_SIZE (MAX_PROD_SIZE/2)
// Mean number of products in product-queue
#define                  MEAN_NUM_PRODS \
        ((int)(MEAN_RESIDENCE_TIME / (INTER_PRODUCT_INTERVAL/1e9)))

/*
 * The product-queue is limited by its data-capacity (rather than its product-
 * capacity) to attempt to reproduce the queue corruption seen by Shawn Chen at
 * the University of Virginia.
 */
// Capacity of the product-queue in bytes
static const unsigned    PQ_DATA_CAPACITY = MEAN_NUM_PRODS*MEAN_PROD_SIZE;
// Capacity of the product-queue in number of products
static const unsigned    PQ_PROD_CAPACITY = MEAN_NUM_PRODS;
// Number of data-products to insert
static const unsigned    NUM_PRODS = NUM_TIMES*MEAN_NUM_PRODS;
static const char        LOCAL_HOST[] = "127.0.0.1";
static const int         UP7_PORT = 38800;
static const char        UP7_PQ_PATHNAME[] = "up7_test.pq";
static const char        DOWN7_PQ_PATHNAME[] = "down7_test.pq";
static McastProdIndex    initialProdIndex;
static pqueue*           receiverPq;
static uint64_t          numDeletedProds;
static const unsigned short FMTP_MCAST_PORT = 5173; // From Wireshark plug-in
static ServiceAddr*      up7Addr;
static VcEndPoint*       localVcEnd;
static Executor*         executor;

static void signal_handler(
        int sig)
{
    switch (sig) {
    case SIGUSR1:
        log_notice_q("SIGUSR1");
        log_refresh();
        return;
    case SIGINT:
        log_notice_q("SIGINT");
        return;
    case SIGTERM:
        log_notice_q("SIGTERM");
        return;
    default:
        log_notice_q("Signal %d", sig);
        return;
    }
}

static int
setTermSigHandler(void)
{
    struct sigaction sigact;
    int              status = sigemptyset(&sigact.sa_mask);
    if (status == 0) {
        sigact.sa_flags = 0;
        sigact.sa_handler = signal_handler;
        status = sigaction(SIGINT, &sigact, NULL);
        if (status == 0) {
            status = sigaction(SIGTERM, &sigact, NULL);
            if (status == 0) {
                sigact.sa_flags = SA_RESTART;
                status = sigaction(SIGUSR1, &sigact, NULL);
            }
        }
    }
    return status;
}

/**
 * Only called once.
 */
static int
setup(void)
{
    /*
     * Ensure that the upstream module `up7` obtains the upstream queue from
     * `getQueuePath()`. This is not done for the downstream module because
     * `down7.c` implements an object-specific product-queue. The path-prefix of
     * the product-queue is also used to construct the pathname of the product-
     * index map (*.pim).
     */
    setQueuePath(UP7_PQ_PATHNAME);

    setLdmLogDir("."); // For LDM-7 receiver session-memory files (*.yaml)

    int status = msm_init();
    if (status) {
        log_add("Couldn't initialize multicast sender map");
    }
    else {
        msm_clear();

        /*
         * The following allows a SIGTERM to be sent to the process group
         * without affecting the parent process (e.g., a make(1)). Unfortunately,
         * it also prevents a `SIGINT` from terminating the process.
        (void)setpgrp();
         */

        status = setTermSigHandler();
        if (status) {
            log_add("Couldn't set termination signal handler");
        }
        else {
            status = sa_new(&up7Addr, LOCAL_HOST, UP7_PORT);

            if (status) {
                log_add_errno(status,
                        "Couldn't construct upstream LDM7 service address");
            }
            else {
                localVcEnd = vcEndPoint_new(1, "Switch ID", "Port ID");

                if (localVcEnd == NULL) {
                    log_add("Couldn't construct local virtual-circuit endpoint");
                    status = LDM7_SYSTEM;
                }
                else {
                    executor = executor_new();

                    if (executor == NULL) {
                        vcEndPoint_free(localVcEnd);
                        log_add("Couldn't create new execution service");
                        status = LDM7_SYSTEM;
                    }
                } // `localVcEnd` created

                if (status)
                    sa_free(up7Addr);
            } // `up7Addr` created
        } // Termination signal handler installed

        if (status)
            msm_destroy();
    } // Multicast sender map initialized

    if (status)
        log_error_q("setup() failure");

    return status;
}

/**
 * Only called once.
 */
static int
teardown(void)
{
    executor_free(executor);
    vcEndPoint_free(localVcEnd);
    sa_free(up7Addr);

    msm_clear();
    msm_destroy();

    unlink(UP7_PQ_PATHNAME);
    unlink(DOWN7_PQ_PATHNAME);

    return 0;
}

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

static int
createEmptyProductQueue(
        const char* const pathname)
{
    pqueue* pq;
    int     status = pq_create(pathname, 0666, PQ_DEFAULT, 0, PQ_DATA_CAPACITY,
            PQ_PROD_CAPACITY, &pq); // PQ_DEFAULT => clobber existing

    if (status) {
        log_add_errno(status, "pq_create(\"%s\") failure", pathname);
    }
    else {
        status = pq_close(pq);
        if (status) {
            log_add("Couldn't close product-queue \"%s\"", pathname);
        }
    }
    return status;
}

/**
 * @param[in] up7        The upstream LDM-7 to be initialized.
 * @param[in] sock       The socket for the upstream LDM-7.
 * @param[in] termFd     Termination file-descriptor.
 */
static void
myUp7_init(
        MyUp7* const myUp7,
        const int    sock)
{
    /*
     * 0 => use default read/write buffer sizes.
     * `sock` will be closed by `svc_destroy()`.
     */
    SVCXPRT* const xprt = svcfd_create(sock, 0, 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(xprt);

    /*
     * Set the remote address in the RPC server-side transport because
     * `svcfd_create()` doesn't.
     */
    {
        struct sockaddr_in addr;
        socklen_t          addrLen = sizeof(addr);

        int status = getpeername(sock, &addr, &addrLen);
        CU_ASSERT_EQUAL_FATAL(status, 0);
        CU_ASSERT_EQUAL_FATAL(addrLen, sizeof(addr));
        CU_ASSERT_EQUAL_FATAL(addr.sin_family, AF_INET);
        xprt->xp_raddr = addr;
        xprt->xp_addrlen = addrLen;
    }

    // Last argument == 0 => don't register with portmapper
    CU_ASSERT_TRUE_FATAL(svc_register(xprt, LDMPROG, 7, ldmprog_7, 0));

    myUp7->xprt = xprt;
}

static void
myUp7_destroy(MyUp7* const myUp7)
{
    svc_unregister(LDMPROG, 7);

    if (myUp7->xprt) // Might have been destroyed by RPC layer
        svc_destroy(myUp7->xprt);
}

/**
 * Returns a new upstream LDM7.
 *
 * @param[in] sock  Socket descriptor with downstream LDM7
 * @return          New upstream LDM7
 * @see `myUp7_delete()`
 */
static MyUp7*
myUp7_new(int sock)
{
    MyUp7* myUp7 = malloc(sizeof(MyUp7));
    CU_ASSERT_PTR_NOT_NULL_FATAL(myUp7);
    myUp7_init(myUp7, sock);
    return myUp7;
}

/**
 * Deletes an upstream LDM7 instance. Inverse of `myUp7_new()`.
 *
 * @param[in] myUp7  Upstream LDM7
 * @see `myUp7_new()`
 */
static void
myUp7_free(MyUp7* const myUp7)
{
    myUp7_destroy(myUp7);
    free(myUp7);
}

/**
 * @param[in] up7          Upstream LDM-7.
 * @retval    0            Success. Connection was closed by downstream LDM-7.
 * @retval    LDM7_INTR    Signal was caught.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
static void
myUp7_run(
        MyUp7* const myUp7)
{
    int       status;
    const int sock = myUp7->xprt->xp_sock;
    struct    pollfd fds;

    fds.fd = sock;
    fds.events = POLLRDNORM;

    /**
     * The `up7.c` module needs to tell this function to return when a error
     * occurs that prevents continuation. The following mechanisms were
     * considered:
     * - Using a thread-specific signaling value. This was rejected because
     *   that would increase the coupling between this function and the
     *   `up7.c` module by
     *   - Requiring the `up7_..._7_svc()` functions use this mechanism; and
     *   - Requiring that the thread-specific data-key be publicly visible.
     * - Closing the socket in the `up7.c` module. This was rejected because
     *   of the race condition between closing the socket and the file
     *   descriptor being reused by another thread.
     * - Having `up7.c` call `svc_unregister()` and then checking `svc_fdset`
     *   in this function. This was rejected because the file description
     *   can also be removed from `svc_fdset` by `svc_getreqsock()`, except
     *   that the latter also destroys the transport.
     * - Having `up7.c` call `svc_destroy()` and then checking `svc_fdset`
     *   in this function. This was rejected because it destroys the
     *   transport, which is dereferenced by `ldmprog_7`.
     * - Creating and using the function `up7_isDone()`. This was chosen.
     */
    for (;;) {
        log_debug_1("myUp7_run(): Calling poll()");
        status = poll(&fds, 1, -1); // `-1` => indefinite timeout

        if (0 > status) {
            CU_ASSERT_EQUAL_FATAL(errno, EINTR);
            break;
        }

        CU_ASSERT_TRUE_FATAL(fds.revents & POLLRDNORM)

        log_debug_1("myUp7_run(): Calling svc_getreqsock()");
        svc_getreqsock(sock); // Calls `ldmprog_7()`. *Might* destroy `xprt`.

        if (!FD_ISSET(sock, &svc_fdset)) {
            // RPC layer called `svc_destroy()` on `myUp7->xprt`
            myUp7->xprt = NULL; // Let others know
            break;
        }
    } // While not done loop

    up7_reset();
}

static void
sender_lock(Sender* const sender)
{
    CU_ASSERT_EQUAL_FATAL(pthread_mutex_lock(&sender->mutex), 0);
}

static void
sender_unlock(Sender* const sender)
{
    CU_ASSERT_EQUAL_FATAL(pthread_mutex_unlock(&sender->mutex), 0);
}

static void
terminateMcastSender(void)
{
    pid_t pid = umm_getMldmSenderPid();

    if (pid) {
        log_debug_1("Sending SIGTERM to multicast LDM sender process");
        CU_ASSERT_EQUAL_FATAL(kill(pid, SIGTERM), 0);

        /* Reap the terminated multicast sender. */
        {
            int status;

            log_debug_1("Reaping multicast sender child process");
            const pid_t wpid = waitpid(pid, &status, 0);

            CU_ASSERT_EQUAL(wpid, pid);
            CU_ASSERT_TRUE_FATAL(wpid > 0);
            CU_ASSERT_TRUE(WIFEXITED(status));
            CU_ASSERT_EQUAL(WEXITSTATUS(status), 0);
            CU_ASSERT_EQUAL(umm_terminated(wpid), 0);
        }
    }
}

/**
 * Executes a sender. Notifies `sender_start()`.
 *
 * @param[in]  arg     Pointer to sender.
 * @param[out] result  Ignored
 * @retval     0       Success
 */
static int
sender_run(
        void* const restrict  arg,
        void** const restrict result)
{
    Sender* const sender = (Sender*)arg;
    static int    status;
    const int     servSock = sender->sock;
    struct pollfd fds;

    fds.fd = servSock;
    fds.events = POLLIN;

    char* upAddr = ipv4Sock_getLocalString(servSock);
    log_info_q("Upstream LDM listening on %s", upAddr);
    free(upAddr);

    sender_lock(sender);
    sender->executing = true;
    CU_ASSERT_EQUAL_FATAL(pthread_cond_signal(&sender->cond), 0);
    sender_unlock(sender);

    for (;;) {
        sender_lock(sender);
        if (sender->done) {
            sender_unlock(sender);
            break;
        }
        sender_unlock(sender);

        /* NULL-s => not interested in receiver's address */
        int sock = accept(servSock, NULL, NULL);

        if (sock == -1) {
            CU_ASSERT_EQUAL(errno, EINTR);
            break;
        }
        else {
            sender_lock(sender);

            if (sender->done) {
                sender_unlock(sender);
            }
            else {
                // Initializes `sender->myUp7.xprt`
                sender->myUp7 = myUp7_new(sock);

                sender_unlock(sender);

                myUp7_run(sender->myUp7);
                myUp7_free(sender->myUp7);
            }
        }
    }

    log_flush_error();
    log_debug_1("sender_run(): Returning &%d", status);

    return 0;
}

/**
 * Stops a sender that's executing on another thread.
 *
 * @param[in] arg     Sender to be stopped
 * @param[in] thread  Thread on which sender is running
 */
static int
sender_halt(
        void* const     arg,
        const pthread_t thread)
{
    Sender* const sender = (Sender*)arg;

    log_debug_1("Terminating multicast LDM sender");
    terminateMcastSender();

    sender_lock(sender);

    sender->done = true;

    CU_ASSERT_EQUAL_FATAL(pthread_kill(thread, SIGTERM), 0);

    sender_unlock(sender);

    return 0;
}

static int
sender_initSock(
        int* const sock)
{
    int status;
    int sck = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    CU_ASSERT_NOT_EQUAL_FATAL(sck, -1);

    int                on = 1;
    struct sockaddr_in addr;

    (void)setsockopt(sck, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));

    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    addr.sin_port = htons(0); // let O/S assign port

    status = bind(sck, (struct sockaddr*)&addr, sizeof(addr));
    CU_ASSERT_EQUAL_FATAL(status, 0);


    status = listen(sck, 1);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    *sock = sck;

    return 0;
}

/**
 * Returns the formatted address of a sender.
 *
 * @param[in] sender  The sender.
 * @return            Formatted address of the sender.
 */
static const char*
sender_getAddr(
        Sender* const sender)
{
    struct sockaddr_in addr;
    socklen_t          len = sizeof(addr);

    (void)getsockname(sender->sock, (struct sockaddr*)&addr, &len);

    return inet_ntoa(addr.sin_addr);
}

/**
 * Returns port number of a sender in host byte-order.
 *
 * @param[in] sender  The sender.
 * @return            Port number of the sender in host byte-order.
 */
static unsigned short
sender_getPort(
        Sender* const sender)
{
    struct sockaddr_in addr;
    socklen_t          len = sizeof(addr);

    (void)getsockname(sender->sock, (struct sockaddr*)&addr, &len);

    return ntohs(addr.sin_port);
}

static void
sender_insertProducts(void)
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

        status = pq_insert(pq, &prod);
        CU_ASSERT_EQUAL_FATAL(status, 0);
        char buf[LDM_INFO_MAX];
        log_info_q("Inserted: prodInfo=\"%s\"",
                s_prod_info(buf, sizeof(buf), info, 1));

        duration.tv_sec = 0;
        duration.tv_nsec = INTER_PRODUCT_INTERVAL;
        while (nanosleep(&duration, &duration))
            ;
    }

    free(data);
}

static int
sender_setMcastInfo(
        McastInfo** const mcastInfo,
        const feedtypet   feedtype)
{
    ServiceAddr* mcastServAddr;
    int          status = sa_new(&mcastServAddr, "224.0.0.1", FMTP_MCAST_PORT);

    if (status) {
        log_add("Couldn't create multicast service address object");
    }
    else {
        ServiceAddr* ucastServAddr;

        status = sa_new(&ucastServAddr, LOCAL_HOST, 0); // O/S selects port
        if (status) {
            log_add("Couldn't create unicast service address object");
        }
        else {
            status = mi_new(mcastInfo, feedtype, mcastServAddr, ucastServAddr);
            if (status) {
                log_add("Couldn't create multicast information object");
            }
            else {
                sa_free(ucastServAddr);
                sa_free(mcastServAddr);
            }
        }
    }

    return status;
}

/**
 * Initializes a sender and starts executing it on a new thread. Blocks until
 * notified by `sender_run()`.
 *
 * @param[in,out] sender  Sender object
 * @param[in]     feed    Feed to be sent
 */
static void
sender_start(
        Sender* const   sender,
        const feedtypet feed)
{
    CU_ASSERT_EQUAL_FATAL(pthread_mutex_init(&sender->mutex, NULL), 0);
    CU_ASSERT_EQUAL_FATAL(pthread_cond_init(&sender->cond, NULL), 0);

    // Ensure that the first product-index will be 0
    CU_ASSERT_EQUAL_FATAL(pim_delete(NULL, feed), 0);

    /*
     * The product-queue must be thread-safe because it's accessed on
     * multiple threads:
     * - The product-insertion thread
     * - The backlog thread
     * - The missed-product thread
     */
    CU_ASSERT_EQUAL_FATAL(createEmptyProductQueue(UP7_PQ_PATHNAME), 0);
    CU_ASSERT_EQUAL_FATAL(pq_open(getQueuePath(), PQ_THREADSAFE, &pq), 0);

    McastInfo* mcastInfo;
    CU_ASSERT_EQUAL_FATAL(sender_setMcastInfo(&mcastInfo, feed), 0);

    CU_ASSERT_EQUAL_FATAL(umm_clear(), 0); // Upstream multicast manager

    VcEndPoint* vcEnd = vcEndPoint_new(1, "Switch ID", "Port ID");
    CU_ASSERT_PTR_NOT_NULL(vcEnd);

    in_addr_t subnet;
    CU_ASSERT_EQUAL(inet_pton(AF_INET, LOCAL_HOST, &subnet), 1);

    CidrAddr*   fmtpSubnet = cidrAddr_new(subnet, 24);
    CU_ASSERT_PTR_NOT_NULL(fmtpSubnet);

    const struct in_addr mcastIface = {inet_addr(LOCAL_HOST)};

    CU_ASSERT_EQUAL(umm_addPotentialSender(mcastIface, mcastInfo, 2, vcEnd,
            fmtpSubnet, UP7_PQ_PATHNAME), 0);

    char* mcastInfoStr = mi_format(mcastInfo);
    char* vcEndPointStr = vcEndPoint_format(vcEnd);
    char* fmtpSubnetStr = cidrAddr_format(fmtpSubnet);
    log_notice_q("LDM7 server starting up: pq=%s, mcastInfo=%s, vcEnd=%s, "
            "subnet=%s", getQueuePath(), mcastInfoStr, vcEndPointStr,
            fmtpSubnetStr);
    free(fmtpSubnetStr);
    free(vcEndPointStr);
    free(mcastInfoStr);

    CU_ASSERT_EQUAL_FATAL(sender_initSock(&sender->sock), 0);

    sender->executing = false;
    sender->done = false;
    sender->myUp7 = NULL;

    // Starts the sender on a new thread
    sender->future = executor_submit(executor, sender, sender_run, sender_halt);
    CU_ASSERT_PTR_NOT_NULL_FATAL(sender->future);

    sender_lock(sender);
    while (!sender->executing)
        CU_ASSERT_EQUAL_FATAL(pthread_cond_wait(&sender->cond, &sender->mutex),
                0);
    sender_unlock(sender);

    cidrAddr_delete(fmtpSubnet);
    vcEndPoint_free(vcEnd);

    mi_delete(mcastInfo);
}

/**
 * Stops a sender from executing and destroys it.
 *
 * @param[in,out]  Sender to be stopped and destroyed
 */
static void
sender_stop(Sender* const sender)
{
    CU_ASSERT_EQUAL(future_cancel(sender->future), 0);
    CU_ASSERT_EQUAL(future_getAndFree(sender->future, NULL), ECANCELED);

    CU_ASSERT_EQUAL_FATAL(close(sender->sock), 0);

    log_debug_1("Clearing upstream multicast manager");
    CU_ASSERT_EQUAL(umm_clear(), 0);

    CU_ASSERT_EQUAL(pq_close(pq), 0);
    CU_ASSERT_EQUAL(unlink(UP7_PQ_PATHNAME), 0);

    CU_ASSERT_EQUAL(pthread_cond_destroy(&sender->cond), 0);
    CU_ASSERT_EQUAL(pthread_mutex_destroy(&sender->mutex), 0);
}

typedef struct {
    signaturet sig;
    bool       delete;
} RequestArg;

static void
thread_blockSigTerm()
{
    sigset_t mask;

    (void)sigemptyset(&mask);
    (void)sigaddset(&mask, SIGTERM);
    (void)pthread_sigmask(SIG_BLOCK, &mask, NULL);
}

static bool
requester_isDone(Requester* const requester)
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
requester_subDecide(
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

static inline int // inline because only called in one place
requester_decide(
        const prod_info* const restrict info,
        const void* const restrict      data,
        void* const restrict            xprod,
        const size_t                    size,
        void* const restrict            arg)
{
    char infoStr[LDM_INFO_MAX];
    log_debug_1("Entered: info=\"%s\"",
            s_prod_info(infoStr, sizeof(infoStr), info, 1));
    static FmtpProdIndex maxProdIndex;
    static bool          maxProdIndexSet = false;
    FmtpProdIndex        prodIndex;
    RequestArg* const    reqArg = (RequestArg*)arg;

    /*
     * The monotonicity of the product-index is checked so that only the most
     * recently-created data-product is eligible for deletion.
     *
     * Product index == sequence number == signature
     */
    prodIndex = info->seqno;
    if (maxProdIndexSet && prodIndex <= maxProdIndex) {
        reqArg->delete = false;
    }
    else {
        requester_subDecide(reqArg, info->signature);
        maxProdIndex = prodIndex;
        maxProdIndexSet = true;
    }

    char buf[2*sizeof(signaturet)+1];
    sprint_signaturet(buf, sizeof(buf), info->signature);
    log_debug_1("Returning %s: prodIndex=%lu",
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
requester_deleteAndRequest(
        Down7* const     down7,
        const signaturet sig)
{
    FmtpProdIndex  prodIndex;
    (void)memcpy(&prodIndex, sig + sizeof(signaturet) - sizeof(FmtpProdIndex),
        sizeof(FmtpProdIndex));
    prodIndex = ntohl(prodIndex); // encoded in `sender_insertProducts()`

    char buf[2*sizeof(signaturet)+1];
    int  status = pq_deleteBySignature(receiverPq, sig);

    if (status) {
        (void)sprint_signaturet(buf, sizeof(buf), sig);
        log_error_q("Couldn't delete data-product: pq=%s, prodIndex=%lu, sig=%s",
                pq_getPathname(receiverPq), (unsigned long)prodIndex, buf);
    }
    else {
        if (log_is_enabled_info) {
            (void)sprint_signaturet(buf, sizeof(buf), sig);
            log_info_q("Deleted data-product: prodIndex=%lu, sig=%s",
                    (unsigned long)prodIndex, buf);
        }

        numDeletedProds++;

        down7_requestProduct(down7, prodIndex);
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
requester_run(Requester* const requester)
{
    log_debug_1("requester_run(): Entered");

    thread_blockSigTerm();

    while (!requester_isDone(requester)) {
        RequestArg reqArg;
        int        status = pq_sequence(receiverPq, TV_GT, PQ_CLASS_ALL,
                requester_decide, &reqArg);

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
                CU_ASSERT_EQUAL_FATAL(requester_deleteAndRequest(
                        requester->down7, reqArg.sig), 0);
            }
        }
    }

    // Because end-of-thread
    log_flush_error();
    log_debug_1("requester_run(): Returning");
}

static void
requester_halt(
        Requester* const requester,
        const pthread_t  thread)
{
    requester->done = 1;
    CU_ASSERT_EQUAL(pthread_kill(thread, SIGTERM), 0);
}

/**
 * Executes a requester to test the "backstop" mechanism. Selected data-products
 * are deleted from the downstream product-queue and then requested from the
 * upstream LDM.
 */
static void
requester_init(
        Requester* const requester,
        Down7* const     down7)
{
    requester->down7 = down7;
    requester->done = false;
}

static void
requester_destroy(Requester* const requester)
{}

/**
 * Initializes a receiver.
 *
 * @param[in,out] receiver  Receiver object
 * @param[in]     addr      Address of sender: either hostname or IPv4 address
 * @param[in]     port      Port number of sender in host byte-order
 * @param[in]     feedtype  The feedtype to which to subscribe
 */
static void
receiver_init(
        Receiver* const restrict   receiver,
        const char* const restrict addr,
        const unsigned short       port,
        const feedtypet            feedtype)
{
    CU_ASSERT_EQUAL_FATAL(createEmptyProductQueue(DOWN7_PQ_PATHNAME), 0)

    /*
     * The product-queue is opened thread-safe because it's accessed on
     * multiple threads:
     * - Multicast data-block insertion
     * - Unicast missed data-block insertion
     * - Unicast missed or backlog product insertion
     */
    CU_ASSERT_EQUAL_FATAL(
            pq_open(DOWN7_PQ_PATHNAME, PQ_THREADSAFE, &receiverPq), 0);

    ServiceAddr* servAddr;
    CU_ASSERT_EQUAL_FATAL(sa_new(&servAddr, addr, port), 0);

    // Ensure no memory from a previous session
    CU_ASSERT_EQUAL_FATAL(mrm_delete(servAddr, feedtype), true);
    receiver->mrm = mrm_open(servAddr, feedtype);
    CU_ASSERT_PTR_NOT_NULL_FATAL(receiver->mrm);

    numDeletedProds = 0;

    VcEndPoint* const vcEnd = vcEndPoint_new(1, "Switch ID", "Port ID");
    CU_ASSERT_PTR_NOT_NULL(vcEnd);
    receiver->down7 = down7_new(servAddr, feedtype, LOCAL_HOST, vcEnd,
            receiverPq, receiver->mrm);
    CU_ASSERT_PTR_NOT_NULL_FATAL(receiver->down7);
    vcEndPoint_free(vcEnd);

    sa_free(servAddr);
}

/**
 * De-initializes a receiver.
 */
static void
receiver_destroy(Receiver* const recvr)
{
    CU_ASSERT_EQUAL(down7_free(recvr->down7), 0);

    CU_ASSERT_TRUE(mrm_close(recvr->mrm));

    CU_ASSERT_EQUAL(pq_close(receiverPq), 0);

    CU_ASSERT_EQUAL(unlink(DOWN7_PQ_PATHNAME), 0);
}

static int
receiver_runRequester(
        void* const restrict  arg,
        void** const restrict result)
{
    Receiver* receiver = (Receiver*)arg;

    requester_run(&receiver->requester);

    log_flush_error();

    return 0;
}

static int
receiver_haltRequester(
        void* const     arg,
        const pthread_t thread)
{
    Receiver* receiver = (Receiver*)arg;

    requester_halt(&receiver->requester, thread);

    return 0;
}

static int
receiver_runDown7(
        void* const restrict  arg,
        void** const restrict result)
{
    Receiver* receiver = (Receiver*)arg;

    CU_ASSERT_EQUAL(down7_run(receiver->down7), 0);

    log_flush_error();

    return 0;
}

static int
receiver_haltDown7(
        void* const     arg,
        const pthread_t thread)
{
    Receiver* receiver = (Receiver*)arg;

    down7_halt(receiver->down7, thread);

    return 0;
}

/**
 * Starts the receiver on a new thread.
 *
 * This implementation access the product-queue on 4 threads:
 * - Multicast data-block receiver thread
 * - Missed data-block receiver thread
 * - Product deletion and request thread (simulates FMTP layer missing products
 * - Missed products reception thread.
 *
 * @param[in,out] recvr     Receiver object
 */
static void
receiver_start(
        Receiver* const restrict recvr)
{
    requester_init(&recvr->requester, recvr->down7);

    /**
     * Starts a data-product requester on a separate thread to test the
     * "backstop" mechanism. Selected data-products are deleted from the
     * downstream product-queue and then requested from the upstream LDM.
     */
    recvr->requesterFuture = executor_submit(executor, recvr,
            receiver_runRequester, receiver_haltRequester);
    CU_ASSERT_PTR_NOT_NULL_FATAL(recvr->requesterFuture );

    recvr->down7Future = executor_submit(executor, recvr,
            receiver_runDown7, receiver_haltDown7);
    CU_ASSERT_PTR_NOT_NULL_FATAL(recvr->down7Future);
}

/**
 * Stops the receiver. Blocks until the receiver's thread terminates.
 */
static void
receiver_stop(Receiver* const recvr)
{
    CU_ASSERT_EQUAL(future_cancel(recvr->down7Future), 0);
    CU_ASSERT_EQUAL(future_getAndFree(recvr->down7Future, NULL), ECANCELED);

    CU_ASSERT_EQUAL(future_cancel(recvr->requesterFuture), 0);
    CU_ASSERT_EQUAL(future_getAndFree(recvr->requesterFuture, NULL), ECANCELED);

    requester_destroy(&recvr->requester);
}

/**
 * @retval 0  Success
 */
static uint64_t
receiver_getNumProds(
        Receiver* const receiver)
{
    return down7_getNumProds(receiver->down7);
}

static long receiver_getPqeCount(
        Receiver* const receiver)
{
    return down7_getPqeCount(receiver->down7);
}

static void
test_up7(
        void)
{
    Sender   sender;

    sender_start(&sender, ANY);
    log_flush_error();

    sender_stop(&sender);
    log_clear();
}

static void
test_down7(
        void)
{
#if 1
    Receiver receiver;

    receiver_init(&receiver, LOCAL_HOST, UP7_PORT, ANY);
    receiver_start(&receiver);

    sleep(1);

    receiver_stop(&receiver);
    receiver_destroy(&receiver);
#else
    CU_ASSERT_EQUAL_FATAL(createEmptyProductQueue(DOWN7_PQ_PATHNAME), 0)
    CU_ASSERT_EQUAL_FATAL(
            pq_open(DOWN7_PQ_PATHNAME, PQ_THREADSAFE, &receiverPq), 0);

    CU_ASSERT_TRUE_FATAL(mrm_delete(up7Addr, ANY));
    McastReceiverMemory* mrm = mrm_open(up7Addr, ANY);
    CU_ASSERT_PTR_NOT_NULL_FATAL(mrm);

    Down7* down7 = down7_create(up7Addr, ANY, LOCAL_HOST, localVcEnd,
            receiverPq, mrm);
    CU_ASSERT_PTR_NOT_NULL(down7);

    sleep(1);

    CU_ASSERT_EQUAL(down7_destroy(down7), 0);

    CU_ASSERT_TRUE(mrm_close(mrm));
    CU_ASSERT_EQUAL(pq_close(receiverPq), 0);
    CU_ASSERT_EQUAL(unlink(DOWN7_PQ_PATHNAME), 0);
#endif
}

static void
test_bad_subscription(
        void)
{
    Sender   sender;
    Receiver receiver;

    sender_start(&sender, NEXRAD2);
    log_flush_error();

    receiver_init(&receiver, sender_getAddr(&sender), sender_getPort(&sender),
            NGRID);
    receiver_start(&receiver);

    sleep(1);

    receiver_stop(&receiver);
    receiver_destroy(&receiver);

    log_debug_1("Terminating sender");
    sender_stop(&sender);
    log_clear();
}

static void
test_up7_down7(
        void)
{
    Sender   sender;
    Receiver receiver;
    int      status;

    // Block pq-used `SIGALRM` and `SIGCONT` to prevent `sleep()` returning
    struct sigaction sigAction, prevSigAction;
    sigset_t         sigMask, prevSigMask;
    sigemptyset(&sigMask);
    sigaddset(&sigMask, SIGALRM);
    sigaddset(&sigMask, SIGCONT); // No effect if all threads block
    status = pthread_sigmask(SIG_BLOCK, &sigMask, &prevSigMask);
    CU_ASSERT_EQUAL(status, 0);
    /*
    sigset_t oldSigSet;
    blockSigCont(&oldSigSet);
    */

    const float retxTimeout = (MEAN_RESIDENCE_TIME/2.0) / 60.0;
    umm_setRetxTimeout(retxTimeout);

    sender_start(&sender, ANY); // Blocks until sender is ready
    log_flush_error();

    host_set* hostSet = lcf_newHostSet(HS_DOTTED_QUAD, LOCAL_HOST, NULL);
    CU_ASSERT_PTR_NOT_NULL_FATAL(hostSet);
    ErrorObj* errObj = lcf_addAllow(ANY, hostSet, ".*", NULL);
    CU_ASSERT_PTR_NULL_FATAL(errObj);

    receiver_init(&receiver, sender_getAddr(&sender), sender_getPort(&sender),
            ANY);
    /* Starts a receiver on a new thread */
    receiver_start(&receiver);
    log_flush_error();

    (void)sleep(2);

    sender_insertProducts();

    //(void)sleep(180);
    log_notice_q("%lu sender product-queue insertions",
            (unsigned long)NUM_PRODS);
    uint64_t numDownInserts = receiver_getNumProds(&receiver);
    log_notice_q("%lu product deletions", (unsigned long)numDeletedProds);
    log_notice_q("%lu receiver product-queue insertions",
            (unsigned long)numDownInserts);
    log_notice_q("%ld outstanding product reservations",
            receiver_getPqeCount(&receiver));
    CU_ASSERT_EQUAL(numDownInserts - numDeletedProds, NUM_PRODS);

    unsigned remaining = sleep(2);
    CU_ASSERT_EQUAL_FATAL(remaining, 0);

    log_debug_1("Stopping receiver");
    receiver_stop(&receiver);

    log_debug_1("Stopping sender");
    sender_stop(&sender);

    lcf_free();

    /*
    status = pthread_sigmask(SIG_SETMASK, &oldSigSet, NULL);
    CU_ASSERT_EQUAL(status, 0);
    */

    status = pthread_sigmask(SIG_SETMASK, &prevSigMask, NULL);
    CU_ASSERT_EQUAL(status, 0);
}

int main(
        const int    argc,
        char* const* argv)
{
    int          status = 1;
    /*
    extern int   opterr;
    extern int   optopt;
    extern char* optarg;
    */

    (void)log_init(argv[0]);
    log_set_level(LOG_LEVEL_NOTICE);

    opterr = 1; // Prevent getopt(3) from printing error messages
    for (int ch; (ch = getopt(argc, argv, "l:")) != EOF; ) {
        switch (ch) {
            case 'l': {
                (void)log_set_destination(optarg);
                break;
            }
            default: {
                log_add("Unknown option: \"%c\"", optopt);
                return 1;
            }
        }
    }

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_up7)
                    && CU_ADD_TEST(testSuite, test_down7)
                    && CU_ADD_TEST(testSuite, test_bad_subscription)
                    && CU_ADD_TEST(testSuite, test_up7_down7)
                    ) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        status = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    log_flush_error();
    log_free();

    return status;
}
