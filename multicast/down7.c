/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down7.c
 * @author: Steven R. Emmerson
 *
 * This file implements a downstream LDM-7.
 */

#include "config.h"

#include "down7.h"
#include "executor.h"
#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_down.h"
#include "file_id_queue.h"
#include "rpc/rpc.h"
#include "rpcutil.h"
#include "timestamp.h"
#include "vcmtp_c_api.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>

/**
 * The data structure of a downstream LDM-7:
 */
struct Down7 {
    ServAddr*             servAddr;      ///< socket address of remote LDM-7
    char*                 mcastName;     ///< name of multicast group
    /** Queue of missed-but-not-yet-requested data-products: */
    FileIdQueue*          missedQ;
    /** Queue of requested-but-not-yet-received data-products: */
    FileIdQueue*          requestedQ;
    CLIENT*               clnt;          ///< client-side RPC handle
    McastGroupInfo*       mcastInfo;     ///< information on multicast group
    Mdl*                  mdl;           ///< multicast downstream LDM
    pthread_t             receiveThread; ///< thread for receiving products
    pthread_t             requestThread; ///< thread for requesting products
    pthread_t             mcastThread;   ///< thread for multicast reception
    pthread_mutex_t       waitMutex;     ///< mutex for waiting
    pthread_cond_t        waitCond;      ///< condition-variable for waiting
    /** Synchronizes multiple-thread access to client-side RPC handle */
    pthread_mutex_t       clntMutex;
    int                   exitStatus;    ///< status of first exited task
    int                   sock;          ///< socket with remote LDM-7
    volatile sig_atomic_t canceled;      ///< whether canceled
    volatile sig_atomic_t taskExited;    ///< whether a task exited
    bool                  mcastWorking;  ///< product received via multicast?
    /**
     * Signature of the first data-product received by the associated multicast
     * LDM during the current session.
     */
    signaturet            firstMcast;
    /**
     * Signature of the last data-product received by the associated multicast
     * LDM during the current session.
     */
    signaturet            lastMcast;
    /**
     * Signature of the last data-product received by the associated multicast
     * downstream LDM during the previous session.
     */
    signaturet            lastPrevMcast;
};

/**
 * Locks the wait-lock of a downstream LDM-7 for exclusive access.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its wait-lock
 *                   locked.
 */
static void
lockWait(
    Down7* const down7)
{
    (void)pthread_mutex_lock(&down7->waitMutex);
}

/**
 * Unlocks the wait-lock of a downstream LDM-7.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its wait-lock
 *                   unlocked.
 */
static void
unlockWait(
    Down7* const down7)
{
    (void)pthread_mutex_unlock(&down7->waitMutex);
}

/**
 * Locks the client-side handle of a downstream LDM-7 for exclusive access.
 *
 * @param[in] down7    Pointer to the downstream LDM-7 to have its client-side
 *                     handle locked.
 * @retval    0        Success.
 * @retval    EDEADLK  A deadlock condition was detected or the current thread
 *                     already owns the mutex.
 */
static int
lockClient(
    Down7* const down7)
{
    return pthread_mutex_lock(&down7->clntMutex);
}

/**
 * Unlocks the client-side handle of a downstream LDM-7.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its client-side
 *                   handle unlocked.
 */
static void
unlockClient(
    Down7* const down7)
{
    (void)pthread_mutex_unlock(&down7->clntMutex);
}

/**
 * Performs common exit actions for a task of a downstream LDM-7:
 *   -# Logs outstanding error messages if the downstream LDM-7 wasn't canceled;
 *   -# Frees log-message resources of the current thread;
 *   -# Sets the status of the first task to exit a downstream LDM-7;
 *   -# Sets the task-exited flag of the downstream LDM-7; and
 *   -# Signals the wait condition-variable.
 *
 * @param[in] arg     Pointer to the downstream LDM-7.
 * @param[in] status  Return status of the exiting task.
 */
static void
taskExit(
    Down7* const down7,
    int const    status)
{
    /*
     * Finish with logging.
     */
    down7->canceled ? log_clear() : log_log(LOG_ERR);
    log_free();

    /*
     * Inform the managing thread.
     */
    lockWait(down7);
    if (down7->exitStatus < 0)
        down7->exitStatus = status;
    down7->taskExited = 1;
    pthread_cond_signal(&down7->waitCond);
    unlockWait(down7);
}

/**
 * Sets a socket address to correspond to a TCP connection to a server on
 * an Internet host
 *
 * @param[out] sockAddr      Pointer to the socket address object to be set.
 * @param[in]  useIPv6       Whether or not to use IPv6.
 * @param[in]  servAddr      Pointer to the address of the server.
 * @retval     0             Success. \c *sockAddr is set.
 * @retval     LDM7_INVAL    Invalid port number or host identifier. \c
 *                           log_add() called.
 * @retval     LDM7_IPV6     IPv6 not supported. \c log_add() called.
 * @retval     LDM7_SYSTEM   System error. \c log_add() called.
 */
