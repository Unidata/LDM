/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down7.c
 * @author: Steven R. Emmerson
 *
 * This file implements a downstream LDM-7, which executes on its own threads
 * to
 *     - Subscribe to a data-stream from an upstream LDM-7;
 *     - Receive multicast data-products;
 *     - Request data-products that were missed by the multicast receiver, and
 *     - Receive those requested data-products.
 */

#include "config.h"

#include "down7.h"
#include "executor.h"
#include "prod_index_queue.h"
#include "globals.h"
#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast.h"
#include "mldm_receiver.h"
#include "mldm_receiver_memory.h"
#include "pq.h"
#include "rpc/rpc.h"
#include "rpcutil.h"
#include "timestamp.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>

#ifndef MAX
    #define MAX(a,b) ((a) >= (b) ? (a) : (b))
#endif

/**
 * Key for getting the pointer to a downstream LDM-7 that's associated with a
 * thread:
 */
static pthread_key_t  down7Key;
/**
 * Lockout for initializing `down7Key`:
 */
static pthread_once_t down7KeyControl = PTHREAD_ONCE_INIT;

typedef enum {
    DOWN7_INITIALIZED,
    DOWN7_RUNNING,
    DOWN7_STOP_REQUESTED,
    DOWN7_STOPPED
} Down7State;

/**
 * The data structure of a downstream LDM-7:
 */
struct Down7 {
    pqueue*               pq;            ///< pointer to the product-queue
    ServiceAddr*          servAddr;      ///< socket address of remote LDM-7
    feedtypet             feedtype;      ///< feed-expression of multicast group
    CLIENT*               clnt;          ///< client-side RPC handle
    McastInfo*            mcastInfo;     ///< information on multicast group
    Mlr*                  mlr;           ///< multicast LDM receiver
    /** Persistent multicast session memory */
    McastSessionMemory*   msm;
    /** Thread for receiving unicast products */
    pthread_t             ucastRecvThread;
    pthread_t             requestThread; ///< thread for requesting products
    /** Thread for receiving multicast products */
    pthread_t             mcastRecvThread;
    pthread_mutex_t       stateMutex;    ///< mutex for changing state
    pthread_cond_t        termCond;      ///< condition-variable for terminating
    /** Synchronizes multiple-thread access to client-side RPC handle */
    pthread_mutex_t       clntMutex;
    int                   exitStatus;    ///< status of first exited task
    int                   sock;          ///< socket with remote LDM-7
    bool                  mcastWorking;  ///< product received via multicast?
    /**
     * Signature of the first data-product received by the associated multicast
     * LDM receiver during the current session.
     */
    signaturet            firstMcast;
    /**
     * Signature of the last data-product received by the associated multicast
     * LDM receiver during the previous session.
     */
    signaturet            prevLastMcast;
    /** Whether or not `prevLastMcast` is set: */
    bool                  prevLastMcastSet;
    int                   fds[2];        ///< Termination pipe(2)
    Down7State            state;         ///< Downstream LDM-7 state
};

static int stopTasks(Down7* down7);

/**
 * Locks the state-lock of a downstream LDM-7.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its state-lock
 *                   locked.
 */
static void
lockState(
    Down7* const down7)
{
    if (pthread_mutex_lock(&down7->stateMutex))
        serror("Couldn't lock downstream LDM-7 state-mutex");
}

/**
 * Unlocks the state-lock of a downstream LDM-7.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its state-lock
 *                   unlocked.
 */
static void
unlockState(
    Down7* const down7)
{
    if (pthread_mutex_unlock(&down7->stateMutex))
        serror("Couldn't unlock downstream LDM-7 state-mutex");
}

static Down7State
getState(
        Down7* const down7)
{
    Down7State state;

    lockState(down7);
    state = down7->state;
    unlockState(down7);

    return state;
}

