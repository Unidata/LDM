/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down7.c
 * @author: Steven R. Emmerson
 *
 * This file implements the downstream LDM-7.
 */

#include "config.h"

#include "down7.h"
#include "executor.h"
#include "inetutil.h"
#include "ldm.h"
#include "log.h"
#include "mcast_down.h"
#include "file_id_queue.h"
#include "rpc/rpc.h"
#include "rpcutil.h"
#include "vcmtp_c_api.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>

struct Down7 {
    ServAddr*       servAddr;
    char*           mcastName;
    pqueue*         pq; ///< product queue
    FileIdQueue*    rq; ///< request queue
    FileIdQueue*    oq; ///< outstanding (i.e., pending) queue
    CLIENT*         clnt;
    McastGroupInfo* mcastInfo;
    pthread_t       thread;
    pthread_cond_t  waitCond;
    pthread_mutex_t clntLock; ///< Synchronizes multiple-thread access
    int             sock;
    int             mainThreadIsActive;
    int             requestThreadIsActive;
    int             mcastThreadIsActive;
};

/**
 * Locks a client-side handle for exclusive access.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its client-side
 *                   handle locked.
 */
static void
lockClient(
    Down7* const down7)
{
    (void)pthread_mutex_lock(&down7->clntLock);
}

/**
 * Unlocks a client-side handle.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its client-side
 *                   handle unlocked.
 */
static void
unlockClient(
    Down7* const down7)
{
    (void)pthread_mutex_unlock(&down7->clntLock);
}

/**
 * Callback-function for a file (i.e., LDM data-product) that was missed by a
 * multicast downstream LDM. The file is queued for reception by other means.
 * This function must and does return immediately.
 *
 * @param[in] fileId  VCMTP file identifier of the missed file.
 * @param[in] arg     Pointer to the downstream LDM-7 object.
 */
static void
missedProdFunc(
    const VcmtpFileId fileId,
    void* const       arg)
{
    fiq_add(((Down7*)arg)->rq, fileId);
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
    const ServAddr* const           servAddr)
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
    CLIENT** const restrict    client,
    const ServAddr* const      servAddr,
    int* const restrict        socket)
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
 * Start requesting missed data-products. Entries from the request-queue are
 * removed and converted into requests for missed data-products, which are
 * asynchronously sent to the remote LDM-7.
 *
 * @param[in] arg       Pointer to the downstream LDM-7 object.
 * @retval    LDM7_RPC  Error in RPC layer. \c log_add() called.
 */