static int
getSockAddr(
    struct sockaddr* const restrict sockAddr,
    const int                       useIPv6,
    const ServAddr* const restrict  servAddr)
{
    int            status;
    char           servName[6];
    unsigned short port = sa_getPort(servAddr);

    if (port == 0 || snprintf(servName, sizeof(servName), "%u", port) >=
            sizeof(servName)) {
        LOG_ADD1("Invalid port number: %u", port);
        status = LDM7_INVAL;
    }
    else {
        struct addrinfo   hints;
        struct addrinfo*  addrInfo;
        const char* const hostId = sa_getHostId(servAddr);

        (void)memset(&hints, 0, sizeof(hints));
        hints.ai_family = useIPv6 ? AF_INET6 : AF_INET;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_socktype = SOCK_STREAM;
        /*
         * AI_ADDRCONFIG means that the local system must be configured with an
         * IP address of the specified family.
         */
        hints.ai_flags = AI_NUMERICSERV | AI_ADDRCONFIG;

        status = getaddrinfo(hostId, servName, &hints, &addrInfo);

        if (status != 0) {
            /*
             * Possible values: EAI_FAMILY, EAI_AGAIN, EAI_FAIL, EAI_MEMORY,
             * EIA_NONAME, EAI_SYSTEM, EAI_OVERFLOW
             */
            LOG_ADD4("Couldn't get %s address for host \"%s\", port %u. "
                    "Status=%d", useIPv6 ? "IPv6" : "IPv4", hostId, port,
                    status);
            status = (useIPv6 && status == EAI_FAMILY)
                    ? LDM7_IPV6
                    : (status == EAI_NONAME)
                      ? LDM7_INVAL
                      : LDM7_SYSTEM;
        }
        else {
            *sockAddr = *addrInfo->ai_addr;
            freeaddrinfo(addrInfo);
        } /* "addrInfo" allocated */
    } /* valid port number */

    return status;
}

/**
 * Returns a socket that's connected to an Internet server via TCP.
 *
 * @param[in]  useIPv6        Whether or not to use IPv6.
 * @param[in]  servAddr       Pointer to the address of the server.
 * @param[out] sock           Pointer to the socket to be set. The client should
 *                            call \c close(*sock) when it's no longer needed.
 * @param[out] sockAddr       Pointer to the socket address object to be set.
 * @retval     0              Success. \c *sock and \c *sockAddr are set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier. \c
 *                            log_add() called.
 * @retval     LDM7_IPV6      IPv6 not supported. \c log_add() called.
 * @retval     LDM7_REFUSED   Remote LDM-7 refused connection. \c log_add()
 *                            called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. \c log_add()
 *                            called.
 * @retval     LDM7_SYSTEM    System error. \c log_add() called.
 */
static int
getSocket(
    const int                       useIPv6,
    const ServAddr* const           servAddr,
    int* const restrict             sock,
    struct sockaddr* const restrict sockAddr)
{
    struct sockaddr addr;
    int             status = getSockAddr(&addr, useIPv6, servAddr);

    if (status == 0) {
        const char* const addrFamilyId = useIPv6 ? "IPv6" : "IPv4";
        const int         fd = socket(addr.sa_family, SOCK_STREAM, IPPROTO_TCP);

        if (fd == -1) {
            LOG_SERROR1("Couldn't create %s TCP socket", addrFamilyId);
            status = (useIPv6 && errno == EAFNOSUPPORT)
                    ? LDM7_IPV6
                    : LDM7_SYSTEM;
        }
        else {
            if (connect(fd, &addr, sizeof(addr))) {
                LOG_SERROR3("Couldn't connect %s TCP socket to host "
                        "\"%s\", port %u", addrFamilyId, sa_getHostId(servAddr),
                        sa_getPort(servAddr));
                (void)close(fd);
                status = (errno == ETIMEDOUT)
                        ? LDM7_TIMEDOUT
                        : (errno == ECONNREFUSED)
                          ? LDM7_REFUSED
                          : LDM7_SYSTEM;
            }
            else {
                *sock = fd;
                *sockAddr = addr;
            }
        } /* "fd" allocated */
    } /* "addr" is set */

    return status;
}

