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
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "mldm_sender_manager.h"
#include "mldm_sender_map.h"
#include "pq.h"
#include "timestamp.h"

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

#define USE_SIGWAIT 0
#define CANCEL_SENDER 1

#ifndef MAX
    #define MAX(a,b) ((a) >= (b) ? (a) : (b))
#endif

typedef struct {
    SVCXPRT*              xprt;
    bool                  xprtAllocated;
} Up7;

typedef struct {
    pthread_t             thread;
    int                   sock;
} Sender;

typedef struct {
    ServiceAddr*          servAddr;
    Down7*                down7;
    pthread_t             thread;
} Receiver;

static const char* const LOCAL_HOST = "127.0.0.1";
static sigset_t          termSigSet;
#define UP7_PQ_PATHNAME   "up7_test.pq"
#define DOWN7_PQ_PATHNAME "down7_test.pq"
static const char* const pqPathnames[] = {UP7_PQ_PATHNAME, DOWN7_PQ_PATHNAME};
#if !USE_SIGWAIT
static pthread_mutex_t   mutex;
static pthread_cond_t    cond;

static int
initCondAndMutex(void)
{
    int                 status;
    pthread_mutexattr_t mutexAttr;

    status = pthread_mutexattr_init(&mutexAttr);
    if (status) {
        LOG_ERRNUM0(status, "Couldn't initialize mutex attributes");
    }
    else {
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);
        /*
         * Recursive in case `termSigHandler()` and `waitUntilDone()` execute
         * on the same thread
         */
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_RECURSIVE);

        if ((status = pthread_mutex_init(&mutex, &mutexAttr))) {
            LOG_ERRNUM0(status, "Couldn't initialize mutex");
        }
        else {
            if ((status = pthread_cond_init(&cond, NULL))) {
                LOG_ERRNUM0(status, "Couldn't initialize condition variable");
                (void)pthread_mutex_destroy(&mutex);
            }
        }

        (void)pthread_mutexattr_destroy(&mutexAttr);
    } // `mutexAttr` initialized

    return status;
}
#endif

/**
 * Only called once.
 */
static int
setup(void)
{
    int         status;

    for (int i = 0; i < sizeof(pqPathnames)/sizeof(pqPathnames[0]); i++) {
        pqueue*     pq;

        // PQ_DEFAULT => clobber any existing
        status = pq_create(pqPathnames[i], 0666, PQ_DEFAULT, 0, 1000000, 100,
                &pq);
        if (status) {
            LOG_ADD1("Couldn't create product-queue \"%s\"", pqPathnames[i]);
            break;
        }
        else {
            status = pq_close(pq);
            if (status) {
                LOG_ADD1("Couldn't close product-queue \"%s\"", pqPathnames[i]);
                break;
            }
        }
    }

    setQueuePath(UP7_PQ_PATHNAME); // so mldm_sender_manager uses upstream queue

    if (status == 0) {
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
             * group without affecting the parent process (e.g., a
             * make(1)).
             */
            (void)setpgrp();

#if !USE_SIGWAIT
            status = initCondAndMutex();
#endif
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
    int status;

    msm_destroy();

#if 0
    status = 0;
#else
    for (int i = 0; i < sizeof(pqPathnames)/sizeof(pqPathnames[0]); i++) {
        status = unlink(pqPathnames[i]);
        if (status) {
            LOG_SERROR1("Couldn't delete product-queue \"%s\"", pqPathnames[i]);
            log_log(LOG_ERR);
        }
    }
#endif

#if !USE_SIGWAIT
    (void)pthread_cond_destroy(&cond);
    (void)pthread_mutex_destroy(&mutex);
#endif

    return status;
}

#if !USE_SIGWAIT

#if 0
/**
 * Aborts the process due to an error in logic.
 */
static void abortProcess(void)
{
    LOG_ADD0("Logic error");
    log_log(LOG_ERR);
    abort();
}

static void
lockMutex(void)
{
    udebug("Locking mutex");
    int status = pthread_mutex_lock(&mutex);
    if (status) {
        LOG_ERRNUM0(status, "Couldn't lock mutex");
        abortProcess();
    }
}

static void
unlockMutex(void)
{
    udebug("Unlocking mutex");
    int status = pthread_mutex_unlock(&mutex);
    if (status) {
        LOG_ERRNUM0(status, "Couldn't unlock mutex");
        abortProcess();
    }
}

static void
setDoneCondition(void)
{
    lockMutex();

    done = 1;
    udebug("Signaling condition variable");
    int status = pthread_cond_broadcast(&cond);
    if (status) {
        LOG_ERRNUM0(status, "Couldn't signal condition variable");
        abortProcess();
    }

    unlockMutex();
}

static void
termSigHandler(
        const int sig)
{
    udebug("Caught signal %d", sig);
    setDoneCondition();
}

/**
 * @param[in]       newAction
 * @param[out]      oldAction
 * @retval 0        Success
 * @retval ENOTSUP  The SA_SIGINFO bit flag is set in the `sa_flags` field of
 *                  `newAction` and the implementation does not support either
 *                  the Realtime Signals Extension option, or the XSI Extension
 *                  option.
 */
static int
setTermSigHandling(
        struct sigaction* newAction,
        struct sigaction* oldAction)
{
    int              status;

    if (sigaction(SIGINT, newAction, oldAction) ||
            sigaction(SIGTERM, newAction, oldAction)) {
        LOG_SERROR0("sigaction() failure");
        status = errno;
    }
    else {
        status = 0;
    }

    return status;
}

static void
waitForDoneCondition(void)
{
    lockMutex();

    while (!done) {
        udebug("Waiting on condition variable");
        int status = pthread_cond_wait(&cond, &mutex);
        if (status) {
            LOG_ERRNUM0(status, "Couldn't wait on condition variable");
            abortProcess();
        }
    }

    unlockMutex();
}

static void
waitUntilDone(void)
{
    struct sigaction newAction;

    (void)sigemptyset(&newAction.sa_mask);
    newAction.sa_flags = 0;
    newAction.sa_handler = termSigHandler;

    struct sigaction oldAction;
    if (setTermSigHandling(&newAction, &oldAction)) {
        LOG_SERROR0("Couldn't set termination signal handling");
        abortProcess();
    }

    waitForDoneCondition();

    if (setTermSigHandling(&oldAction, NULL)) {
        LOG_SERROR0("Couldn't reset termination signal handling");
        abortProcess();
    }
}
#endif
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
        const int  sock)
{
    /*
     * 0 => use default read/write buffer sizes.
     * `sock` will be closed by `svc_destroy()`.
     */
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
        xprt->xp_raddr = addr;
        xprt->xp_addrlen = addrLen;
    }

    // Last argument == 0 => don't register with portmapper
    bool success = svc_register(xprt, LDMPROG, SEVEN, ldmprog_7, 0);
    CU_ASSERT_TRUE_FATAL(success);

    up7->xprt = xprt;

    return 0;
}