static Down7State
setState(
        Down7* const     down7,
        const Down7State newState)
{
    Down7State oldState;

    lockState(down7);
    oldState = down7->state;
    down7->state = newState;
    unlockState(down7);

    return oldState;
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
 *   -# Logs outstanding error messages if the downstream LDM-7 wasn't stopped;
 *   -# Frees log-message resources of the current thread;
 *   -# Sets the status of the first task to exit a downstream LDM-7;
 *   -# Terminates all tasks; and
 *
 * @param[in] arg     Pointer to the downstream LDM-7.
 * @param[in] status  Status of the exiting task.
 */
static void
taskExit(
    Down7* const down7,
    int const    status)
{
    lockState(down7);

    if (DOWN7_STOP_REQUESTED == down7->state || done) {
        log_clear(); // Because end of thread
    }
    else {
        if (0 == down7->exitStatus)
            down7->exitStatus = status;
        log_log(LOG_ERR); // Because end of thread
    }

    if (DOWN7_RUNNING == down7->state) {
        if (stopTasks(down7)) {
            LOG_ADD0("Couldn't terminate other tasks of downstream LDM-7");
            log_log(LOG_ERR);
        }
    }

    log_free(); // Because end of thread
    unlockState(down7);
}

/**
 * Returns a socket that's connected to an Internet server via TCP.
 *
 * @param[in]  servAddr       Pointer to the address of the server.
 * @param[out] sock           Pointer to the socket to be set. The client should
 *                            call `close(*sock)` when it's no longer needed.
 * @param[out] sockAddr       Pointer to the socket address object to be set.
 * @retval     0              Success. `*sock` and `*sockAddr` are set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier.
 *                            `log_start()` called.
 * @retval     LDM7_IPV6      IPv6 not supported. `log_start()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection. `log_start()`
 *                            called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. `log_start()`
 *                            called.
 * @retval     LDM7_SYSTEM    System error. `log_start()` called.
 */
static int
getSocket(
    const ServiceAddr* const restrict       servAddr,
    int* const restrict                     sock,
    struct sockaddr_storage* const restrict sockAddr)
{
    struct sockaddr_storage addr;
    socklen_t               sockLen;
    int                     status = sa_getInetSockAddr(servAddr, false, &addr,
            &sockLen);

    if (status == 0) {
        const int         useIPv6 = addr.ss_family == AF_INET6;
        const char* const addrFamilyId = useIPv6 ? "IPv6" : "IPv4";
        const int         fd = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP);

        if (fd == -1) {
            LOG_SERROR1("Couldn't create %s TCP socket", addrFamilyId);
            status = (useIPv6 && errno == EAFNOSUPPORT)
                    ? LDM7_IPV6
                    : LDM7_SYSTEM;
        }
        else {
            if (connect(fd, (struct sockaddr*)&addr, sockLen)) {
                LOG_SERROR3("Couldn't connect %s TCP socket to \"%s\", port %hu",
                        addrFamilyId, sa_getInetId(servAddr),
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
        } /* "fd" is open */
    } /* "addr" is set */

    return status;
}

/**
 * Returns a client-side RPC handle to a remote LDM-7.
 *
 * @param[out] client         Address of pointer to client-side handle. The
 *                            client should call `clnt_destroy(*client)` when
 *                            it is no longer needed.
 * @param[in]  servAddr       Pointer to the address of the server.
 * @param[out] socket         Pointer to the socket to be set. The client should
 *                            call `close(*socket)` when it's no longer needed.
 * @retval     0              Success. `*client` and `*sock` are set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier.
 *                            `log_start()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection. `log_start()`
 *                            called.
 * @retval     LDM7_RPC       RPC error. `log_start()` called.
 * @retval     LDM7_SYSTEM    System error. `log_start()` called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. `log_start()`
 *                            called.
 * @retval     LDM7_UNAUTH    Not authorized. `log_start()` called.
 */
static int
newClient(
    CLIENT** const restrict           client,
    const ServiceAddr* const restrict servAddr,
    int* const restrict               socket)
{
    int                     sock;
    struct sockaddr_storage sockAddr;
    int                     status = getSocket(servAddr, &sock, &sockAddr);

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
                    "port %hu: %s", sa_getInetId(servAddr),
                    sa_getPort(servAddr), clnt_spcreateerror(""));
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

#if 0
/**
 * Tests the connection to an upstream LDM-7 by sending a no-op message to it.
 *
 * @param[in] down7     Pointer to the downstream LDM-7.
 * @retval    0         Success. The connection is still good.
 * @retval    LDM7_RPC  RPC error. `log_start()` called.
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
	LOG_START1("test_connection_7() failure: %s", clnt_errmsg(down7->clnt));
        unlockClient(down7);
        status = LDM7_RPC;
    }

    return status;
}
#endif

/**
 * Runs an RPC-based server. Destroys and unregisters the service transport.
 * Doesn't return until an error occurs or termination is externally requested.
 *
 * @param[in]     xprt           Pointer to the RPC service transport. Will be
 *                               destroyed upon return.
 * @param[in,out] termFd         Termination file-descriptor. When this
 *                               descriptor is ready for reading, the function
 *                               returns.
 * @retval        0              Success. `termFd` was ready for reading or
 *                               the RPC layer closed the connection..
 * @retval        LDM7_RPC       Error in RPC layer. `log_start()` called.
 * @retval        LDM7_SYSTEM    System error. `log_start()` called.
 * @retval        LDM7_TIMEDOUT  Time-out occurred.
 */
static int
run_svc(
    SVCXPRT* const                        xprt,
    const int                             termFd)
{
    const int sock = xprt->xp_sock;
    const int width = MAX(sock, termFd) + 1;
    int       status;

    for (;;) {
        fd_set fds;

        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        FD_SET(termFd, &fds);

        /* `NULL` timeout argument => indefinite wait */
        udebug("down7.c:run_svc(): Calling select(): sock=%d, termFd=%d", sock,
                termFd);
        status = select(width, &fds, NULL, NULL, NULL);

        if (0 > status) {
            LOG_SERROR2("down7.c:run_svc(): select() error on socket %d or "
                    "termination-pipe %d", sock, termFd);
            svc_destroy(xprt);
            status = LDM7_SYSTEM;
            break;
        }
        if (FD_ISSET(termFd, &fds)) {
            /* Termination requested */
            (void)read(termFd, &status, sizeof(int));
            svc_destroy(xprt);
            status = 0;
            break;
        }
        if (FD_ISSET(sock, &fds))
            svc_getreqsock(sock); // Process RPC message. Calls ldmprog_7()
        if (!FD_ISSET(sock, &svc_fdset)) {
           /* The RPC layer destroyed the service transport */
            status = 0;
            break;
        }
    }

    return status; // Eclipse IDE wants to see a return
}

/**
 * Runs the RPC-based data-product receiving service of a downstream LDM-7.
 * Destroys and unregisters the service transport. Doesn't return until an
 * error occurs or termination is externally requested.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @param[in] xprt           Pointer to the server-side transport object. Will
 *                           be destroyed upon return.
 * @retval    LDM7_RPC       An RPC error occurred. `log_start()` called.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 */
static int
run_down7_svc(
    Down7* const restrict   down7,
    SVCXPRT* const restrict xprt)
{
    /*
     * The RPC-based server doesn't know its associated downstream LDM-7;
     * therefore, a thread-specific pointer to the downstream LDM-7 is set to
     * provide context to the server.
     */
    int status = pthread_setspecific(down7Key, down7);

    if (status) {
        LOG_ERRNUM0(status,
                "Couldn't set thread-specific pointer to downstream LDM-7");
        svc_destroy(xprt);
        status = LDM7_SYSTEM;
    }
    else {
        if (status) {
            LOG_SERROR0("Couldn't create termination pipe(2)");
            svc_destroy(xprt);
            status = LDM7_SYSTEM;
        }
        else {
            /*
             * The following executes until an error occurs or termination is
             * externally requested. It destroys and unregisters the service
             * transport, which will close the downstream LDM-7's client socket.
             */
            status = run_svc(xprt, down7->fds[0]); // indefinite timeout
            unotice("Downstream LDM-7 server terminated");
        }
    } // thread-specific pointer to downstream LDM-7 is set

    return status;
}

/**
 * Requests a data-product that was missed by the multicast LDM receiver.
 *
 * @param[in] down7       Pointer to the downstream LDM-7.
 * @param[in] prodId      LDM product-ID of missed data-product.
 * @retval    0           Success. A data-product was requested.
 * @retval    LDM7_RPC    RPC error. `log_start()` called.
 */
static int
requestProduct(
    Down7* const         down7,
    VcmtpProdIndex const iProd)
{
    int    status;
#   define CLNT (down7->clnt)

    (void)lockClient(down7);
    (void)request_product_7((VcmtpProdIndex*)&iProd, CLNT); // asynchronous send

    if (clnt_stat(CLNT) != RPC_TIMEDOUT) {
        /*
         * The status will always be RPC_TIMEDOUT unless an error occurs
         * because `request_product_7()` uses asynchronous message-passing.
         */
        LOG_START1("request_product_7() failure: %s", clnt_errmsg(CLNT));
        status = LDM7_RPC;
    }
    else {
        status = 0;
    }

    unlockClient(down7);

    return status;
}

/**
 * Requests the backlog of data-products from the previous session. The backlog
 * comprises all products since the last product received by the associated
 * multicast LDM receiver from the previous session (or the time-offset if
 * that product isn't found) to the first product received by the associated
 * multicast LDM receiver of this session (or the current time if that product
 * isn't found). Called by `pthread_create()`.
 *
 * NB: If the current session ends before all backlog products have been
 * received, then the backlog products that weren't received will never be
 * received.
 *
 * This function blocks until the client-side handle is available.
 *
 * @param[in] arg       Pointer to downstream LDM-7.
 * @retval    0         Success.
 * @retval    LDM7_RPC  Error in RPC layer. `log_log()` called.
 */
static void*
requestSessionBacklog(
    void* const restrict arg)
{
    Down7* const down7 = (Down7*)arg;
    BacklogSpec  spec;

    if (down7->prevLastMcastSet)
        (void)memcpy(spec.after, down7->prevLastMcast, sizeof(signaturet));
    spec.afterIsSet = down7->prevLastMcastSet;
    (void)memcpy(spec.before, down7->firstMcast, sizeof(signaturet));
    spec.timeOffset = getTimeOffset();

    (void)lockClient(down7);
    (void)request_backlog_7(&spec, down7->clnt);
    if (clnt_stat(down7->clnt) != RPC_TIMEDOUT) {
        /*
         * The status will always be RPC_TIMEDOUT unless an error occurs
         * because `request_backlog_7()` uses asynchronous message-passing.
         */
        LOG_ADD1("request_backlog_7() failure: %s", clnt_errmsg(down7->clnt));
    }
    (void)unlockClient(down7);

    log_log(LOG_ERR); // because end of thread

    return NULL; // because thread is detached
}

/**
 * Requests from the associated upstream LDM-7, the next product in a downstream
 * LDM-7's missed-but-not-requested queue. Doesn't return until the queue has a
 * product, or the queue is shut down, or an error occurs.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @retval    0              Success.
 * @retval    LDM7_SHUTDOWN  The missed-but-not-requested queue has been shut
 *                           down.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 * @retval    LDM7_RPC       Error in RPC layer. `log_start()` called.
 */
static inline int // inlined because only called by one small function
makeRequest(
    Down7* const down7)
{
    int            status;
    VcmtpProdIndex iProd;

    /*
     * The semantics and order of the following actions are necessary to
     * preserve the meaning of the two queues and to ensure that all missed
     * data-products are received following a restart.
     */
    if (msm_peekMissedFileWait(down7->msm, &iProd)) {
        udebug("The queue of missed data-products has been shutdown");
        status = LDM7_SHUTDOWN;
    }
    else {
        if (msm_addRequestedFile(down7->msm, iProd)) {
            LOG_ADD0("Couldn't add VCMTP product-index to requested-queue");
            status = LDM7_SYSTEM;
        }
        else {
            /* The queue can't be empty */
            (void)msm_removeMissedFileNoWait(down7->msm, &iProd);

            if ((status = requestProduct(down7, iProd)) != 0)
                LOG_ADD0("Couldn't request missed data-product");
        } // product-index added to requested-but-not-received queue
    } // have a peeked-at product-index from the missed-but-not-requested queue

    return status;
}

/**
 * Requests data-products that were missed by the multicast LDM receiver.
 * Entries from the missed-but-not-requested queue are removed and converted
 * into requests for missed data-products, which are asynchronously sent to the
 * remote LDM-7. Doesn't return until the request-queue is shut down or an
 * unrecoverable error occurs. Called by `pthread_create()`.
 *
 * @param[in] arg            Pointer to the downstream LDM-7 object.
 * @retval    LDM7_RPC       Error in RPC layer. `log_start()` called.
 * @retval    LDM7_SHUTDOWN  The request-queue was shut down.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 */
static void*
startRequestTask(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    int          status;

    do {
        status = makeRequest(down7);
    } while (status == 0 && getState(down7) == DOWN7_RUNNING);

    taskExit(down7, status);

    return NULL;
}

/**
 * Cleanly stops the concurrent task of a downstream LDM-7 that's requesting
 * data-products that were missed by the multicast LDM receiver by shutting down
 * the queue of missed products and shutting down the socket to the remote LDM-7
 * for writing. Returns immediately. Idempotent.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 whose requesting task is
 *                   to be stopped.
 */
static void
stopRequestTask(
    Down7* const down7)
{
    udebug("Stopping data-product requesting task");
    lockState(down7);
    if (down7->msm)
        msm_shutDownMissedFiles(down7->msm);
    if (0 <= down7->sock)
        (void)shutdown(down7->sock, SHUT_WR);
    unlockState(down7);
}

/**
 * Creates an RPC transport for receiving unicast data-product from an upstream
 * LDM-7.
 *
 * @param[in]  sock         The TCP socket connected to the upstream LDM-7.
 * @param[out] rpcXprt      The created RPC transport. Caller should call
 *                          `svc_destroy(xprt)` when it's no longer needed.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 * @retval     LDM7_RPC     RPC error. `log_start()` called.
 */
static int
createUcastRecvXprt(
    const int       sock,
    SVCXPRT** const rpcXprt)
{
    int                status;
    struct sockaddr_in addr;
    socklen_t          addrLen = sizeof(addr);

    status = getpeername(sock, &addr, &addrLen);
    if (status) {
        LOG_SERROR0("Couldn't get Internet address of upstream LDM-7");
        status = LDM7_SYSTEM;
    }
    else {
        SVCXPRT* const     xprt = svcfd_create(sock, 0, MAX_RPC_BUF_NEEDED);

        if (xprt == NULL) {
            LOG_ADD1("Couldn't create server-side RPC transport for receiving "
                    "data-products from upstream LDM-7 at \"%s\"",
                    inet_ntoa(addr.sin_addr));
            status = LDM7_RPC;
        }
        else {
            /*
             * Set the remote address of the server-side RPC transport because
             * `svcfd_create()` doesn't.
             */
            xprt->xp_raddr = addr;
            xprt->xp_addrlen = addrLen;
            *rpcXprt = xprt;
            status = 0;
        }
    }

    return status;
}

/**
 * Receives unicast data-products from the associated upstream LDM-7 -- either
 * because they were missed by the multicast LDM receiver or because they are
 * part of the backlog. Called by `pthread_create()`.
 *
 * NB: When this function returns, the TCP socket will have been closed.
 *
 * @param[in] arg            Pointer to the downstream LDM-7.
 * @retval    0              Success.
 * @retval    LDM7_RPC       RPC error. `log_start()` called.
 * @retval    LDM7_TIMEDOUT  A timeout occurred. `log_start()` called.
 * @retval    LDM7_SYSTEM    System error. `log_log()` called.
 */
static void*
startUcastRecvTask(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    SVCXPRT*     xprt;
    int          status = createUcastRecvXprt(down7->sock, &xprt);

    if (0 == status) {
        if (!svc_register(xprt, LDMPROG, SEVEN, ldmprog_7, 0)) {
            char buf[256];

            (void)sa_snprint(down7->servAddr, buf, sizeof(buf));
            LOG_ADD1("Couldn't register RPC server for receiving "
                    "data-products from upstream LDM-7 at \"%s\"", buf);
            svc_destroy(xprt);
            status = LDM7_RPC;
        }
        else {
            /*
             * The following executes until an error occurs or termination is
             * externally requested. It destroys and unregisters the service
             * transport, which will close the downstream LDM-7's client socket.
             */
            status = run_down7_svc(down7, xprt);
        }
    } // `xprt` initialized

    taskExit(down7, status);

    return NULL;
}

/**
 * Closes the termination pipe(2) of a downstream LDM-7.
 *
 * @param[in] down7  Downstream LDM-7 whose termination pipe(2) is to be closed.
 * @retval    0      Success.
 * @retval    -1     Failure. `log_start()` called.
 */
static int
closeTermPipe(
        Down7* const down7)
{
    int status = 0;

    lockState(down7);
    for (int i = 0; i < 2; ++i) {
        if (close(down7->fds[i])) {
            LOG_SERROR2("Couldn't close %s-end of termination-pipe(2), %d",
                    i ? "write" : "read", down7->fds[i]);
            status = -1;
        }
        down7->fds[i] = -1;
    }
    unlockState(down7);

    return status;
}

/**
 * Ensures that the concurrent task of a downstream LDM-7 that's receiving
 * unicast data-products from an upstream LDM-7 is stopped by closing the
 * write-end of the termination pipe(2). Returns immediately. Idempotent.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 whose receiving task must
 *                   be stopped.
 * @retval    0      Success.
 * @retval    -1     Failure. `log_start()` called.
 */
static int
stopUcastRecvTask(
    Down7* const down7)
{
    int status;

    lockState(down7);
    int fd = down7->fds[1];
    unlockState(down7);

    udebug("Stopping unicast data-product receiving task");
    if (write(fd, &fd, sizeof(int)) == -1) {
        LOG_SERROR0("Couldn't write to termination-pipe(2)");
        status = -1;
    }
    else {
        status = 0;
    }

    return status;
}

/**
 * Receives data-products via multicast. Doesn't return until
 * `stopMcastRecvTask()` is called. Called by `pthread_create()`.
 *
 * @param[in] arg            Pointer to the downstream LDM-7.
 * @retval    LDM7_SHUTDOWN  The multicast LDM receiver was stopped.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 */
static void*
startMcastRecvTask(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;

    Mlr* const   mlr = mlr_new(pq, down7->mcastInfo, down7);
    int          status;

    if (mlr == NULL) {
        LOG_ADD0("Couldn't create a new multicast LDM receiver");
        status = LDM7_SYSTEM;
    }
    else {
        lockState(down7);
        down7->mlr = mlr;
        unlockState(down7);
        status = mlr_start(mlr);
    }

    lockState(down7);
    mlr_free(down7->mlr);
    down7->mlr = NULL;
    unlockState(down7);

    taskExit(down7, status);

    return NULL;
}

/**
 * Stops the receiver of multicast data-products of a downstream LDM-7.
 * Idempotent.
 *
 * @param[in] down7  The downstream LDM-7.
 */
static void
stopMcastRecvTask(
        Down7* const down7)
{
    udebug("Stopping multicast data-product receiving task");
    lockState(down7);
    if (down7->mlr)
        mlr_stop(down7->mlr);
    unlockState(down7);
}

/**
 * Starts the concurrent tasks of a downstream LDM-7. Returns immediately.
 *
 * @pre                     The state of the downstream LDM-7 is unlocked.
 * @param[in]  down7        Pointer to the downstream LDM-7.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  Error. `log_start()` called. No task is running.
 */
static int
startTasks(
    Down7* const             down7)
{
    int status;

    if ((status = pthread_create(&down7->ucastRecvThread, NULL,
            startUcastRecvTask, down7)) != 0) {
        LOG_ERRNUM0(status, "Couldn't start task that receives "
                "data-products that were missed by the multicast LDM "
                "receiving task");
        status = LDM7_SYSTEM;
    }
    else if ((status = pthread_create(&down7->requestThread, NULL,
            startRequestTask, down7)) != 0) {
        LOG_ERRNUM0(status, "Couldn't start task that requests "
                "data-products that were missed by the multicast LDM "
                "receiving task");
        (void)stopUcastRecvTask(down7);
        (void)pthread_join(down7->ucastRecvThread, NULL);
        status = LDM7_SYSTEM;
    }
    else if ((status = pthread_create(&down7->mcastRecvThread, NULL,
            startMcastRecvTask, down7)) != 0) {
        LOG_ERRNUM0(status, "Couldn't start multicast LDM receiving task");
        stopRequestTask(down7);
        (void)pthread_join(down7->requestThread, NULL);
        (void)stopUcastRecvTask(down7);
        (void)pthread_join(down7->ucastRecvThread, NULL);
        status = LDM7_SYSTEM;
    }

    return status;
}

/**
 * Stops all tasks of a downstream LDM-7. Undefined behavior results if
 * called from a signal handler. Returns immediately. Idempotent.
 *
 * @param[in] down7  Pointer to the downstream LDM-7.
 * @retval    0      Success.
 * @retval    -1     Failure. `log_start()` called.
 */
static int
stopTasks(
    Down7* const down7)
{
    int status;

    lockState(down7);

    stopMcastRecvTask(down7);
    stopRequestTask(down7);

    status = stopUcastRecvTask(down7);
    if (status)
        LOG_ADD0("Couldn't stop downstream LDM-7 unicast receiving task");

    unlockState(down7);

    return status;
}

/**
 * Joins the tasks of a downstream LDM-7 (i.e., waits for all its tasks to
 * complete).
 *
 * @param[in] down7  The downstream LDM-7.
 */
static void
joinTasks(
        Down7* const down7)
{
    (void)pthread_join(down7->mcastRecvThread, NULL);
    (void)pthread_join(down7->requestThread, NULL);
    (void)pthread_join(down7->ucastRecvThread, NULL);
}

/**
 * Receives data for a downstream LDM-7. Doesn't return until the downstream
 * LDM-7  terminates.
 *
 * @param[in] down7            Pointer to the downstream LDM-7.
 * @retval    LDM7_SYSTEM      System error. `log_start()` called.
 * @retval    LDM7_SHUTDOWN    The downstream LDM-7 was shut down.
 * @retval    LDM7_RPC         RPC error. `log_start()` called.
 * @retval    LDM7_TIMEDOUT    A timeout occurred. `log_start()` called.
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
        joinTasks(down7);
    }

    return status;
}

/**
 * Subscribes a downstream LDM-7 to a multicast group and receives the data.
 * Doesn't return until the LDM-7 is shut down or an error occurs.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @retval    LDM7_SHUTDOWN  LDM-7 was shut down.
 * @retval    LDM7_TIMEDOUT  Timeout occurred. `log_start()` called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). `log_start()`
 *                           called.
 * @retval    LDM7_INVAL     Invalid multicast group name.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 */
static int
subscribeAndExecute(
    Down7* const down7)
{
    int                status;
    SubscriptionReply* reply;

    (void)lockClient(down7);
    reply = subscribe_7(&down7->feedtype, down7->clnt);

    if (reply == NULL) {
        LOG_START1("subscribe_7() failure: %s", clnt_errmsg(down7->clnt));
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
            down7->mcastInfo = &reply->SubscriptionReply_u.mgi;
            status = execute(down7);
        }
        xdr_free(xdr_SubscriptionReply, (char*)reply);
    } /* `reply` allocated */

    return status;
}

/**
 * Creates the client-side handle and executes the downstream LDM-7.
 *
 * @param[in] down7          Pointer to the downstream LDM-7 to be executed.
 * @retval    LDM7_SHUTDOWN  LDM-7 was shut down.
 * @retval    LDM7_INVAL     Invalid port number or host identifier.
 *                           `log_start()` called.
 * @retval    LDM7_REFUSED   Remote host refused connection. `log_start()`
 *                           called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. `log_start()` called.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). `log_start()`
 *                           called.
 * @retval    LDM7_INVAL     Invalid multicast group name. `log_start()` called.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 *                           `log_start()` called.
 */
static int
createClientAndExecute(
    Down7* const down7)
{
    int status = newClient(&down7->clnt, down7->servAddr, &down7->sock);

    if (status == 0) {
        status = subscribeAndExecute(down7);

        clnt_destroy(down7->clnt); // won't close externally-created socket
        (void)close(down7->sock); // likely closed by server-side receiver
        down7->sock = -1;
    } // "down7->clnt" and "down7->sock" allocated

    return status;
}

/**
 * Executes a downstream LDM-7. Doesn't return until the LDM-7 is shut down or
 * an error occurs.
 *
 * @param[in] down7          Pointer to the downstream LDM-7 to be executed.
 * @retval    LDM7_SHUTDOWN  LDM-7 was shut down.
 * @retval    LDM7_INVAL     Invalid port number or host identifier.
 *                           `log_start()` called.
 * @retval    LDM7_REFUSED   Remote host refused connection. `log_start()`
 *                           called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. `log_start()` called.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). `log_start()`
 *                           called.
 * @retval    LDM7_INVAL     Invalid multicast group name. `log_start()` called.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 *                           `log_start()` called.
 */
static int
runDown7Once(
    Down7* const down7)
{
    int status;

    lockState(down7);
    down7->msm = msm_open(down7->servAddr, down7->feedtype);
    unlockState(down7);

    if (down7->msm == NULL) {
        LOG_ADD0("Couldn't open multicast session memory");
        status = LDM7_SYSTEM;
    }
    else {
        down7->prevLastMcastSet = msm_getLastMcastProd(down7->msm,
                down7->prevLastMcast);
        status = createClientAndExecute(down7);

        lockState(down7);
        if (!msm_close(down7->msm)) {
            LOG_ADD0("Couldn't close multicast session memory");
            status = LDM7_SYSTEM;
        }
        else {
            down7->msm = NULL;
        }
        unlockState(down7);
    } // `down7->msm` open

    return status;
}

/**
 * Waits a short time. Doesn't return until the time period is up or the
 * downstream LDM-7 is no longer running.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 */
static void
nap(
    Down7* const down7)
{
    struct timespec absTime;

    absTime.tv_sec = time(NULL) + 60; // a time in the future
    absTime.tv_nsec = 0;

    lockState(down7);
    while (DOWN7_RUNNING == down7->state)
        (void)pthread_cond_timedwait(&down7->termCond, &down7->stateMutex,
                &absTime);
    unlockState(down7);
}

/**
 * Inserts a data-product into the product-queue and then unlocks the
 * product-queue. Logs directly.
 *
 * @param[in] pq          Pointer to the product-queue. Must be locked.
 * @param[in] prod        Pointer to the data-product to be inserted into the product-
 *                        queue.
 * @retval    0           Success. `uinfo()` called.
 * @retval    EINVAL      Invalid argument. `uerror()` called.
 * @retval    PQUEUE_DUP  Product already exists in the queue. `uinfo()` called.
 * @retval    PQUEUE_BIG  Product is too large to insert in the queue. `uwarn()`
 *                        called.
 */
static int
insertAndUnlock(
    pqueue* const restrict  pq,
    product* const restrict prod)
{
    int status = pq_insert(pq, prod);

    (void)pq_unlock(pq);

    if (status) {
        if (status == EINVAL) {
            uerror("Invalid argument");
        }
        else {
            char buf[256];

            (void)s_prod_info(buf, sizeof(buf), &prod->info, ulogIsDebug());

            if (0 == status) {
                uinfo("%s", buf);
            }
            else if (status == PQUEUE_DUP) {
                uinfo("Duplicate data-product: %s", buf);
            }
            else {
                uwarn("Product too big for queue: %s", buf);
            }
        }
    }

    return status;
}

/**
 * Processes a data-product from a remote LDM-7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM-7.
 *
 * @param[in] pq           Pointer to the product-queue.
 * @param[in] prod         Pointer to the data-product.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
static int
deliver_product(
    pqueue* const restrict  pq,
    product* const restrict prod)
{
    int status = pq_lock(pq);

    if (status) {
        LOG_ERRNUM0(status, "Couldn't lock product-queue");
        status = LDM7_SYSTEM;
    }
    else {
        status = insertAndUnlock(pq, prod);

        if (status) {
            status = (status == EINVAL)
                    ? LDM7_SYSTEM
                    : 0; // either too big or duplicate data-product
        }
    } // product-queue is locked

    return status;
}

/**
 * Ensures that the thread-specific data-key for the downstream LDM pointer
 * exists.
 */
static void
createDown7Key(void)
{
    int status = pthread_key_create(&down7Key, NULL);

    if (status) {
        LOG_ERRNUM0(status,
                "Couldn't create thread-specific data-key for downstream LDM-7");
    }
}

/**
 * Handles failure of delivery of a data-product by logging the fact and
 * destroying the server-side RPC transport.
 *
 * @param[in] msg    The log message.
 * @param[in] info   The product metadata.
 * @param[in] rqstp  The service request.
 */
static void
deliveryFailure(
    const char* restrict            msg,
    const prod_info* const restrict info,
    struct svc_req* const restrict  rqstp)
{
    char buf[256];

    LOG_ADD2("%s: %s", msg, s_prod_info(buf, sizeof(buf), info, ulogIsDebug()));
    log_log(LOG_ERR);
    (void)svcerr_systemerr(rqstp->rq_xprt);
    svc_destroy(rqstp->rq_xprt);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new downstream LDM-7.
 *
 * @param[in] servAddr    Pointer to the address of the server from which to
 *                        obtain multicast information, backlog products, and
 *                        products missed by the VCMTP layer. Caller may free
 *                        upon return.
 * @param[in] feedtype    Feedtype of multicast group to receive.
 * @param[in] pqPathname  Pathname of the product-queue.
 * @retval    NULL        Failure. `log_start()` called.
 * @return                Pointer to the new downstream LDM-7.
 */
Down7*
down7_new(
    const ServiceAddr* const restrict servAddr,
    const feedtypet                   feedtype,
    const char* const restrict        pqPathname)
{
    Down7* const down7 = LOG_MALLOC(sizeof(Down7), "downstream LDM-7");
    int          status;

    if (down7 == NULL)
        goto return_NULL;

    if ((down7->servAddr = sa_clone(servAddr)) == NULL) {
        char buf[256];

        (void)sa_snprint(servAddr, buf, sizeof(buf));
        LOG_ADD1("Couldn't clone server address \"%s\"", buf);
        goto free_down7;
    }

    if ((status = pthread_cond_init(&down7->termCond, NULL)) != 0) {
        LOG_ERRNUM0(status,
                "Couldn't initialize condition-variable for terminating");
        goto free_servAddr;
    }

    {
        pthread_mutexattr_t mutexAttr;

        status = pthread_mutexattr_init(&mutexAttr);
        if (status) {
            LOG_ERRNUM0(status,
                    "Couldn't initialize attributes of state-mutex");
        }
        else {
            (void)pthread_mutexattr_setprotocol(&mutexAttr,
                    PTHREAD_PRIO_INHERIT);
            (void)pthread_mutexattr_settype(&mutexAttr,
                    PTHREAD_MUTEX_RECURSIVE);

            if ((status = pthread_mutex_init(&down7->stateMutex, &mutexAttr))) {
                LOG_ERRNUM0(status, "Couldn't initialize state-mutex");
                (void)pthread_mutexattr_destroy(&mutexAttr);
                goto free_termCond;
            }

            (void)pthread_mutexattr_destroy(&mutexAttr);
        } // `mutexAttr` initialized
    }

    if ((status = pthread_mutex_init(&down7->clntMutex, NULL)) != 0) {
        LOG_ERRNUM0(status, "Couldn't initialize mutex for client-side handle");
        goto free_stateMutex;
    }

    if ((status = pq_open(pqPathname, 0, &down7->pq))) {
        LOG_ADD1("Couldn't open product-queue \"%s\"", pqPathname);
        goto free_clntMutex;
    }

    if ((status = pipe(down7->fds))) {
        LOG_SERROR0("Couldn't create termination pipe(2)");
        goto close_pq;
    }

    if ((status = pthread_once(&down7KeyControl, createDown7Key)) != 0) {
        goto close_pipe;
    }

    (void)memset(down7->firstMcast, 0, sizeof(signaturet));
    (void)memset(down7->prevLastMcast, 0, sizeof(signaturet));
    down7->feedtype = feedtype;
    down7->clnt = NULL;
    down7->sock = -1;
    down7->mcastInfo = NULL;
    down7->exitStatus = 0;
    down7->mlr = NULL;
    down7->mcastWorking = false;
    down7->msm = NULL;
    down7->state = DOWN7_INITIALIZED;

    return down7;

close_pipe:
    (void)closeTermPipe(down7);
close_pq:
    (void)pq_close(down7->pq);
free_clntMutex:
    pthread_mutex_destroy(&down7->clntMutex);
free_stateMutex:
    pthread_mutex_destroy(&down7->stateMutex);
free_termCond:
    pthread_cond_destroy(&down7->termCond);
free_servAddr:
    sa_free(down7->servAddr);
free_down7:
    free(down7);
return_NULL:
    return NULL;
}

/**
 * Executes a downstream LDM-7. Doesn't return until `down7_stop()` is called
 * or an error occurs.
 *
 * @param[in] arg            Pointer to downstream LDM-7 object.
 * @retval    0              Success. `down7_stop()` was called.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 * @retval    LDM7_INVAL     The downstream LDM-7 object is in the wrong state:
 *                           it has been passed to `down7_stop()`,
 *                           `down7_free()`, `down7_spawn()`, or
 *                           `down7_terminate()`. `log_start() called.
 */
Ldm7Status
down7_run(
        Down7* const down7)
{
    int status;

    lockState(down7);

    if (DOWN7_INITIALIZED != down7->state) {
        unlockState(down7);
        LOG_START0("Downstream LDM-7 is in the wrong state");
        status = LDM7_INVAL;
    }
    else {
        down7->state = DOWN7_RUNNING;
        unlockState(down7);

        char* const addrStr = sa_format(down7->servAddr);
        unotice("Downstream LDM-7 starting up: remoteAddr=%s, feedtype=%s,"
                "pq=%s", addrStr, s_feedtypet(down7->feedtype), getQueuePath());
        free(addrStr);

        for (;;) {
            status = runDown7Once(down7);

            if (LDM7_SYSTEM == status)
                break;

            log_log(LOG_NOTICE); // might log nothing

            if (LDM7_TIMEDOUT != status)
                nap(down7); // returns immediately if stop requested

            if (getState(down7) == DOWN7_STOP_REQUESTED) {
                status = 0;
                break;
            }
        }

        (void)setState(down7, DOWN7_STOPPED);
    }

    return status;
}

/**
 * Stops a running downstream LDM-7 that has been started via `down7_run()`.
 * Causes `down7_run()` to return if it hasn't already. Returns immediately.
 *
 * @pre                      `down7_run()` has been called on the downstream
 *                           LDM-7.
 * @param[in] down7          The running downstream LDM-7 to be stopped.
 * @retval    0              Success. `down7_run()` should return.
 * @retval    LDM7_SYSTEM    The downstream LDM-7 couldn't be stopped due to a
 *                           system error. `log_log()` called.
 * @retval    LDM7_INVAL     The downstream LDM-7 is in the wrong state.
 *                           `log_start()` called.
 */
Ldm7Status
down7_stop(
        Down7* const down7)
{
    int status;

    lockState(down7);

    if (DOWN7_RUNNING != down7->state) {
        LOG_START0("Downstream LDM-7 isn't running");
        status = LDM7_INVAL;
    }
    else {
        down7->state = DOWN7_STOP_REQUESTED;

        udebug("Terminating downstream LDM-7 tasks");
        if (stopTasks(down7)) {
            LOG_ADD0("Couldn't stop concurrent tasks of downstream LDM-7");
            status = LDM7_SYSTEM;
        }
        else {
            /* Causes `nap()` to return */
            (void)pthread_cond_broadcast(&down7->termCond);
            status = 0;
        }
    }

    unlockState(down7);

    return status;
}

/**
 * Frees the resources of a downstream LDM-7 returned by `down7_new()` that
 * either wasn't started or has been stopped.
 *
 * @pre                   The downstream LDM-7 was returned by `down7_new()` and
 *                        either `down7_run()` has not been called on it or
 *                        `down7_stop()` has been called on it.
 * @param[in] down7       Pointer to the downstream LDM-7 to be freed or NULL.
 * @retval    0           Success.
 * @retval    LDM7_INVAL  The downstream LDM-7 was not returned by `down7_new()`
 *                        or `down7_run()` has been called on it but not
 *                        `down7_stop()`. `log_start()` called.
 */
int
down7_free(
    Down7* const down7)
{
    int status = 0;

    if (down7) {
        Down7State state = getState(down7);

        if (DOWN7_INITIALIZED != state && DOWN7_STOPPED != state) {
            LOG_START0("Downstream LDM-7 not allocated or not stopped");
            status = LDM7_INVAL;
        }
        else {
            if (closeTermPipe(down7)) {
                LOG_ADD0("Couldn't close termination-pipe(2)");
                status = -1;
            }
            if (pthread_mutex_destroy(&down7->clntMutex)) {
                LOG_ADD0("Couldn't destroy client-mutex");
                status = -1;
            }
            if (pthread_mutex_destroy(&down7->stateMutex)) {
                LOG_ADD0("Couldn't destroy state-mutex");
                status = -1;
            }
            if (pthread_cond_destroy(&down7->termCond)) {
                LOG_ADD0("Couldn't destroy termination condition-variable");
                status = -1;
            }
            if (pq_close(down7->pq)) {
                LOG_ADD0("Couldn't close product-queue");
                status = -1;
            }
            sa_free(down7->servAddr);
            free(down7);
        }
    }

    return status;
}

/**
 * Returns a new downstream LDM-7 that has been started on a new thread.
 *
 * @param[in] servAddr       Pointer to the address of the server from which to
 *                           obtain multicast information, backlog products, and
 *                           products missed by the VCMTP layer. Caller may free
 *                           upon return.
 * @param[in] feedtype       Feedtype of multicast group to receive.
 * @param[in] pqPathname     Pathname of the product-queue.
 * @retval    LDM7_SHUTDOWN  Process termination requested.
 * @retval    LDM7_SYSTEM    System error occurred. `log_start()` called.
 */
Ldm7Status
down7_spawn(
    const ServiceAddr* const restrict servAddr,
    const feedtypet                   feedtype,
    const char* const restrict        pqPathname)
{
    int    status;
    Down7* down7 = down7_new(servAddr, feedtype, pqPathname);

    if (NULL == down7) {
        status = LDM7_SYSTEM;
    }
    else {
        status = down7_run(down7);
        down7_free(down7);
    } // `down7` allocated

    return status;
}

/**
 * Stops a downstream LDM-7 and destroys and frees its resources.
 *
 * @param[in] down7  The downstream LDM-7.
 * @retval    0      Success.
 */
Ldm7Status
down7_terminate(
        Down7* const down7)
{
    // TODO
    return 0;
}

/**
 * Queues a data-product that that was missed by the multicast LDM receiver.
 * This function is called by the multicast LDM receiver; therefore, it must
 * return immediately so that the multicast LDM receiver can continue.
 *
 * @param[in] down7   Pointer to the downstream LDM-7.
 * @param[in] iProd   Index of the missed VCMTP product.
 */
void
down7_missedProduct(
    Down7* const         down7,
    const VcmtpProdIndex iProd)
{
    /*
     * Cancellation of the operation of the missed-but-not-requested queue is
     * ignored because nothing can be done about it at this point and no harm
     * should result.
     */
    udebug("Down-7 missed product: %lu", (unsigned long)iProd);
    (void)msm_addMissedFile(down7->msm, iProd);
}

/**
 * Tracks the last data-product to be successfully received by the multicast
 * LDM receiver associated with a downstream LDM-7. This function is called by
 * the multicast LDM receiver; therefore, it must return immediately so that the
 * multicast LDM receiver can continue.
 *
 * The first time this function is called for a given downstream LDM-7, it
 * starts a detached thread that requests the backlog of data-products that
 * were missed due to the passage of time from the end of the previous session
 * to the reception of the first multicast data-product.
 *
 * @param[in] down7  Pointer to the downstream LDM-7.
 * @param[in] last   Pointer to the metadata of the last data-product to be
 *                   successfully received by the associated multicast
 *                   LDM receiver. Caller may free when it's no longer needed.
 */
void
down7_lastReceived(
    Down7* const restrict           down7,
    const prod_info* const restrict last)
{
    msm_setLastMcastProd(down7->msm, last->signature);

    if (!down7->mcastWorking) {
        down7->mcastWorking = true;
        (void)memcpy(down7->firstMcast, last->signature, sizeof(signaturet));

        pthread_t backlogThread;
        int status = pthread_create(&backlogThread, NULL, requestSessionBacklog,
                down7);

        if (status) {
            LOG_ERRNUM0(status, "Couldn't create backlog-requesting thread");
            log_log(LOG_ERR);
        }
        else {
            (void)pthread_detach(backlogThread);
        }
    }
}

/**
 * Processes a missed data-product from a remote LDM-7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM-7 because it was missed by the
 * multicast LDM receiver. Destroys the server-side RPC transport if the
 * data-product isn't expected or can't be inserted into the product-queue. Does
 * not reply. Called by the RPC dispatcher `ldmprog_7()`.
 *
 * @param[in] missedProd  Pointer to the missed data-product.
 * @param[in] rqstp       Pointer to the RPC service-request.
 * @retval    NULL        Always.
 */
void*
deliver_missed_product_7_svc(
    MissedProduct* const restrict  missedProd,
    struct svc_req* const restrict rqstp)
{
    prod_info* const info = &missedProd->prod.info;
    Down7*           down7 = pthread_getspecific(down7Key);
    VcmtpProdIndex   iProd;

    if (!msm_peekRequestedFileNoWait(down7->msm, &iProd) ||
            iProd != missedProd->iProd) {
        deliveryFailure("Unexpected product received", info, rqstp);
    }
    else {
        // The queue can't be empty
        (void)msm_removeRequestedFileNoWait(down7->msm, &iProd);

        if (deliver_product(down7->pq, &missedProd->prod) != 0)
            deliveryFailure("Couldn't insert missed product", info, rqstp);
    }

    return NULL; // causes RPC dispatcher to not reply
}

/**
 * Accepts notification from the upstream LDM-7 that a requested data-product
 * doesn't exist. Called by the RPC dispatch routine `ldmprog_7()`.
 *
 * @param[in] iProd   Index of the data-product.
 * @param[in] rqstp   Pointer to the RPC service-request.
 */
void*
no_such_product_7_svc(
    VcmtpProdIndex* const iProd,
    struct svc_req* const rqstp)
{
    uwarn("Upstream LDM-7 says requested product doesn't exist: %lu",
            (unsigned long)iProd);

    return NULL ; /* don't reply */
}

/**
 * Processes a backlog data-product from a remote LDM-7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM-7 because it was missed during the
 * previous session. Destroys the server-side RPC transport if the data-product
 * can't be inserted into the product-queue. Does not reply. Called by the RPC
 * dispatcher `ldmprog_7()`.
 *
 * @param[in] prod        Pointer to the backlog data-product.
 * @param[in] rqstp       Pointer to the RPC service-request.
 * @retval    NULL        Always.
 */
void*
deliver_backlog_product_7_svc(
    product* const restrict        prod,
    struct svc_req* const restrict rqstp)
{
    Down7* down7 = pthread_getspecific(down7Key);

    if (deliver_product(down7->pq, prod))
        deliveryFailure("Couldn't insert backlog product", &prod->info, rqstp);

    return NULL; // causes RPC dispatcher to not reply
}

/**
 * Accepts notification that the downstream LDM-7 associated with the current
 * thread has received all backlog data-products from its upstream LDM-7. From
 * now on, the current process may be terminated for a time period that is less
 * than the minimum residence time of the upstream LDM-7's product-queue without
 * loss of data. Called by the RPC dispatcher `ldmprog_7()`.
 *
 * @param[in] rqstp  Pointer to the RPC server-request.
 */
void*
end_backlog_7_svc(
    void* restrict                 noArg,
    struct svc_req* const restrict rqstp)
{
    char   saStr[512];
    Down7* down7 = pthread_getspecific(down7Key);

    unotice("All backlog data-products received: feedtype=%s, server=%s",
            s_feedtypet(down7->feedtype),
            sa_snprint(down7->servAddr, saStr, sizeof(saStr)));

    return NULL; // causes RPC dispatcher to not reply
}