/**
 * Returns a client-side RPC handle to a remote LDM-7.
 *
 * @param[out] client         Address of pointer to client-side handle. The
 *                            client should call \c clnt_destroy(*client) when
 *                            it is no longer needed.
 * @param[in]  servAddr       Pointer to the address of the server.
 * @param[out] socket         Pointer to the socket to be set. The client should
 *                            call \c close(*socket) when it's no longer needed.
 * @retval     0              Success. \c *client and \c *sock are set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier. \c
 *                            log_add() called.
 * @retval     LDM7_REFUSED   Remote LDM-7 refused connection. \c log_add()
 *                            called.
 * @retval     LDM7_RPC       RPC error. \c log_add() called.
 * @retval     LDM7_SYSTEM    System error. \c log_add() called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. \c log_add() called.
 * @retval     LDM7_UNAUTH    Not authorized. \c log_add() called.
 */
static int
newClient(
    CLIENT** const restrict        client,
    const ServAddr* const restrict servAddr,
    int* const restrict            socket)
{
    int             sock;
    struct sockaddr sockAddr;
    /* Try IPv6 first */
    int             status = getSocket(1, servAddr, &sock, &sockAddr);

    if (status == LDM7_IPV6) {
        /* Try IPv4 */
        status = getSocket(0, servAddr, &sock, &sockAddr);
    }

    if (status == 0) {
        /*
         * "clnttcp_create()" expects a pointer to a "struct sockaddr_in", but
         * a pointer to a "struct sockaddr_in6" object may be used if the socket
         * value is non-negative and the port field of the socket address
         * structure is non-zero. Both conditions are true at this point.
         */
        CLIENT* const clnt = clnttcp_create((struct sockaddr_in*)&sockAddr,
                LDMPROG, SEVEN, &sock, 0, 0);

        if (clnt == NULL) {
            LOG_SERROR3("Couldn't create RPC client for host \"%s\", "
                    "port %u: %s", sa_getHostId(servAddr), sa_getPort(servAddr),
                    clnt_spcreateerror(""));
            (void)close(sock);
            status = clntStatusToLdm7Status(rpc_createerr.cf_stat);
        }
        else {
            *client = clnt;
            *socket = sock;
        }
    } /* "sock" allocated */

    return status;
}

/**
 * Tests the connection to an upstream LDM-7 by sending a no-op message to it.
 *
 * @param[in] down7     Pointer to the downstream LDM-7.
 * @retval    0         Success. The connection is still good.
 * @retval    LDM7_RPC  RPC error. `log_add()` called.
 */
static int
testConnection(
    Down7* const down7)
{
    int status;

    (void)lockClient(down7);
    test_connection_7(NULL, down7->clnt);

    if (clnt_stat(down7->clnt) == RPC_TIMEDOUT) {
        /*
         * "test_connection_7()" uses asynchronous message-passing, so the
         * status will always be RPC_TIMEDOUT unless an error occurs.
         */
        unlockClient(down7);
        status = 0;
    }
    else {
	LOG_ADD1("%s", clnt_errmsg(down7->clnt));
        unlockClient(down7);
        status = LDM7_RPC;
    }

    return status;
}

/**
 * Runs an RPC service. Doesn't return until no RPC message arrives within the
 * timeout interval or an error occurs.
 *
 * @param[in] xprt           Pointer to the RPC service transport.
 * @retval    LDM7_TIMEDOUT  Timeout occurred.
 * @retval    LDM7_RPC       Error in RPC layer. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 */
static int
run_down7(
    SVCXPRT* const xprt)
{
    for (;;) {
        const int      sock = xprt->xp_sock;
        fd_set         fds;
        struct timeval timeout;
        int            status;

        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeout.tv_sec = 3600;
        timeout.tv_usec = 0;

        status = select(sock+1, &fds, 0, 0, &timeout); // cancellation point

        if (status == 0)
            return LDM7_TIMEDOUT;

        if (status < 0) {
            LOG_SERROR1("select() error on socket %d", sock);
            return LDM7_SYSTEM;
        }

        /*
         * The socket is ready for reading.
         */
        svc_getreqsock(sock); /* process RPC message */

        if (FD_ISSET(sock, &svc_fdset))
            continue;

        /*
         * The RPC layer closed the socket and destroyed the associated
         * SVCXPRT structure.
         */
         log_add("svc_run(): RPC layer closed connection");
         return LDM7_RPC;
    }

    return 0; // Eclipse IDE wants to see a return
}

/**
 * Runs the data-product receiving service. Blocks until an unrecoverable error
 * occurs.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @param[in] xprt           Pointer to the server-side transport object.
 * @retval    LDM7_TIMEDOUT  A timeout occurred. `log_add()` called.
 * @retval    LDM7_RPC       An RPC error occurred. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 */
