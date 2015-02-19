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
#include "globals.h"
#include "inetutil.h"
#include "log.h"
#include "mcast_info.h"
#include "mldm_sender_manager.h"
#include "mldm_sender_map.h"
#include "pq.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <stdbool.h>
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
    int                   termFd;
} Up7;

typedef struct {
    pthread_t             thread;
    int                   sock;
    int                   fds[2]; // for termination pipe(2)
} Sender;

typedef struct {
    ServiceAddr*          servAddr;
    Down7*                down7;
    pthread_t             thread;
} Receiver;

static const char* const LOCAL_HOST = "127.0.0.1";
static sigset_t          termSigSet;
static const char* const PQ_PATHNAME = "up7_down7_test.pq";

/**
 * Only called once.
 */
static int
setup(void)
{
    pqueue* pq;
    int     status = pq_create(PQ_PATHNAME, 0666, 0, 0, 1000000, 100, &pq);

    if (status) {
        LOG_ADD1("Couldn't create product-queue \"%s\"", PQ_PATHNAME);
    }
    else {
        status = pq_close(pq);
        if (status) {
            LOG_ADD1("Couldn't close product-queue \"%s\"", PQ_PATHNAME);
        }
        else {
            setQueuePath(PQ_PATHNAME);

            status = msm_init();
            if (status) {
                LOG_ADD0("msm_init() failure");
            }
            else {
                (void)sigemptyset(&termSigSet);
                (void)sigaddset(&termSigSet, SIGINT);
                (void)sigaddset(&termSigSet, SIGTERM);
                /*
                 * The following allows a SIGTERM to be sent to the process
                 * group without affecting the parent process (e.g., a make(1)).
                 */
                (void)setpgrp();
            }
        }
    }

    return status;
}

/**
 * Only called once.
 */
static int
teardown(void)
{
    msm_destroy();

    int status = unlink(PQ_PATHNAME);
    if (status) {
        LOG_SERROR1("Couldn't delete product-queue \"%s\"", PQ_PATHNAME);
        log_log(LOG_ERR);
    }

    return status;
}

static void
blockTermSigs(void)
{
    (void)pthread_sigmask(SIG_BLOCK, &termSigSet, NULL);
}

#if 0
static void
termSigHandler(
        const int sig)
{
    udebug("Caught signal %d", sig);
}

static int
setTermSigHandler(
        struct sigaction* oldAction)
{
    struct sigaction newAction;
    int              status;

    (void)sigemptyset(&newAction.sa_mask);
    newAction.sa_flags = 0;
    newAction.sa_handler = termSigHandler;

    if (sigaction(SIGINT, &newAction, oldAction) ||
            sigaction(SIGTERM, &newAction, oldAction)) {
        LOG_SERROR0("sigaction() failure");
        status = errno;
    }
    else {
        status = 0;
    }

    return status;
}

static int
restoreTermSigHandling(
        struct sigaction* oldAction)
{
    int              status;

    if (sigaction(SIGINT, oldAction, NULL) ||
            sigaction(SIGTERM, oldAction, NULL)) {
        LOG_SERROR0("sigaction() failure");
        status = errno;
    }
    else {
        status = 0;
    }

    return status;
}
#endif

/**
 * Closes the socket on failure.
 *
 * @param[in] up7     The upstream LDM-7 to be initialized.
 * @param[in] sock    The socket for the upstream LDM-7.
 * @param[in] termFd  Termination file-descriptor.
 * @retval    0       Success.
 */