static void
funcCancelled(
        void* const arg)
{
    const char* funcName = (const char*)arg;
    udebug("funcCancelled(): %s() thread cancelled", funcName);
}

/**
 * Might call `svc_destroy(up7->xprt)`.
 *
 * @param[in] up7  Upstream LDM-7.
 * @retval    0    Success.
 */
static int
up7_run(
        Up7* const up7)
{
    const int     sock = up7->xprt->xp_sock;
    int           status;

    struct pollfd fds;
    fds.fd = sock;
    fds.events = POLLRDNORM;

    pthread_cleanup_push(funcCancelled, "up7_run");

    int initCancelState;
    (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &initCancelState);

    for (;;) {
        udebug("up7_run(): Calling poll()");
        status = poll(&fds, 1, -1); // `-1` => indefinite timeout

        int cancelState;
        // (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancelState);

        if (0 > status) {
            svc_destroy(up7->xprt);
            break;
        }
        if ((fds.revents & POLLERR) || (fds.revents & POLLNVAL)) {
            status = EIO;
            break;
        }
        if (fds.revents & POLLHUP) {
            status = 0;
            break;
        }
        if (fds.revents & POLLRDNORM) {
            udebug("up7_run(): Calling svc_getreqsock()");
            svc_getreqsock(sock); // calls `ldmprog_7()`
        }
        if (!FD_ISSET(sock, &svc_fdset)) {
           /*
            * The connection to the receiver was closed by the RPC layer =>
            * `svc_destroy(up7->xprt)` was called.
            */
            up7->xprt = NULL; // so others don't try to destroy it
            status = 0;
            break;
        }

        (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelState);
    }

    /*
     * In order to play nice with the caller, the cancelability state is
     * reverted to its value on entry.
     */
    (void)pthread_setcancelstate(initCancelState, &initCancelState);

    pthread_cleanup_pop(0);

    udebug("up7_run(): Returning %d", status);
    return status;
}

static void
up7_destroy(
        Up7* const up7)
{
    svc_unregister(LDMPROG, SEVEN);
    if (up7->xprt)
        svc_destroy(up7->xprt);
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
    up7_destroy((Up7*)arg); // closes `sock`
}