static int
run_svc(
    Down7* const restrict   down7,
    SVCXPRT* const restrict xprt)
{
    int status;

    for (;;) {
        status = run_down7(xprt);

        if (status == LDM7_TIMEDOUT && (status = testConnection(down7)) == 0)
            continue; // connection is still good

        LOG_ADD0("Connection to upstream LDM-7 is broken");
        break;
    }

    return status; // Eclipse IDE wants to see a return
}

/**
 * Requests a data-product that was missed by the multicast downstream LDM.
 *
 * @param[in] down7       Pointer to the downstream LDM-7.
 * @param[in] fileId      VCMTP file-ID of missed data-product.
 * @retval    0           Success. A data-product was requested.
 * @retval    LDM7_RPC    RPC error. `log_add()` called.
 */
static int
requestProduct(
    Down7* const      down7,
    VcmtpFileId const fileId)
{
    int               status;
    CLIENT* const     clnt = down7->clnt;

    (void)lockClient(down7);
    (void)request_product_7((VcmtpFileId*)&fileId, clnt); // asynchronous send

    if (clnt_stat(clnt) != RPC_TIMEDOUT) {
        /*
         * The status will always be RPC_TIMEDOUT unless an error occurs
         * because `request_product_7()` uses asynchronous message-passing.
         */
        LOG_ADD1("%s", clnt_errmsg(clnt));
        status = LDM7_RPC;
    }

    unlockClient(down7);

    return status;
}

/**
 * Requests the backlog of data-products from the previous session. The backlog
 * comprises all products since the last product received by the associated
 * multicast downstream LDM from the previous session (or the time-offset if
 * that product isn't found) to the first product received by the associated
 * multicast downstream LDM of this session (or the current time if that product
 * isn't found).
 *
 * NB: If this session ends before all backlog products are received, then the
 * backlog products that weren't received will never be received.
 *
 * This function blocks until the client-side handle is available.
 *
 * @param[in] arg  Pointer to downstream LDM-7.
 */
static void*
requestSessionBacklog(
    void* const restrict arg)
{
    Down7* const down7 = (Down7*)arg;
    BacklogSpec  spec;

    (void)memcpy(spec.after, down7->lastPrevMcast, sizeof(signaturet));
    (void)memcpy(spec.before, down7->firstMcast, sizeof(signaturet));
    // TODO spec.timeOffset =

    (void)lockClient(down7);
    (void)request_backlog_7(&spec, down7->clnt); // asynchronous message passing
    (void)unlockClient(down7);

    return NULL;
}

/**
 * Makes a single request for data from the upstream LDM-7 associated with a
 * downstream LDM-7.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @retval    0              Success.
 * @retval    LDM7_CANCELED  The not-yet-requested queue has been canceled.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 * @retval    LDM7_RPC       Error in RPC layer. `log_add()` called.
 */
inline static int
makeRequest(
    Down7* const down7)
{
    int            status;
    VcmtpFileId    fileId;

    /*
     * The semantics and order of the following actions are necessary to
     * preserve the meaning of the two queues and to ensure that all missed
     * data-products are received following a restart.
     */
    if (fiq_peek(down7->missedQ, &fileId)) {
        LOG_ADD0("The queue of missed data-products has been canceled");
        status = LDM7_CANCELED;
    }
    else {
        if (fiq_add(down7->requestedQ, fileId)) {
            LOG_ADD0("Couldn't add VCMTP file-ID to requested-queue");
            status = LDM7_SYSTEM;
        }
        else {
            if ((status = requestProduct(down7, fileId)) != 0) {
                LOG_ADD0("Couldn't request missed data-product");
                (void)fiq_removeTail(down7->requestedQ);
            }
            else {
                (void)fiq_removeHead(down7->missedQ);
                status = 0;
            } // product successfully requested
        } // file-ID added to requested-but-unreceived queue
    } // have a peeked-at file-ID from the missed-queue

    return status;
}

/**
 * Starts the task of a downstream LDM-7 that requests data-products that were
 * missed by the downstream multicast receiver. Entries from the request-queue
 * are removed and converted into requests for missed data-products, which are
 * asynchronously sent to the remote LDM-7. Blocks until the request-queue is
 * canceled or an unrecoverable error occurs.
 *
 * @param[in] arg            Pointer to the downstream LDM-7 object.
 * @retval    LDM7_RPC       Error in RPC layer. \c log_add() called.
 * @retval    LDM7_CANCELED  The request-queue was canceled.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 */
static void*
startRequester(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    int          status;

    do {
        status = makeRequest(down7);
    } while (status == 0);

    taskExit(down7, status);

    return (void*)status; // Eclipse IDE wants to see a return
}

/**
 * Cleanly stops the executing task of a downstream LDM-7 that's requesting
 * data-products that were missed by the multicast receiver by canceling the
 * queue of missed files and shutting down the socket to the remote LDM-7 for
 * writing.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 whose requesting task is
 *                   to be stopped.
 */