static int
up7_init(
        Up7* const up7,
        const int  sock,
        const int  termFd)
{
    /* 0 => use default read/write buffer sizes */
    SVCXPRT* const xprt = svcfd_create(sock, 0, 0);
    CU_ASSERT_PTR_NOT_EQUAL_FATAL(xprt, NULL);

    /*
     * Set the remote address of the RPC server-side transport because
     * `svcfd_create()` doesn't.
     */
    {
        struct sockaddr_in addr;
        socklen_t          addrLen = sizeof(addr);

        int status = getpeername(sock, &addr, &addrLen);
        CU_ASSERT_EQUAL_FATAL(status, 0);
        CU_ASSERT_EQUAL_FATAL(addrLen, sizeof(addr));
        CU_ASSERT_EQUAL_FATAL(addr.sin_family, AF_INET);
        (void)memcpy(&xprt->xp_raddr, &addr, addrLen);
        xprt->xp_addrlen = addrLen;
    }

    bool success = svc_register(xprt, LDMPROG, SEVEN, ldmprog_7, 0);
    CU_ASSERT_TRUE_FATAL(success);

    up7->xprt = xprt;
    up7->termFd = termFd;

    return 0;
}

/**
 * @param[in] up7  Upstream LDM-7.
 * @retval    0    Success. `up7->termFd` was ready for reading or the RPC layer
 *                 closed the connection.
 */
static int
up7_run(
        Up7* const up7)
{
    const int sock = up7->xprt->xp_sock;
    const int termFd = up7->termFd;
    const int width = MAX(sock, termFd) + 1;
    int       status;

    for (;;) {
        fd_set    readfds;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(termFd, &readfds);

        /* NULL timeout argument => indefinite wait */
        status = select(width, &readfds, NULL, NULL, NULL);

        int initState;
        (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &initState);

        if (0 > status)
            break;
        if (FD_ISSET(termFd, &readfds)) {
            /* Termination requested */
            (void)read(termFd, &status, sizeof(int));
            status = 0;
            break;
        }
        if (FD_ISSET(sock, &readfds))
            svc_getreqset(&readfds); // calls `ldmprog_7()`
        if (!FD_ISSET(sock, &svc_fdset)) {
           /* The connection to the receiver was closed by the RPC layer */
            status = 0;
            break;
        }

        int ignoredState;
        (void)pthread_setcancelstate(initState, &ignoredState);
    }

    return status;
}

static void
up7_destroy(
        Up7* const up7)
{
    svc_unregister(LDMPROG, SEVEN);
    up7->xprt = NULL;
}

static void
closeSocket(
        void* const arg)
{
    int sock = *(int*)arg;

    if (close(sock))
        LOG_SERROR1("Couldn't close socket %d", sock);
}

static void
destroyUp7(
        void* const arg)
{
    up7_destroy((Up7*)arg); // closes `servSock`
}


static int
servlet_run(
        const int servSock,
        const int termFd)
{
    /* NULL-s => not interested in receiver's address */
    const int sock = accept(servSock, NULL, NULL);
    int       status;

    CU_ASSERT_NOT_EQUAL_FATAL(sock, -1);

    pthread_cleanup_push(closeSocket, &sock);

    Up7 up7;
    status = up7_init(&up7, sock, termFd); // Closes `servSock` on failure
    CU_ASSERT_EQUAL_FATAL(status, 0);

    pthread_cleanup_push(destroyUp7, &up7); // calls `up7_destroy()`

    status = up7_run(&up7);
    CU_ASSERT_EQUAL(status, 0);

    pthread_cleanup_pop(true); // calls `up7_destroy()`
    pthread_cleanup_pop(false); // `servSock` already closed

    return 0;
}

static void
freeLogging(
        void* const arg)
{
    log_free();
}

/**
 * Called by `pthread_create()`.
 *
 * @param[in] arg  Pointer to sender.
 * @retval    &0   Success. Input end of sender's termination pipe(2) is closed.
 */