static int
servlet_run(
        const int servSock)
{
    /* NULL-s => not interested in receiver's address */
    int sock = accept(servSock, NULL, NULL);
    int status;

    CU_ASSERT_NOT_EQUAL_FATAL(sock, -1);

    pthread_cleanup_push(closeSocket, &sock);

    Up7 up7;
    status = up7_init(&up7, sock);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    pthread_cleanup_push(destroyUp7, &up7); // calls `up7_destroy()`

    status = up7_run(&up7); // might call `svc_destroy(up7->xprt)`
    CU_ASSERT_EQUAL(status, 0);

    pthread_cleanup_pop(1); // might call `svc_destroy(up7->xprt)`
    pthread_cleanup_pop(0); // `sock` already closed

    udebug("servlet_run(): Returning");
    return 0;
}

static void
freeLogging(
        void* const arg)
{
    log_free();
}

/**
 * Called by `pthread_create()`. The thread is cancelled by
 * `sender_terminate()`.
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
    static int    status;

    struct pollfd fds;
    fds.fd = servSock;
    fds.events = POLLIN;

    pthread_cleanup_push(freeLogging, NULL);

    for (;;) {
        status = poll(&fds, 1, -1); // `-1` => indefinite timeout

        int cancelState;
        (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancelState);

        if (0 > status)
            break;
        if (fds.revents & POLLHUP) {
            status = 0;
            break;
        }
        if (fds.revents & POLLIN) {
            status = servlet_run(servSock);

            if (status) {
                LOG_ADD0("servlet_run() failure");
                break;
            }
        }

        (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelState);
    } // `poll()` loop

    /* Because the current thread is ending: */
    (status && !done)
        ? log_log(LOG_ERR)
        : log_clear(); // don't care about errors if termination requested

    pthread_cleanup_pop(1); // calls `log_free()`

    udebug("sender_run(): Returning &%d", status);
    return &status;
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

static void
sender_insertProducts(
        Sender* const sender)
{
    pqueue* pq;
    int     status = pq_open(UP7_PQ_PATHNAME, 0, &pq);

    CU_ASSERT_EQUAL_FATAL(status, 0);

    product    prod;
    prod_info* info = &prod.info;
    char       ident[80];
    void*      data = NULL;

    info->feedtype = EXP;
    info->ident = ident;
    info->origin = "localhost";
    (void)memset(info->signature, 0, sizeof(info->signature));
    srand48(1234567890);

    for (int i = 0; i < 150; i++) {
        const unsigned size = 100000*drand48() + 0.5;
        const ssize_t  nbytes = snprintf(ident, sizeof(ident), "%d", i);

        CU_ASSERT_TRUE_FATAL(nbytes >= 0 && nbytes < sizeof(ident));
        status = set_timestamp(&info->arrival);
        CU_ASSERT_EQUAL_FATAL(status, 0);
        info->seqno = i;
        (void)memcpy(info->signature, &i, sizeof(i));
        info->sz = size;

        data = realloc(data, size);
        CU_ASSERT_PTR_NOT_NULL(data);
        prod.data = data;

        status = pq_insert(pq, &prod);
        CU_ASSERT_EQUAL_FATAL(status, 0);
        char buf[1024];
        LOG_ADD1("Inserted: prodInfo=\"%s\"",
                s_prod_info(buf, sizeof(buf), info, 1));
        log_log(LOG_INFO);

        struct timespec duration;
        duration.tv_sec = 0;
        duration.tv_nsec = 1000000; // 1 ms
        status = nanosleep(&duration, NULL);
        CU_ASSERT_EQUAL_FATAL(status, 0);
    }

    free(data);
    status = pq_close(pq);
    CU_ASSERT_EQUAL_FATAL(status, 0);
}