static void
stopRequester(
    Down7* const down7)
{
    (void)fiq_cancel(down7->missedQ);
    (void)shutdown(down7->sock, SHUT_WR);
}

/**
 * Starts the task of a downstream LDM-7 that receives data-products that were
 * missed by the VCMTP layer.
 *
 * NB: When this function returns, the TCP socket will have been closed.
 *
 * @param[in] arg            Pointer to the downstream LDM-7.
 * @retval    0              Success.
 * @retval    LDM7_RPC       RPC error. \c log_add() called.
 * @retval    LDM7_TIMEDOUT  A timeout occurred. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 */
static void*
startReceiver(
    void* const arg)
{
    Down7* const    down7 = (Down7*)arg;
    ServAddr* const servAddr = down7->servAddr;
    SVCXPRT*        xprt = svcfd_create(down7->sock, 0, MAX_RPC_BUF_NEEDED);
    int             status;
    char            buf[256];

    if (xprt == NULL) {
        (void)sa_format(servAddr, buf, sizeof(buf));
        LOG_ADD1("Couldn't create RPC service for receiving data-products from "
                "upstream LDM-7 at \"%s\"", buf);
        status = LDM7_RPC;
    }
    else {
        if (!svc_register(xprt, LDMPROG, SEVEN, ldmprog_7, 0)) {
            (void)sa_format(servAddr, buf, sizeof(buf));
            LOG_ADD1("Couldn't register RPC service for receiving "
                    "data-products from upstream LDM-7 at \"%s\"", buf);
            status = LDM7_RPC;
        }
        else {
            status = run_svc(down7, xprt); // indefinite execution
        }

        /*
         * The following will call `svc_unregister()` and (effectively)
         * `close(down7->sock)`, which will close the client-side socket.
         */
        svc_destroy(xprt);
    } // "xprt" allocated

    taskExit(down7, status);

    return (void*)status;
}

/**
 * Cleanly stops an executing task that's receiving data-products that were
 * missed by the multicast receiver by shutting down the socket to the remote
 * LDM-7 for reading.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 whose receiving task is to
 *                   be stopped.
 */
static void
stopReceiver(
    Down7* const down7)
{
    (void)shutdown(down7->sock, SHUT_RD);
}

/**
 * Starts the task of a downstream LDM-7 that receives data-products via
 * multicast. Blocks until the multicast downstream LDM is stopped
 *
 * @param[in] arg            Pointer to the downstream LDM-7.
 * @retval    LDM7_CANCELED  The multicast downstream LDM was stopped.
 * @retval    LDM7_SYSTEM    System error. \c log_add() called.
 */
static void*
startMcaster(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    int          status;
    Mdl* const   mdl = mdl_new(pq, down7->mcastInfo, down7);

    if (mdl == NULL) {
        LOG_ADD0("Couldn't create a new multicast downstream LDM");
        status = LDM7_SYSTEM;
    }
    else {
        down7->mdl = mdl;
        status = mdl_start(down7->mdl);
    }

    taskExit(down7, status);

    return (void*)status;
}

/**
 * Terminates all tasks of a downstream LDM-7. Undefined behavior results if
 * called from a signal handler.
 *
 * @param[in] down7  Pointer to the downstream LDM-7.
 * @return           The status of the first task to exit.
 */
static int
terminateTasks(
    Down7* const down7)
{
    mdl_stop(down7->mdl);
    stopRequester(down7);
    stopReceiver(down7);

    (void)pthread_join(down7->mcastThread, NULL);
    (void)pthread_join(down7->requestThread, NULL);
    (void)pthread_join(down7->receiveThread, NULL);

    return down7->exitStatus;
}

/**
 * Starts the concurrent tasks of a downstream LDM-7.
 *
 * @param[in] down7        Pointer to the downstream LDM-7.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  Error. `log_add()` called.
 */
static int
startTasks(
    Down7* const    down7)
{
    int       status = LDM7_SYSTEM;

    if (pthread_create(&down7->receiveThread, NULL, startReceiver, down7)) {
        LOG_ADD0("Couldn't start task that receives data-products that were "
                "missed by the multicast receiver task");
    }
    else if (pthread_create(&down7->requestThread, NULL, startRequester, down7)) {
        LOG_ADD0("Couldn't start task that requests data-products that were "
                "missed by the multicast receiver task");
    }
    else if (pthread_create(&down7->mcastThread, NULL, startMcaster, down7)) {
        LOG_ADD0("Couldn't start multicast receiver task");
    }
    else {
        status = 0;
    }

    if (status)
        terminateTasks(down7);

    return status;
}