static void*
sender_run(
        void* const arg)
{
    Sender* const sender = (Sender*)arg;
    const int     servSock = sender->sock;
    const int     termFd = sender->fds[0];
    const int     width = MAX(servSock, termFd) + 1;
    int           status;

    pthread_cleanup_push(freeLogging, NULL);

    for (;;) {
        fd_set readfds;

        FD_ZERO(&readfds);
        FD_SET(servSock, &readfds);
        FD_SET(termFd, &readfds);

        /* NULL timeout argument => indefinite wait */
        status = select(width, &readfds, 0, NULL, NULL);

        if (0 > status)
            break;
        if (FD_ISSET(termFd, &readfds)) {
            /* Termination requested */
            (void)read(termFd, &status, sizeof(int));
            status = 0;
            break;
        }
        if (FD_ISSET(servSock, &readfds)) {
            status = servlet_run(servSock, termFd);

            if (status) {
                LOG_ADD0("servlet_run() failure");
                break;
            }
        } // sender's socket ready for reading
    } // `select()` loop

    /* Because the current thread is ending: */
    (status && !done)
        ? log_log(LOG_ERR)
        : log_clear(); // don't care about errors if termination requested

    pthread_cleanup_pop(true); // calls `log_free()`

    static int staticStatus;
    staticStatus = status;
    return &staticStatus;
}