static void*
startRequestThread(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;

    for (;;) {
        VcmtpFileId  fileId;

        (void)fiq_remove(down7->rq, &fileId); /* blocks */
        lockClient(down7);
        (void)request_product_7(&fileId, down7->clnt); /* asynchronous send */

        if (clnt_stat(down7->clnt) != RPC_TIMEDOUT) {
            /*
             * "request_product_7()" uses asynchronous message-passing, so the
             * status will always be RPC_TIMEDOUT unless an error occurs.
             */
            unlockClient(down7);
            return (void*)LDM7_RPC;
        }

        unlockClient(down7);
    }

    return 0; // Eclipse IDE wants to see this
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

    lockClient(down7);
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
 * Runs an RPC service. Continues as long as an RPC message arrives before the
 * timeout occurs.
 *
 * @param[in] xprt           Pointer to the RPC service transport.
 * @retval    LDM7_TIMEDOUT  Timeout occurred.
 * @retval    LDM7_RPC       Error in RPC layer. `log_add()` called.
 *                           `svc_destroy(xprt)` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 *                           `svc_destroy(xprt)` called.
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
        timeout.tv_sec = 60;
        timeout.tv_usec = 0;

        status = select(sock+1, &fds, 0, 0, &timeout);

        if (status == 0)
            return LDM7_TIMEDOUT;

        if (status < 0) {
            LOG_SERROR1("select() error on socket %d", sock);
            svc_destroy(xprt);
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

    return 0; // Eclipse IDE wants to see this
}

/**
 * Runs the data-product receiving service. Upon return, `svc_destroy()` --
 * which calls both `svc_unregister()` and `close(sock)` -- will have been
 * called,
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
        // The following calls `svc_destroy(xprt)` on all errors except timeout.
        status = run_down7(xprt);

        if (status) {
            if (status == LDM7_TIMEDOUT) {
                // `svc_destroy(xprt)` wasn't called.
                if (testConnection(down7) == 0)
                    continue; // connection is still good

                svc_destroy(xprt);
            }
            LOG_ADD0("Connection to upstream LDM-7 is broken");
            break;
        }
    }

    return status; // Eclipse IDE wants to see this
}

/**
 * Starts the receiving thread for data-products missed by the VCMTP layer.
 *
 * NB: When this function returns, the TCP socket will have been closed.
 *
 * @param[in] arg            Pointer to the downstream LDM-7.
 * @retval    0              Success.
 * @retval    LDM7_RPC       RPC error. \c log_add() called.
 */
static void*
startReceiveThread(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    SVCXPRT*     xprt = svcfd_create(down7->sock, 0, MAX_RPC_BUF_NEEDED);
    int          status;
    char         buf[256];

    if (xprt == NULL) {
        (void)sa_format(down7->servAddr, buf, sizeof(buf));
        LOG_ADD1("Couldn't create RPC service for upstream LDM-7 at \"%s\"",
                buf);
        status = LDM7_RPC;
    }
    else {
        if (!svc_register(xprt, LDMPROG, SEVEN, ldmprog_7, 0)) {
            (void)sa_format(down7->servAddr, buf, sizeof(buf));
            LOG_ADD1("Couldn't register RPC service for upstream LDM-7 at "
                    "\"%s\"", buf);
            status = LDM7_RPC;
            /*
             * The following calls `svc_unregister()` and `close(xprt->xp_sock)`
             */
            svc_destroy(xprt);
        }
        else {
            status = run_svc(down7, xprt); // indefinite execution
            /*
             * NB: `run_svc()` calls `svc_destroy(xprt)`, which calls
             * `svc_unregister()` and `close(xprt->xp_sock)` (which is also the
             * client-side socket).
             */
        }
    } // "xprt" allocated

    return (void*)status;
}

/**
 * Starts the multicast downstream LDM.
 *
 * @param[in] arg            Pointer to the downstream LDM-7.
 * @retval    0              The multicast downstream LDM terminated
 *                           successfully.
 * @retval    LDM7_SYSTEM    System error. \c log_add() called.
 */
static void*
startMcastThread(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;

    return (void*)mdl_createAndExecute(down7->mcastInfo, down7->pq,
            missedProdFunc, down7);
}

/**
 * Starts concurrent tasks.
 *
 * @param[in] down7        Pointer to the downstream LDM-7.
 * @param[in] executor     Pointer to the execution service to use.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  Error. `log_add()` called.
 */
static int
startTasks(
    Down7* const    down7,
    Executor* const executor)
{
    int       status;

    if (ex_submit(executor, startRequestThread, down7, NULL) == NULL) {
        LOG_ADD0("Couldn't start task that requests data-products that were "
                "missed by the multicast receiver task");
        status = LDM7_SYSTEM;
    }
    else {
        if (ex_submit(executor, startMcastThread, down7, NULL) == NULL) {
            Task* task;

            LOG_SERROR0("Couldn't start task that receives multicast "
                    "data-products");
            ex_cancel(executor);
            while ((task = ex_take(executor)) != NULL)
                task_free(task);
            status = LDM7_SYSTEM;
        }
    }

    return status;
}

/**
 * Waits for a task to complete and then cancels all remaining tasks.
 *
 * @param[in] down7     Pointer to the downstream LDM-7.
 * @param[in] executor  Pointer to the execution service being used.
 * @return              Status code of the first task to complete.
 */
static int
waitOnTasks(
    Down7* const    down7,
    Executor* const executor)
{
    // TODO
    return 0;
}

/**
 * Receives data.
 *
 * @param[in] down7            Pointer to the downstream LDM-7.
 * @retval    0                Success.
 * @retval    LDM7_INTR        Execution was interrupted by a signal.
 * @retval    LDM7_TIMEDOUT    Timeout occurred.
 * @retval    LDM7_SYSTEM      System error. \c log_add() called.
 */
static int
execute(
    Down7* const down7)
{
    int             status;
    Executor* const executor = ex_new();

    if (executor == NULL) {
        LOG_ADD0("Couldn't create new execution service");
        status = LDM7_SYSTEM;
    }
    else {
        status = startTasks(down7, executor);

        if (status) {
            LOG_ADD0("Couldn't start concurrent tasks");
        }
        else {
            status = waitOnTasks(down7, executor);
        }

        ex_free(executor);
    } // "executor" allocated */

    return status;
#if 0
    pthread_t requestThread;
    int       status = pthread_create(&requestThread, NULL, startRequestThead,
            down7);

    if (status) {
        LOG_SERROR0("Couldn't start thread for requesting data-products that "
                "were missed by the VCMTP layer");
        status = LDM7_SYSTEM;
    }
    else {
        pthread_t mcastThread;

        status = pthread_create(&mcastThread, NULL, startMcastThread, down7);

        if (status) {
            LOG_SERROR0("Couldn't start thread for receiving data-products via "
                    "VCMTP");
            status = LDM7_SYSTEM;

            (void)pthread_cancel(requestThread);
            (void)pthread_join(requestThread, NULL);
        }
        else {
            status = waitOnThreads(down7, requestThread, mcastThread);
        } /* "mcastThread" allocated */
    } /* "requestThread" allocated */

    return status;
#endif
}

/**
 * Subscribes to a multicast group and receives the data.
 *
 * @param[in] down7          Pointer to the downstream LDM-7 object.
 * @retval    0              Success.
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

    lockClient(down7);
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
 * Starts executing a downstream LDM-7 on the current thread.
 *
 * @param[in] down7          Pointer to the downstream LDM-7 to be executed.
 * @retval    0              Success. All desired data was received.
 * @retval    LDM7_INVAL     Invalid port number or host identifier. \c
 *                           log_add() called.
 * @retval    LDM7_REFUSED   Remote LDM-7 refused connection. \c log_add()
 *                           called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. \c log_add() called.
 * @retval    LDM7_SYSTEM    System error. \c log_add() called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). \c log_add()
 *                           called.
 * @retval    LDM7_INVAL     Invalid multicast group name.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 */
static void*
startDown7(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    int          sock;
    int          status = newClient(&down7->clnt, down7->servAddr,
            &down7->sock);

    if (status == 0) {
        if (pthread_mutex_init(&down7->clntLock, NULL)) {
            LOG_SERROR0("Couldn't initialize mutex for client-side handle");
            clnt_destroy(down7->clnt);
            down7->clnt = NULL;
        }
        else {
            status = subscribeAndExecute(down7);

            clnt_destroy(down7->clnt);
            down7->clnt = NULL;

            (void)close(down7->sock);
            down7->sock = -1;
        }
    } /* "down7->clnt" and "down7->sock" allocated */

    return (void*)status;
}

/**
 * Returns a new downstream LDM-7.
 *
 * @param[in] servAddr   Pointer to the address of the server from which to
 *                       obtain multicast information, backlog files, and
 *                       files missed by the VCMTP layer. The client may free
 *                       upon return.
 * @param[in] mcastName  Name of multicast group to receive. The client may free
 *                       upon return.
 * @param[in] pq         Pointer to the product-queue.
 * @retval    NULL       Failure. \c log_add() called.
 * @return               Pointer to the new downstream LDM-7.
 */
Down7*
dl7_new(
    const ServAddr* const restrict servAddr,
    const char* const restrict     mcastName,
    pqueue* const restrict         pq)
{
    Down7* const down7 = LOG_MALLOC(sizeof(Down7), "downstream LDM-7");

    if (down7 == NULL)
        goto return_NULL;

    if ((down7->servAddr = sa_clone(servAddr)) == NULL)
        goto free_down7;

    if ((down7->mcastName = strdup(mcastName)) == NULL)
        goto free_servAddr;

    if ((down7->rq = fiq_new()) == NULL)
        goto free_mcastName;

    if ((down7->oq = fiq_new()) == NULL)
        goto free_requestQueue;

    down7->pq = pq;
    down7->mainThreadIsActive = 0;
    down7->requestThreadIsActive = 0;
    down7->mcastThreadIsActive = 0;
    down7->clnt = NULL;
    down7->sock = -1;
    down7->mcastInfo = NULL;

    return down7;

free_requestQueue:
    fiq_free(down7->rq);
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
 * Starts a downstream LDM-7. Returns immediately. Idempotent.
 *
 * @param[in] down7        Pointer to the downstream LDM-7 to be started.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. \c log_add() called.
 */
int
dl7_start(
    Down7* const down7)
{
    int status;

    if (down7->mainThreadIsActive) {
        status = 0;
    }
    else {
        status = pthread_create(&down7->thread, NULL, startDown7, down7);

        if (status) {
            LOG_SERROR0("Couldn't start downstream LDM-7 thread");
            status = LDM7_SYSTEM;
        }
        else {
            down7->mainThreadIsActive = 1;
        }
    }

    return status;
}

/**
 * Stops a downstream LDM-7. Returns when the downstream LDM-7 has stopped.
 * Idempotent.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to be stopped.
 */
void
dl7_stop(
    Down7* const down7)
{
    if (down7->mainThreadIsActive) {
        (void)pthread_cancel(down7->thread);
        (void)pthread_join(down7->thread, NULL);
        down7->mainThreadIsActive = 0;
    }
}

/**
 * Frees a downstream LDM-7. Stops it first if necessary.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 object to be freed or NULL.
 */
void
dl7_free(
    Down7* const down7)
{
    if (down7) {
        if (down7->mainThreadIsActive)
            dl7_stop(down7);
        if (down7->clnt) {
            clnt_destroy(down7->clnt);
            (void)pthread_mutex_destroy(&down7->clntLock);
        }
        if (down7->sock >= 0)
            close(down7->sock);
        fiq_free(down7->oq);
        fiq_free(down7->rq);
        free(down7->mcastName);
        sa_free(down7->servAddr);
        free(down7);
    }
}