/**
 * Waits for all tasks of a downstream LDM-7 to complete. Blocks until one task
 * terminates or the downstream LDM-7 is canceled, then terminates all remaining
 * tasks and returns.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @retval    LDM7_CANCELED  The downstream LDM-7 was canceled.
 * @return                   The status of the first task to exit.
 */
static int
waitOnTasks(
    Down7* const down7)
{
    int status;

    lockWait(down7);
    while (!down7->canceled && !down7->taskExited)
        pthread_cond_wait(&down7->waitCond, &down7->waitMutex);
    unlockWait(down7);

    status = terminateTasks(down7);

    return down7->canceled ? LDM7_CANCELED : status;
}

/**
 * Receives data for a downstream LDM-7. Blocks until the LDM-7 is canceled or
 * an unrecoverable error occurs.
 *
 * @param[in] down7            Pointer to the downstream LDM-7.
 * @retval    LDM7_SYSTEM      System error. \c log_add() called.
 * @retval    LDM7_CANCELED    The downstream LDM-7 was canceled.
 * @retval    LDM7_RPC         RPC error. \c log_add() called.
 * @retval    LDM7_TIMEDOUT    A timeout occurred. `log_add()` called.
 */
static int
execute(
    Down7* const down7)
{
    int status = startTasks(down7);

    if (status) {
        LOG_ADD0("Couldn't start downstream LDM-7 tasks");
    }
    else {
        status = waitOnTasks(down7);
    }

    return status;
}

/**
 * Subscribes a downstream LDM-7 to a multicast group and receives the data.
 * Blocks until the LDM-7 is canceled or an error occurs.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @retval    LDM7_CANCELED  LDM-7 was canceled.
 * @retval    LDM7_TIMEDOUT  Timeout occurred. \c log_add() called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). \c
 *                           log_add() called.
 * @retval    LDM7_INVAL     Invalid multicast group name.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 * @retval    LDM7_SYSTEM    System error. \c log_add() called.
 */
static int
subscribeAndExecute(
    Down7* const down7)
{
    int                status;
    SubscriptionReply* reply;

    (void)lockClient(down7);
    reply = subscribe_7(down7->mcastName, down7->clnt);

    if (reply == NULL) {
	LOG_ADD1("%s", clnt_errmsg(down7->clnt));
	status = clntStatusToLdm7Status(clnt_stat(down7->clnt));
        unlockClient(down7);
    }
    else {
        unlockClient(down7);

        if (reply->status == 0) {
            /*
             * NB: The simple assignment to "down7->mcastInfo" works because
             * the right-hand-side won't be freed until after "execute()".
             * Otherwise, something like "mcastInfo_clone()" would have to be
             * created and used.
             */
            down7->mcastInfo = &reply->SubscriptionReply_u.groupInfo;
            status = execute(down7);
        }
        xdr_free(xdr_SubscriptionReply, (char*)reply);
    } /* "reply" allocated */

    return status;
}

/**
 * Executes a downstream LDM-7. Blocks until the LDM-7 is canceled or an
 * error occurs.
 *
 * @param[in] down7          Pointer to the downstream LDM-7 to be executed.
 * @retval    LDM7_CANCELED  LDM-7 was canceled.
 * @retval    LDM7_INVAL     Invalid port number or host identifier. \c
 *                           log_add() called.
 * @retval    LDM7_REFUSED   Remote LDM-7 refused connection. \c log_add()
 *                           called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. \c log_add() called.
 * @retval    LDM7_SYSTEM    System error. \c log_add() called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). \c log_add()
 *                           called.
 * @retval    LDM7_INVAL     Invalid multicast group name. `log_add()` called.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 *                           `log_add()` called.
 */
static int
runDown7Once(
    Down7* const down7)
{
    int status = newClient(&down7->clnt, down7->servAddr, &down7->sock);

    if (status == 0) {
        status = subscribeAndExecute(down7);

        clnt_destroy(down7->clnt); // won't close externally-created socket
        (void)close(down7->sock); // likely closed by server-side receiver
    } // "down7->clnt" and "down7->sock" allocated

    return status;
}

/**
 * Waits a short time. Blocks until the time period is up or the downstream
 * LDM-7 is canceled.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @retval    0              Timeout occurred.
 * @retval    LDM7_CANCELED  The downstream LDM-7 was canceled.
 */
static int
nap(
    Down7* const down7)
{
    struct timespec absTime;

    absTime.tv_sec = time(NULL) + 60; // a time in the future
    absTime.tv_nsec = 0;

    lockWait(down7);
    while (!down7->canceled) {
        if (pthread_cond_timedwait(&down7->waitCond, &down7->waitMutex,
                &absTime) == ETIMEDOUT)
            break;
    }
    unlockWait(down7);

    return down7->canceled ? LDM7_CANCELED : 0;
}