static int
senderSock_init(
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
 * Starts executing the sender on a new thread.
 *
 * @param[in] sender  The sender to be initialized.
 * @retval    0       Success. `*sender` is initialized and the sender is
 *                    executing.
 */
static int
sender_spawn(
        Sender* const sender)
{
    int status = senderSock_init(&sender->sock);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = pipe(sender->fds);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = pthread_create(&sender->thread, NULL, sender_run, sender);
    CU_ASSERT_EQUAL_FATAL(status, 0);

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

static int
sender_terminate(
        Sender* const sender)
{
    int status = 0;

    status = write(sender->fds[1], &status, sizeof(int));
    CU_ASSERT_NOT_EQUAL_FATAL(status, -1);

    void* statusPtr;

    status = pthread_join(sender->thread, &statusPtr);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    status = *(int*)statusPtr;
    CU_ASSERT_EQUAL_FATAL(status, 0);

    (void)close(sender->fds[0]);
    (void)close(sender->fds[1]);
   (void)close(sender->sock);

    return 0;
}

/**
 * Starts a receiver on the current thread. Called by `pthread_create()`.
 *
 * @param[in] arg  Argument
 * @retval    &0   Success.
 */
static void*
receiver_start(
        void* const arg)
{
    Receiver* const receiver = (Receiver*)arg;
    int             status = down7_run(receiver->down7);

    CU_ASSERT_EQUAL_FATAL(status, 0);

    /* Because at end of thread: */
    done ? log_clear() : log_log(LOG_ERR);
    log_free();

    static int staticStatus;
    staticStatus = status;
    return &staticStatus;
}

/**
 * Starts a receiver on a new thread.
 *
 * @param[out] receiver  Receiver.
 * @param[in]  addr      Address of sender: either hostname or IPv4 address.
 * @param[in]  port      Port number of sender in host byte-order.
 * @retval 0   Success.
 */
static int
receiver_spawn(
        Receiver* const restrict   receiver,
        const char* const restrict addr,
        const unsigned short       port)
{
    ServiceAddr* servAddr;
    int          status = sa_new(&servAddr, addr, port);

    CU_ASSERT_EQUAL_FATAL(status, 0);

    receiver->down7 = down7_new(servAddr, ANY, getQueuePath());
    CU_ASSERT_PTR_NOT_EQUAL_FATAL(receiver->down7, NULL);

    status = pthread_create(&receiver->thread, NULL, receiver_start,
            receiver);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    sa_free(servAddr);

    return status;
}

/**
 * Terminates a receiver by stopping it and destroying its resources.
 *
 * @param[in] receiver  The receiver to be terminated.
 * @retval    0       Success.
 */
static int
receiver_terminate(
        Receiver* const receiver)
{
    int status = down7_stop(receiver->down7);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    void* statusPtr;
    status = pthread_join(receiver->thread, &statusPtr);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    status = *(int*)statusPtr;
    CU_ASSERT_EQUAL(status, 0);

    status = down7_free(receiver->down7);
    CU_ASSERT_EQUAL(status, 0);

    return status;
}

static void
terminateMcastSender(void)
{
    int status;

    /*
     * Terminate the multicast sender by sending a SIGTERM to the process group.
     */
    {
        struct sigaction oldSigact;
        struct sigaction newSigact;
        status = sigemptyset(&newSigact.sa_mask);

        CU_ASSERT_EQUAL_FATAL(status, 0);
        newSigact.sa_flags = 0;
        newSigact.sa_handler = SIG_IGN;
        status = sigaction(SIGTERM, &newSigact, &oldSigact);
        CU_ASSERT_EQUAL_FATAL(status, 0);

        status = kill(0, SIGTERM);
        CU_ASSERT_EQUAL_FATAL(status, 0);

        status = sigaction(SIGTERM, &oldSigact, NULL);
        CU_ASSERT_EQUAL(status, 0);
    }

    /* Reap the terminated multicast sender. */
    {
        const pid_t wpid = wait(&status);
        CU_ASSERT_TRUE_FATAL(wpid > 0);
        CU_ASSERT_TRUE(WIFEXITED(status));
        CU_ASSERT_EQUAL(WEXITSTATUS(status), 0);

        status = mlsm_terminated(wpid);
        CU_ASSERT_EQUAL(status, 0);
    }
}

static void
test_up7_down7(
        void)
{
    Sender   sender;
    Receiver receiver;
    int      status;

    blockTermSigs();
    done = 0;

    ServiceAddr* mcastServAddr;
    status = sa_new(&mcastServAddr, "224.0.0.1", 38800);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    ServiceAddr* ucastServAddr;
    status = sa_new(&ucastServAddr, LOCAL_HOST, 0);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    McastInfo* mcastInfo;
    status = mi_new(&mcastInfo, ANY, mcastServAddr, ucastServAddr);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    sa_free(ucastServAddr);
    sa_free(mcastServAddr);

    status = mlsm_addPotentialSender(mcastInfo);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    mi_free(mcastInfo);

    /* Starts a sender on a new thread */
    status = sender_spawn(&sender);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    /* Starts a receiver on a new thread */
    status = receiver_spawn(&receiver, sender_getAddr(&sender),
            sender_getPort(&sender));
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);

#if 1
    sleep(1);
#else
    (void)sigwait(&termSigSet, &status);
#endif
    done = 1;

    status = receiver_terminate(&receiver);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = sender_terminate(&sender);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    terminateMcastSender();

    status = mlsm_clear();
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
}

static void
test_down7(
        void)
{
    Receiver receiver;
    int      status;

    blockTermSigs();
    done = 0;

    /* Starts a receiver on a new thread */
    status = receiver_spawn(&receiver, LOCAL_HOST, 38800);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    sleep(1);
    done = 1;

    status = receiver_terminate(&receiver);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
}

static void
test_up7(
        void)
{
    Sender   sender;
    int      status;

    blockTermSigs();
    done = 0;

    ServiceAddr* mcastServAddr;
    status = sa_new(&mcastServAddr, "224.0.0.1", 38800);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    ServiceAddr* ucastServAddr;
    status = sa_new(&ucastServAddr, LOCAL_HOST, 0);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    McastInfo* mcastInfo;
    status = mi_new(&mcastInfo, ANY, mcastServAddr, ucastServAddr);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    sa_free(ucastServAddr);
    sa_free(mcastServAddr);

    status = mlsm_addPotentialSender(mcastInfo);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    mi_free(mcastInfo);

    /* Starts a sender on a new thread */
    status = sender_spawn(&sender);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    sleep(1);
    done = 1;

    status = sender_terminate(&sender);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    status = mlsm_clear();
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
}

int main(
        const int argc,
        const char* const * argv)
{
    int status = 1;

    log_initLogging(basename(argv[0]), LOG_NOTICE, LOG_LDM);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_up7) &&
                    CU_ADD_TEST(testSuite, test_down7) &&
                    CU_ADD_TEST(testSuite, test_up7_down7)) {
                CU_basic_set_mode(CU_BRM_VERBOSE);
                (void) CU_basic_run_tests();
            }
        }

        status = CU_get_number_of_tests_failed();
        CU_cleanup_registry();
    }

    log_log(LOG_ERR);
    log_free();

    return status;
}