static int
sender_terminate(
        Sender* const sender)
{
    int status = 0;

#if CANCEL_SENDER
    udebug("Canceling sender thread");
    status = pthread_cancel(sender->thread);
    UASSERT(status == 0);
#else
    udebug("Writing to termination pipe");
    status = write(sender->fds[1], &status, sizeof(int));
    CU_ASSERT_NOT_EQUAL_FATAL(status, -1);
#endif

    void* statusPtr;

    udebug("Joining sender thread");
    status = pthread_join(sender->thread, &statusPtr);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    if (statusPtr == PTHREAD_CANCELED) {
        status = 0;
    }
    else {
        status = *(int*)statusPtr;
        CU_ASSERT_EQUAL_FATAL(status, 0);
    }

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
    static int      status;

    status = down7_start(receiver->down7);
    CU_ASSERT_EQUAL(status, LDM7_SHUTDOWN);

    // setDoneCondition();

    /* Because at end of thread: */
    done ? log_clear() : log_log(LOG_ERR);
    log_free();

    return &status;
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

    receiver->down7 = down7_new(servAddr, ANY, DOWN7_PQ_PATHNAME);
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
    udebug("Calling down7_stop()");
    int status = down7_stop(receiver->down7);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    void* statusPtr;
    udebug("Joining receiver thread");
    status = pthread_join(receiver->thread, &statusPtr);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    CU_ASSERT_PTR_NOT_NULL_FATAL(statusPtr);
    CU_ASSERT_NOT_EQUAL_FATAL(statusPtr, PTHREAD_CANCELED);
    status = *(int*)statusPtr;
    CU_ASSERT_EQUAL(status, LDM7_SHUTDOWN);

    udebug("Calling down7_free()");
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

        udebug("Setting SIGTERM action to ignore");
        newSigact.sa_flags = 0;
        newSigact.sa_handler = SIG_IGN;
        status = sigaction(SIGTERM, &newSigact, &oldSigact);
        CU_ASSERT_EQUAL_FATAL(status, 0);

        udebug("Sending SIGTERM to process group");
        status = kill(0, SIGTERM);
        CU_ASSERT_EQUAL_FATAL(status, 0);

        udebug("Restoring SIGTERM action");
        status = sigaction(SIGTERM, &oldSigact, NULL);
        CU_ASSERT_EQUAL(status, 0);
    }

    /* Reap the terminated multicast sender. */
    {
        udebug("Reaping multicast sender child process");
        const pid_t wpid = wait(&status);
        CU_ASSERT_TRUE_FATAL(wpid > 0);
        CU_ASSERT_TRUE(WIFEXITED(status));
        CU_ASSERT_EQUAL(WEXITSTATUS(status), 0);

        status = mlsm_terminated(wpid);
        CU_ASSERT_EQUAL(status, 0);
    }
}

static void
test_up7(
        void)
{
    Sender   sender;
    int      status;

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

    status = mlsm_addPotentialSender(mcastInfo, 2, "127.0.0.1");
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

static void
test_down7(
        void)
{
    Receiver receiver;
    int      status;

    done = 0;

    /* Starts a receiver on a new thread */
    status = receiver_spawn(&receiver, LOCAL_HOST, 38800);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);

#if 1
    sleep(1);
    done = 1;
#else
    waitUntilDone();
#endif

    status = receiver_terminate(&receiver);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
}

static void
test_up7_down7(
        void)
{
    Sender   sender;
    Receiver receiver;
    int      status;

    done = 0;

    ServiceAddr* mcastServAddr;
    status = sa_new(&mcastServAddr, "224.0.0.1", 38800);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    ServiceAddr* ucastServAddr;
    status = sa_new(&ucastServAddr, LOCAL_HOST, 0);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);

    McastInfo* mcastInfo;
    status = mi_new(&mcastInfo, ANY, mcastServAddr, ucastServAddr);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);
    sa_free(ucastServAddr);
    sa_free(mcastServAddr);

    status = mlsm_addPotentialSender(mcastInfo, 2, "127.0.0.1");
    log_log(LOG_ERR);
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

    (void)sleep(1);

    sender_insertProducts(&sender);

#if USE_SIGWAIT
    (void)sigwait(&termSigSet, &status);
    done = 1;
#elif 0
    waitUntilDone();
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL_FATAL(status, 0);
#else
    (void)sleep(1);
#endif

    udebug("Terminating receiver");
    status = receiver_terminate(&receiver);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    udebug("Terminating sender");
    status = sender_terminate(&sender);
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);

    udebug("Terminating multicast sender");
    terminateMcastSender();

    udebug("Clearing multicast LDM sender map");
    status = mlsm_clear();
    log_log(LOG_ERR);
    CU_ASSERT_EQUAL(status, 0);
}

int main(
        const int argc,
        const char* const * argv)
{
    int status = 1;

    log_initLogging(basename(argv[0]), LOG_INFO, LOG_LDM);

    if (CUE_SUCCESS == CU_initialize_registry()) {
        CU_Suite* testSuite = CU_add_suite(__FILE__, setup, teardown);

        if (NULL != testSuite) {
            if (CU_ADD_TEST(testSuite, test_up7)
                    && CU_ADD_TEST(testSuite, test_down7)
                    && CU_ADD_TEST(testSuite, test_up7_down7)
                    ) {
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