/**
 * Inserts a data-product into the product-queue and then unlocks the
 * product-queue. Logs any errors directly.
 *
 * @param[in] pq    Pointer to the product-queue. Must be locked.
 * @param[in] prod  Pointer to the data-product to be inserted into the product-
 *                  queue.
 */
static void
insertAndUnlock(
    pqueue* const restrict  pq,
    product* const restrict prod)
{
    int status = pq_insert(pq, prod);

    (void)pq_unlock(pq);

    if (status) {
        char buf[256];

        if (status == PQUEUE_DUP) {
            uinfo("Duplicate data-product: %s",
                s_prod_info(buf, sizeof(buf), &prod->info, ulogIsDebug()));
        }
        else {
            uwarn("Product too big for queue: %s",
                s_prod_info(buf, sizeof(buf), &prod->info, ulogIsDebug()));
        }
    }
}

/**
 * Returns the metadata of the last data-product received by a multicast
 * downstream LDM during its previous session.
 *
 * @param[in] servAddr   The address of the upstream LDM-7 from which the
 *                       multicast downstream LDM received data-products.
 * @param[in] mcastName  The name of the multicast group that the multicast
 *                       downstream LDM received.
 * @retval    NULL       No such information is available.
 * @return               The metadata of the last data-product received by the
 *                       multicast downstream LDM associated with the given
 *                       parameters during its previous session.
 */
static prod_info*
getLastPrevMcast(
    const ServAddr* const restrict servAddr,
    const char* const restrict     mcastName,
    signaturet* const restrict     lastPrevMcast)
{
    // TODO

    return NULL;
}

/**
 * Processes a data-product from a remote LDM-7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM-7.
 *
 * @param[in] prod  Pointer to the data-product.
 */
static void
deliver_product(
    product* const restrict prod)
{
    int status = pq_lock(pq);

    if (status) {
        LOG_ADD1("Couldn't lock product-queue: %s", strerror(status));
        log_log(LOG_ERR);
    }
    else {
        insertAndUnlock(pq, prod);
    } // product-queue is locked
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new downstream LDM-7.
 *
 * @param[in] servAddr   Pointer to the address of the server from which to
 *                       obtain multicast information, backlog files, and
 *                       files missed by the VCMTP layer. The client may free
 *                       upon return.
 * @param[in] mcastName  Name of multicast group to receive. The client may free
 *                       upon return.
 * @retval    NULL       Failure. \c log_add() called.
 * @return               Pointer to the new downstream LDM-7.
 */
Down7*
dl7_new(
    const ServAddr* const restrict servAddr,
    const char* const restrict     mcastName)
{
    Down7* const down7 = LOG_MALLOC(sizeof(Down7), "downstream LDM-7");

    if (down7 == NULL)
        goto return_NULL;

    if ((down7->servAddr = sa_clone(servAddr)) == NULL) {
        char buf[256];

        (void)sa_format(servAddr, buf, sizeof(buf));
        LOG_ADD1("Couldn't clone server address \"%s\"", buf);
        goto free_down7;
    }

    if ((down7->mcastName = strdup(mcastName)) == NULL) {
        LOG_SERROR1("Couldn't duplicate multicast group name \"%s\"",
                mcastName);
        goto free_servAddr;
    }

    if ((down7->missedQ = fiq_new()) == NULL) {
        LOG_ADD0("Couldn't create queue of missed data-products");
        goto free_mcastName;
    }

    if ((down7->requestedQ = fiq_new()) == NULL) {
        LOG_ADD0("Couldn't create queue of requested data-products");
        goto free_missedQ;
    }

    if (pthread_cond_init(&down7->waitCond, NULL)) {
        LOG_ADD0("Couldn't initialize condition-variable for waiting");
        goto free_requestedQ;
    }

    if (pthread_mutex_init(&down7->waitMutex, NULL)) {
        LOG_ADD0("Couldn't initialize mutex for waiting");
        goto free_waitCond;
    }

    if (pthread_mutex_init(&down7->clntMutex, NULL)) {
        LOG_SERROR0("Couldn't initialize mutex for client-side handle");
        goto free_waitMutex;
    }

    down7->clnt = NULL;
    down7->sock = -1;
    down7->mcastInfo = NULL;
    down7->canceled = 0;
    down7->taskExited = 0;
    down7->exitStatus = -1;
    down7->mdl = NULL;
    down7->mcastWorking = false;
    (void)memset(down7->lastMcast, 0, sizeof(signaturet));
    getLastPrevMcast(servAddr, mcastName, &down7->lastPrevMcast);

    return down7;

free_waitMutex:
    pthread_mutex_destroy(&down7->waitMutex);
free_waitCond:
    pthread_cond_destroy(&down7->waitCond);
free_requestedQ:
    fiq_free(down7->requestedQ);
free_missedQ:
    fiq_free(down7->missedQ);
free_mcastName:
    free(down7->mcastName);
free_servAddr:
    sa_free(down7->servAddr);
free_down7:
    free(down7);
return_NULL:
    return NULL;
}

/**
 * Starts a downstream LDM-7. Blocks until the downstream LDM-7 is canceled or
 * an unrecoverable error occurs.
 *
 * @param[in] down7        Pointer to the downstream LDM-7 to be started.
 * @retval    LDM_CANCELED LDM-7 was canceled. `log_clear()` called.
 * @retval    LDM_SYSTEM   System error. `log_add()` called.
 */
int
dl7_start(
    Down7* const down7)
{
    int status;

    do {
        status = runDown7Once(down7);

        if (status == LDM7_SYSTEM || status == LDM7_CANCELED)
            break;

        log_log(LOG_WARNING);
    } while ((status = nap(down7)) == 0);

    if (status == LDM7_CANCELED)
        log_clear();

    return status; // Eclipse IDE wants to see a return
}

/**
 * Queues a data-product that that was missed by the multicast downstream LDM
 * associated with a downstream LDM-7 for reception via unicast TCP from the
 * associated upstream LDM-7. This function must and does return immediately.
 *
 * @param[in] down7   Pointer to the downstream LDM-7.
 * @param[in] fileId  VCMTP file identifier of the missed file.
 */
void
dl7_missedProduct(
    Down7* const      down7,
    const VcmtpFileId fileId)
{
    /*
     * Cancellation of the operation of the VCMTP file-identifier queue is
     * ignored because nothing can be done about it at this point and no harm
     * should result.
     */
    (void)fiq_add(down7->missedQ, fileId);
}

/**
 * Tracks the last data-product to be successfully received by the multicast
 * downstream LDM associated with a downstream LDM-7. Called by the multicast
 * downstream LDM. This function must not and doesn't block.
 *
 * @param[in] down7  Pointer to the downstream LDM-7.
 * @param[in] last   Pointer to the metadata of the last data-product to be
 *                   successfully received by the associated multicast
 *                   downstream LDM. Caller may free when it's no longer needed.
 */
void
dl7_lastReceived(
    Down7* const restrict           down7,
    const prod_info* const restrict last)
{
    (void)memcpy(down7->lastMcast, last->signature, sizeof(signaturet));
    if (!down7->mcastWorking) {
        pthread_t backlogThread;

        (void)memcpy(down7->firstMcast, last->signature, sizeof(signaturet));
        down7->mcastWorking = true;
        pthread_create(&backlogThread, NULL, requestSessionBacklog, down7);
        pthread_detach(backlogThread);
    }
}

/**
 * Processes a data-product from a remote LDM-7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM-7 because it was missed by the
 * multicast receiver. Does not reply. Called by the RPC dispatcher
 * `svc_getreqsock()`.
 *
 * @param[in] missedProd  Pointer to the missed data-product.
 * @param[in] rqstp       Pointer to the RPC service-request.
 */
void*
deliver_product_7_svc(
    MissedProduct* const restrict  missedProd,
    struct svc_req* const restrict rqstp)
{
    deliver_product(&missedProd->prod);
    return NULL; /* don't reply */
}

/**
 * Processes a backlog data-product from a remote LDM-7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM-7 because it was missed during the
 * previous session. Does not reply. Called by the RPC dispatcher
 * `svc_getreqsock()`.
 *
 * @param[in] prod        Pointer to the backlog data-product.
 * @param[in] rqstp       Pointer to the RPC service-request.
 */
void*
deliver_backlog_7_svc(
    product* const restrict        prod,
    struct svc_req* const restrict rqstp)
{
    deliver_product(prod);
    return NULL; /* don't reply */
}

/**
 * Stops a downstream LDM-7 cleanly. Returns immediately. Idempotent. Undefined
 * behavior results if called from a signal handler.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to be stopped.
 */
void
dl7_stop(
    Down7* const down7)
{
    down7->canceled = 1;
    (void)pthread_cond_signal(&down7->waitCond);
}

/**
 * Frees a downstream LDM-7.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 object to be freed or NULL.
 */
void
dl7_free(
    Down7* const down7)
{
    if (down7) {
        (void)pthread_mutex_destroy(&down7->clntMutex);
        (void)pthread_mutex_destroy(&down7->waitMutex);
        (void)pthread_cond_destroy(&down7->waitCond);
        fiq_free(down7->requestedQ);
        fiq_free(down7->missedQ);
        free(down7->mcastName);
        sa_free(down7->servAddr);
        free(down7);
    }
}
