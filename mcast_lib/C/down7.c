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
#include "mcast_info.h"
#include "mldm_receiver.h"
#include "mldm_receiver_memory.h"
#include "pq.h"
#include "rpc/rpc.h"
#include "rpcutil.h"
#include "timestamp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
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
    DOWN7_EXECUTING,
    DOWN7_STOPPING,
    DOWN7_STOPPED
} Down7State;

/**
 * Thread-safe proxy for an upstream LDM-7 associated with a downstream LDM-7.
 */
typedef struct {
    CLIENT*               clnt;   ///< client-side RPC handle
    pthread_mutex_t       mutex;  ///< because accessed by multiple threads
} Up7Proxy;

/**
 * The data structure of a downstream LDM-7:
 */
struct Down7 {
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
    pqueue*               pq;            ///< pointer to the product-queue
    ServiceAddr*          servAddr;      ///< socket address of remote LDM-7
    McastInfo*            mcastInfo;     ///< information on multicast group
    /** IP address of interface to use for incoming multicast packets */
    char*                 mcastIface;
    Mlr*                  mlr;           ///< multicast LDM receiver
    /** Persistent multicast receiver memory */
    McastReceiverMemory*   mrm;
    Up7Proxy*             up7proxy;      ///< proxy for upstream LDM-7
    Executor*             executor;      ///< asynchronous executor of jobs
    pthread_mutex_t       stateMutex;    ///< recursive mutex for changing state
    pthread_cond_t        napCond;       ///< condition-variable for napping
    /** Synchronizes multiple-thread access to client-side RPC handle */
    feedtypet             feedtype;      ///< feed-expression of multicast group
    Down7State            state;         ///< Downstream LDM-7 state
    int                   sock;          ///< socket with remote LDM-7
    bool                  mcastWorking;  ///< product received via multicast?
    /** Whether or not `prevLastMcast` is set: */
    bool                  prevLastMcastSet;
};

// Type for obtaining integer status from `pthread_create()` start-function.
typedef union {
    void* ptr;
    int   val;
} PtrInt;

static int up7proxy_init(
        Up7Proxy* const restrict  proxy,
        const int                 socket,
        struct sockaddr_in* const sockAddr)
{
    int       status;

    if (proxy == NULL || socket <= 0 || sockAddr == NULL) {
        status = LDM7_INVAL;
    }
    else {
        status = pthread_mutex_init(&proxy->mutex, NULL);

        if (status) {
            LOG_ERRNUM0(status,
                    "Couldn't initialize mutex for upstream LDM-7 proxy");
            status = LDM7_SYSTEM;
        }
        else {
            int sock = socket;
            CLIENT* const clnt = clnttcp_create(sockAddr, LDMPROG, SEVEN, &sock,
                    0, 0);

            if (clnt == NULL) {
                LOG_SERROR3("Couldn't create RPC client for host %s, port %hu: "
                        "%s", inet_ntoa(sockAddr->sin_addr),
                        ntohs(sockAddr->sin_port), clnt_spcreateerror(""));
                (void)pthread_mutex_destroy(&proxy->mutex);
                status = clntStatusToLdm7Status(rpc_createerr.cf_stat);
            }
            else {
                proxy->clnt = clnt;
                status = 0;
            }
        }
    }

    return status;
}

/**
 * Returns a new proxy for an upstream LDM-7.
 *
 * @param[out] up7proxy     Pointer to the new proxy.
 * @param[in]  socket       The socket to use.
 * @param[in]  sockAddr     The address of the upstream LDM-7 server.
 * @retval     0            Success.
 * @retval     LDM7_INVAL   Invalid argument.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static int up7proxy_new(
        Up7Proxy** const restrict up7proxy,
        const int                 socket,
        struct sockaddr_in* const sockAddr)
{
    int       status;

    if (up7proxy == NULL) {
        status = LDM7_INVAL;
    }
    else {
        Up7Proxy* const proxy = LOG_MALLOC(sizeof(Up7Proxy),
                "upstream LDM-7 proxy");

        if (proxy == NULL) {
            status = LDM7_SYSTEM;
        }
        else {
            status = up7proxy_init(proxy, socket, sockAddr);

            if (status) {
                free(proxy);
            }
            else {
                *up7proxy = proxy;
            }
        }
    }

    return status;
}

/**
 * Idempotent.
 *
 * @param[in] proxy  Upstream LDM-7 proxy.
 * @post             `proxy->clnt == NULL`
 */
static void up7proxy_destroyClient(
        Up7Proxy* const proxy)
{
    if (proxy->clnt) {
        clnt_destroy(proxy->clnt); // won't close externally-created socket
        proxy->clnt = NULL;
    }
}

/**
 * @param[in] proxy
 */
static void up7proxy_free(
        Up7Proxy* const proxy)
{
    if (proxy) {
        up7proxy_destroyClient(proxy);
        int status = pthread_mutex_destroy(&proxy->mutex);
        if (status) {
            LOG_ERRNUM0(status, "Couldn't destroy mutex");
            log_log(LOG_ERR);
        }
        free(proxy);
    }
}

/**
 * Locks an upstream LDM-7 proxy for exclusive access.
 *
 * @pre                   `proxy->clnt != NULL`
 * @param[in] proxy       Pointer to the upstream LDM-7 proxy to be locked.
 */
static void
up7proxy_lock(
    Up7Proxy* const proxy)
{
    int status = pthread_mutex_lock(&proxy->mutex);
    UASSERT(status == 0);
    UASSERT(proxy->clnt != NULL);
}

/**
 * Unlocks an upstream LDM-7 proxy.
 *
 * @param[in] proxy       Pointer to the upstream LDM-7 proxy to be unlocked.
 */
static void
up7proxy_unlock(
    Up7Proxy* const proxy)
{
    int status = pthread_mutex_unlock(&proxy->mutex);
    UASSERT(status == 0);
}

/**
 * Subscribes to an upstream LDM-7 server.
 *
 * @param[in]  proxy      Proxy for the upstream LDM-7.
 * @param[in]  feedtype   Feedtype specification.
 * @param[out] mcastInfo  Information on the multicast group corresponding to
 *                        `feedtype`.
 * @retval     0          If and only if success. `*mcastInfo` is set. The
 *                        caller should call `mi_free(*mcastInfo)` when it's no
 *                        longer needed.
 */
static int
up7proxy_subscribe(
        Up7Proxy* const restrict   proxy,
        const feedtypet            feedtype,
        McastInfo** const restrict mcastInfo)
{
    int status;

    up7proxy_lock(proxy);

    CLIENT* const      clnt = proxy->clnt;
    feedtypet          feed = feedtype;
    SubscriptionReply* reply = subscribe_7(&feed, clnt);

    if (reply == NULL) {
        char buf[256];

        (void)sprint_feedtypet(buf, sizeof(buf), feedtype);
        LOG_START2("Couldn't subscribe to feedtype %s: %s",  buf,
                clnt_errmsg(clnt));
        status = clntStatusToLdm7Status(clnt_stat(clnt));
        up7proxy_destroyClient(proxy);
    }
    else {
        status = reply->status;
        if (status == 0) {
            McastInfo* const mi = &reply->SubscriptionReply_u.mgi;
            char* miStr = mi_format(mi);
            udebug("%s:up7proxy_subscribe(): Subscription reply is %s",
                    __FILE__, miStr);
            free(miStr);
            *mcastInfo = mi_clone(mi);
            xdr_free(xdr_SubscriptionReply, (char*)reply);
        }
    }

    up7proxy_unlock(proxy);

    udebug("up7proxy_subscribe(): Returning %d", status);
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
 * @param[in] arg       Pointer to upstream LDM-7 proxy.
 * @retval    0         Success.
 * @retval    LDM7_RPC  Error in RPC layer. `log_start()` called.
 */
static int
up7proxy_requestSessionBacklog(
    Up7Proxy* const restrict    proxy,
    BacklogSpec* const restrict spec)
{
    up7proxy_lock(proxy);

    CLIENT* const clnt = proxy->clnt;
    int           status;

    (void)request_backlog_7(spec, clnt); // asynchronous => no reply
    if (clnt_stat(clnt) == RPC_TIMEDOUT) {
        /*
         * The status will always be RPC_TIMEDOUT unless an error occurs
         * because `request_backlog_7()` uses asynchronous message-passing.
         */
        status = 0;
    }
    else {
        LOG_ADD1("Couldn't request session backlog: %s", clnt_errmsg(clnt));
        up7proxy_destroyClient(proxy);
        status = LDM7_RPC;
    }

    up7proxy_unlock(proxy);

    return status;
}

/**
 * Requests a data-product that was missed by the multicast LDM receiver.
 *
 * @param[in] proxy       Pointer to the upstream LDM-7 proxy.
 * @param[in] prodId      LDM product-ID of missed data-product.
 * @retval    0           Success. A data-product was requested.
 * @retval    LDM7_RPC    RPC error. `log_start()` called.
 */
static int
up7proxy_requestProduct(
    Up7Proxy* const      proxy,
    VcmtpProdIndex const iProd)
{
    up7proxy_lock(proxy);

    CLIENT* clnt = proxy->clnt;
    int     status;

    udebug("%s:up7proxy_requestProduct(): iProd=%lu", __FILE__,
            (unsigned long)iProd);
    // Asynchronous send => no reply
    (void)request_product_7((VcmtpProdIndex*)&iProd, clnt); // safe cast

    if (clnt_stat(clnt) == RPC_TIMEDOUT) {
        /*
         * The status will always be RPC_TIMEDOUT unless an error occurs
         * because `request_product_7()` uses asynchronous message-passing.
         */
        status = 0;
    }
    else {
        LOG_START2("Couldn't request missed data-product: iProd=%lu: %s",
                (unsigned long)iProd, clnt_errmsg(clnt));
        up7proxy_destroyClient(proxy);
        status = LDM7_RPC;
    }

    up7proxy_unlock(proxy);

    return status;
}

/**
 * Tests the connection to an upstream LDM-7 by sending a no-op/no-reply message
 * to it.
 *
 * @param[in] proxy     Pointer to the proxy for the upstream LDM-7.
 * @retval    0         Success. The connection is still good.
 * @retval    LDM7_RPC  RPC error. `log_start()` called.
 */
static int
up7proxy_testConnection(
    Up7Proxy* const proxy)
{
    int status;

    up7proxy_lock(proxy);
    test_connection_7(NULL, proxy->clnt);
    if (clnt_stat(proxy->clnt) == RPC_TIMEDOUT) {
        /*
         * "test_connection_7()" uses asynchronous message-passing, so the
         * status will always be RPC_TIMEDOUT unless an error occurs.
         */
        status = 0;
    }
    else {
	LOG_START1("test_connection_7() failure: %s", clnt_errmsg(proxy->clnt));
        status = LDM7_RPC;
    }
    up7proxy_unlock(proxy);

    return status;
}

/**
 * Locks the state of a downstream LDM-7.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its state locked.
 */
static void
lockState(
    Down7* const down7)
{
    udebug("lockState(): Locking state");
    int status = pthread_mutex_lock(&down7->stateMutex);
    UASSERT(status == 0);
}

/**
 * Unlocks the state of a downstream LDM-7.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its state unlocked.
 */
static void
unlockState(
    Down7* const down7)
{
    udebug("unlockState(): Unlocking state");
    int status = pthread_mutex_unlock(&down7->stateMutex);
    UASSERT(status == 0);
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

static bool
setCancelState(
        const bool enable)
{
    int oldState;
    (void)pthread_setcancelstate(enable ? PTHREAD_CANCEL_ENABLE :
            PTHREAD_CANCEL_DISABLE, &oldState);
    return oldState == PTHREAD_CANCEL_ENABLE;
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
 * @retval     LDM7_REFUSED   Remote host refused connection (server likely
 *                            isn't running). `log_start()` called.
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

    udebug("getSocket(): Returning %d", status);
    return status;
}

/**
 * Creates a new client-side handle in a downstream LDM-7 for its remote LDM-7.
 *
 * @param[in]  down7          Pointer to the downstream LDM-7.
 * @retval     0              Success. `down7->up7proxy` and `down7->sock` are
 *                            set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier.
 *                            `log_start()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection. `log_start()`
 *                            called.
 * @retval     LDM7_RPC       RPC error. `log_start()` called.
 * @retval     LDM7_SYSTEM    System error. `log_start()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection (server likely
 *                            isn't running). `log_start()` called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. `log_start()`
 *                            called.
 * @retval     LDM7_UNAUTH    Not authorized. `log_start()` called.
 */
static int
newClient(
    Down7* const    down7)
{
    int                     sock;
    struct sockaddr_storage sockAddr;
    int                     status = getSocket(down7->servAddr, &sock,
            &sockAddr);

    if (status == 0) {
        status = up7proxy_new(&down7->up7proxy, sock,
                (struct sockaddr_in*)&sockAddr);
        if (status) {
            (void)close(sock);
        }
        else {
            down7->sock = sock;
        }
    } // `sock` is open

    udebug("newClient(): Returning %d", status);
    return status;
}

/**
 * Tests the connection to the upstream LDM-7 of a downstream LDM-7 by sending
 * a no-op/no-reply message to it.
 *
 * @param[in] down7     Pointer to the downstream LDM-7.
 * @retval    0         Success. The connection is still good.
 * @retval    LDM7_RPC  RPC error. `log_start()` called.
 */
static inline int // inline because small and only called in one spot
testConnection(
    Down7* const down7)
{
    return up7proxy_testConnection(down7->up7proxy);
}

static void
destroyTransport(
        void* const arg)
{
    svc_destroy((SVCXPRT*)arg);
}

/**
 * Runs the RPC-based server of a downstream LDM-7. Destroys and unregisters the
 * service transport. Doesn't return until an error occurs or termination is
 * externally requested.
 *
 * @pre                          Current thread's cancelability is disabled.
 * @param[in]     down7          Pointer to the downstream LDM-7.
 * @param[in]     xprt           Pointer to the RPC service transport. Will be
 *                               destroyed upon return.
 * @retval        0              Success. The RPC transport was closed.
 * @retval        LDM7_RPC       Error in RPC layer. `log_start()` called.
 * @retval        LDM7_SYSTEM    System error. `log_start()` called.
 */
static int
run_svc(
    Down7* const restrict   down7,
    SVCXPRT* restrict       xprt)
{
    const int     sock = xprt->xp_sock;
    int           status;
    struct pollfd pfd;
    int           timeout = interval * 1000; // probably 30 seconds

    pfd.fd = sock;
    pfd.events = POLLIN;

    pthread_cleanup_push(destroyTransport, xprt);

    for (;;) {
        udebug("down7.c:run_svc(): Calling poll(): socket=%d", sock);
        setCancelState(true); // enable cancellation of `poll()`
        status = poll(&pfd, 1, timeout);
        setCancelState(false); // subsequent code should be quick

        if (0 == status) {
            // Timeout
            status = testConnection(down7);
            if (status)
                break;
            continue;
        }
        if (0 > status) {
            LOG_SERROR1("down7.c:run_svc(): poll() error on socket %d", sock);
            status = LDM7_SYSTEM;
            break;
        }
        if ((pfd.revents & POLLHUP) || (pfd.revents & POLLERR)) {
            // RPC transport socket closed or in error
            status = 0;
            break;
        }
        if (pfd.revents & POLLIN)
            svc_getreqsock(sock); // Process RPC message. Calls ldmprog_7()
        if (!FD_ISSET(sock, &svc_fdset)) {
           // The RPC layer destroyed the service transport
            xprt = NULL;
            status = 0;
            break;
        }
    }

    pthread_cleanup_pop(xprt != NULL); // destroys service transport

    return status; // Eclipse IDE wants to see a return
}

/**
 * Runs the RPC-based data-product receiving service of a downstream LDM-7.
 * Destroys and unregisters the service transport. Doesn't return until an
 * error occurs or the server transport is closed.
 *
 * @pre                      Current thread's cancelability is disabled.
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @param[in] xprt           Pointer to the server-side transport object. Will
 *                           be destroyed upon return.
 * @retval    0              Success. The server transport is closed.
 * @retval    LDM7_RPC       An RPC error occurred. `log_start()` called.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 */
static int
run_down7_svc(
    Down7* const restrict   down7,
    SVCXPRT* const restrict xprt)
{
    /*
     * The downstream LDM-7 RPC functions don't know their associated downstream
     * LDM-7; therefore, a thread-specific pointer to the downstream LDM-7 is
     * set to provide context to those that need it.
     */
    int status = pthread_setspecific(down7Key, down7);

    if (status) {
        LOG_ERRNUM0(status,
                "Couldn't set thread-specific pointer to downstream LDM-7");
        svc_destroy(xprt);
        status = LDM7_SYSTEM;
    }
    else {
        /*
         * The following executes until an error occurs or termination is
         * externally requested. It destroys and unregisters the service
         * transport, which will close the downstream LDM-7's client socket.
         */
        status = run_svc(down7, xprt);
        unotice("Downstream LDM-7 server terminated");
    } // thread-specific pointer to downstream LDM-7 is set

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

    if (down7->prevLastMcastSet) {
        (void)memcpy(spec.after, down7->prevLastMcast, sizeof(signaturet));
    }
    else {
        (void)memset(spec.after, 0, sizeof(signaturet));
    }
    spec.afterIsSet = down7->prevLastMcastSet;
    (void)memcpy(spec.before, down7->firstMcast, sizeof(signaturet));
    spec.timeOffset = getTimeOffset();

    PtrInt ptrInt;
    ptrInt.val = up7proxy_requestSessionBacklog(down7->up7proxy, &spec);
    return ptrInt.ptr;
}

/**
 * Requests the next product in a downstream LDM-7's missed-but-not-requested
 * queue from the associated upstream LDM-7. Doesn't return until the queue has
 * a product, or the queue is shut down, or an error occurs.
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
    if (!mrm_peekMissedFileWait(down7->mrm, &iProd)) {
        udebug("makeRequest(): The queue of missed data-products has been shutdown");
        status = LDM7_SHUTDOWN;
    }
    else {
        if (!mrm_addRequestedFile(down7->mrm, iProd)) {
            LOG_ADD0("Couldn't add VCMTP product-index to requested-queue");
            status = LDM7_SYSTEM;
        }
        else {
            /* The queue can't be empty */
            (void)mrm_removeMissedFileNoWait(down7->mrm, &iProd);

            status = up7proxy_requestProduct(down7->up7proxy, iProd);
        } // product-index added to requested-but-not-received queue
    } // have a peeked-at product-index from the missed-but-not-requested queue

    return status;
}

/**
 * Requests data-products that were missed by the multicast LDM receiver.
 * Entries from the missed-but-not-requested queue are removed and converted
 * into requests for missed data-products, which are asynchronously sent to the
 * remote LDM-7. Doesn't return until `stopRequestTask()` is called or an
 * unrecoverable error occurs. Called by `pthread_create()`.
 *
 * @param[in] arg            Pointer to the downstream LDM-7 object.
 * @retval    LDM7_SHUTDOWN  `stopRequestTask()` was called.
 * @retval    LDM7_RPC       Error in RPC layer. `log_start()` called.
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
    } while (status == 0 && getState(down7) == DOWN7_EXECUTING);

    log_log(status ? LOG_ERR : LOG_INFO);

    PtrInt ptrInt;
    ptrInt.val = status;
    return ptrInt.ptr;
}

/**
 * Cleanly stops the concurrent task of a downstream LDM-7 that's requesting
 * data-products that were missed by the multicast LDM receiver by shutting down
 * the queue of missed products and shutting down the socket to the remote LDM-7
 * for writing. Returns immediately. Idempotent.
 *
 * @pre              The downstream LDM-7 state is locked.
 * @param[in] arg    Pointer to the downstream LDM-7 whose requesting task is
 *                   to be stopped.
 * @post             The downstream LDM-7 state is locked.
 */
static void
stopRequestTask(
        void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    if (down7->mrm) {
        udebug("stopRequestTask(): Stopping data-product requesting task");
        mrm_shutDownMissedFiles(down7->mrm);
    }
    if (0 <= down7->sock)
        (void)shutdown(down7->sock, SHUT_WR);
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
 * part of the backlog. Called by `pthread_create()`. Doesn't complete until an
 * error occurs, the server transport is closed, or the thread on which this
 * function is executing is cancelled.
 *
 * NB: When this task completes, the TCP socket will have been closed.
 *
 * @param[in] arg            Pointer to the downstream LDM-7.
 * @retval    0              Success. The server transport was closed.
 * @retval    LDM7_RPC       RPC error. `log_start()` called.
 * @retval    LDM7_SYSTEM    System error. `log_log()` called.
 */
static void*
startUcastRecvTask(
    void* const arg)
{
    Down7* const down7 = (Down7*)arg;

    (void)setCancelState(false);

    SVCXPRT*     xprt;
    int          status = createUcastRecvXprt(down7->sock, &xprt);

    if (0 == status) {
        // Last argument == 0 => don't register with portmapper
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

    log_log(status ? LOG_ERR : LOG_INFO);

    PtrInt ptrInt;
    ptrInt.val = status;
    return ptrInt.ptr;
}

/**
 * Receives data-products via multicast. Doesn't return until
 * `stopMcastRecvTask()` is called or an error occurs. Called by
 * `pthread_create()`.
 *
 * @param[in] arg            Pointer to the downstream LDM-7.
 * @retval    LDM7_SHUTDOWN  `stopMcastRecvTask()` was called.
 * @retval    LDM7_MCAST     Multicast reception error. `log_add()` called.
 */
static void*
startMcastRecvTask2(
    void* const arg)
{
    udebug("%s:startMcastRecvTask2(): Entered", __FILE__);

    Down7* const down7 = (Down7*)arg;
    int          status = mlr_start(down7->mlr); // doesn't return immediately

    mlr_free(down7->mlr); // because done with `down7->mlr`
    down7->mlr = NULL; // to reveal logic error

    // Because end of task
    if (status) {
        log_log((status == LDM7_SHUTDOWN) ? LOG_INFO : LOG_ERR);
    }
    else {
        log_clear();
    }

    udebug("startMcastRecvTask2(): Returning &%d", status);
    PtrInt ptrInt;
    ptrInt.val = status;
    return ptrInt.ptr;
}

/**
 * Stops the receiver of multicast data-products of a downstream LDM-7.
 * Idempotent.
 *
 * @pre              The downstream LDM-7 state is unlocked.
 * @param[in] arg    The downstream LDM-7.
 * @post             The downstream LDM-7 state is unlocked.
 */
static void
stopMcastRecvTask(
        void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    lockState(down7);
    if (down7->mlr) {
        udebug("stopMcastRecvTask(): Stopping multicast data-product receiving task");
        mlr_stop(down7->mlr);
    }
    unlockState(down7);
}

/**
 * Starts a task that receives data-products via multicast.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @retval    LDM7_SHUTDOWN  `stopMcastRecvTask()` was called.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 */
static int
startMcastRecvTask(
        Down7* const down7)
{
    udebug("startMcastRecvTask(): Entered");

    Mlr* const   mlr = mlr_new(down7->mcastInfo, down7->mcastIface, down7);
    int          status;

    if (mlr == NULL) {
        LOG_ADD0("Couldn't create a new multicast LDM receiver");
        status = LDM7_SYSTEM;
    }
    else {
        /*
         * `down7->mlr` must exist before a separate thread is created so that
         * the task can be stopped by `stopMcastRecvTask()`.
         */
        down7->mlr = mlr;
        status = exe_submit(down7->executor, startMcastRecvTask2, down7,
                stopMcastRecvTask, NULL);
        if (status == EINVAL) {
            status = LDM7_SHUTDOWN;
        }
        else if (status) {
            LOG_ADD0("Couldn't submit multicast receiving task");
            status = LDM7_SYSTEM;
        }
    }

    udebug("startMcastRecvTask(): Returning %d", status);
    return status;
}

/**
 * Starts a concurrent task of a downstream LDM-7 that helps receive
 * data-products. Returns immediately.
 *
 * @param[in]  exe            The executor.
 * @param[in]  start          The start function.
 * @param[in]  arg            The argument to the start function.
 * @param[in]  stop           The stop function or NULL.
 * @param[in]  desc           Description of the job.
 * @param[out] job            The job or NULL.
 * @retval     0              Success.
 * @retval     LDM7_SHUTDOWN  `down7_stop()` was called. The task isn't running.
 * @retval     LDM7_SYSTEM    Error. `log_start()` called. The task isn't
 *                            running.
 */
static int
startRecvTask(
        Executor* const restrict   exe,
        void*              (*const start)(void*),
        void* restrict             arg,
        void               (*const stop)(void*),
        const char* const restrict desc,
        Job** const restrict       job)
{
    int status = exe_submit(exe, start, arg, stop, job);

    if (status == EINVAL) {
        log_clear();
        status = LDM7_SHUTDOWN;
    }
    else if (status) {
        LOG_ERRNUM1(status, "Couldn't start task that %s", desc);
        status = LDM7_SYSTEM;
    }

    udebug("startRecvTask(): Returning %d", status);
    return status;
}

/**
 * Starts the concurrent tasks of a downstream LDM-7 that collectively receive
 * data-products. Returns immediately.
 *
 * @pre                       The state of the downstream LDM-7 is locked.
 * @param[in]  down7          Pointer to the downstream LDM-7.
 * @retval     0              Success.
 * @retval     LDM7_SHUTDOWN  The LDM-7 has been shut down. No task is running.
 * @retval     LDM7_SYSTEM    Error. `log_start()` called. No task is running.
 * @post                      The state of the downstream LDM-7 is locked.
 */
static int
startRecvTasks(
    Down7* const             down7)
{
    int status = startRecvTask(down7->executor, startUcastRecvTask, down7,
            NULL, "receives data-products that were missed by the multicast "
            "LDM receiving task", NULL);
    if (status == 0) {
        status = startRecvTask(down7->executor, startRequestTask, down7,
                stopRequestTask, "requests data-products that were missed by "
                "the multicast LDM receiving task", NULL);
        if (status == 0)
            status = startMcastRecvTask(down7);
    }

    if (status == LDM7_SYSTEM && exe_shutdown(down7->executor))
        LOG_ADD0("Couldn't shut down task executor");

    udebug("startRecvTasks(): Returning %d", status);
    return status;
}

/**
 * @param[in] down7         Downstream LDM-7
 * @retval    0             Success. All tasks terminated with exit status 0.
 * @retval    LDM_SHUTDOWN  `down7_stop()` was called.
 * @retval    LDM_SYSTEM    System error. `log_add()` called.
 */
static int
reapRecvTasks(
        Down7* const down7)
{
    int status = 0;

    while (exe_count(down7->executor) > 0) {
        Job* job = exe_getCompleted(down7->executor);

        PtrInt ptrInt;
        ptrInt.ptr = job_result(job);
        int    result = ptrInt.val;

        if (job_wasStopped(job) || job_status(job) || result) { // in that order
            if (exe_shutdown(down7->executor)) {
                LOG_ADD0("Couldn't shut down task executor");
                status = LDM7_SYSTEM;
            }
            else {
                (void)exe_clear(down7->executor); // frees jobs
                if (status == 0)
                    status = job_wasStopped(job)
                        ? LDM7_SHUTDOWN
                        : job_status(job)
                          ? LDM7_SYSTEM
                          : result;
            }
        }

        job_free(job);
    }

    udebug("reapRecvTasks(): Returning %d", status);
    return status;
}

/**
 * Receives products from an upstream LDM-7. Doesn't return until the
 * termination condition variable is signaled and the state is no longer
 * `DOWN7_RUNNING` or an error occurs.
 *
 * @pre                        The downstream LDM-7 state is locked.
 * @param[in] down7            Pointer to the downstream LDM-7.
 * @retval    LDM7_RPC         RPC error. `log_start()` called.
 * @retval    LDM7_SHUTDOWN    The downstream LDM-7 was shut down.
 * @retval    LDM7_SYSTEM      System error. `log_start()` called.
 * @retval    LDM7_TIMEDOUT    A timeout occurred. `log_start()` called.
 * @post                       The downstream LDM-7 state is locked.
 */
static int
receive(
    Down7* const down7)
{
    int status = startRecvTasks(down7);

    if (status) {
        LOG_ADD0("Couldn't start downstream LDM-7 receiving tasks");
    }
    else {
        unlockState(down7); // allow tasks to be stopped by `down7_stop()`
        status = reapRecvTasks(down7);
        lockState(down7);
    }

    udebug("receive(): Returning %d", status);
    return status;
}

static void
freeClient(
        void* arg)
{
    Down7* const   down7 = (Down7*)arg;
    up7proxy_free(down7->up7proxy); // won't close externally-created socket
    down7->up7proxy = NULL;
    (void)close(down7->sock);
    down7->sock = -1;
}

/**
 * Subscribes a downstream LDM-7 by contacting the upstream LDM-7 server. Frees
 * and then sets `down7->mcastInfo`. Called by `pthread_create()`.
 *
 * @pre                      The current thread has deferred cancelability.
 * @param[in] down7          Pointer to the downstream LDM-7.
 *            0              Success. `down7->up7proxy`, `down7->sock`, and
 *                           `down7->mcastInfo` are set.
 * @retval    LDM7_REFUSED   `log_log(LOG_WARN)` called.
 * @retval    LDM7_RPC       `log_log(LOG_ERR)` called.
 * @retval    LDM7_SYSTEM    `log_log(LOG_ERR)` called.
 * @retval    LDM7_TIMEDOUT  `log_log(LOG_WARN)` called.
 * @retval    LDM7_UNAUTH    `log_log(LOG_WARN)` called.
 */
static void*
subscribe(
        void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    int          status = newClient(down7); // sets `down7->{up7proxy,sock}`

    if (status == 0) {
        pthread_cleanup_push(freeClient, down7);
        status = up7proxy_subscribe(down7->up7proxy, down7->feedtype,
                &down7->mcastInfo);
        (void)setCancelState(false); // the rest should be quick
        pthread_cleanup_pop(status); // free client on error
    }

    // Because end-of-thread:
    if (status) {
        log_log((status == LDM7_TIMEDOUT || status == LDM7_UNAUTH ||
                status == LDM7_REFUSED)
                ? LOG_WARNING
                : LOG_ERR);
    }
    else {
        log_clear();
    }

    udebug("subscribe(): Returning &%d", status);
    PtrInt ptrInt;
    ptrInt.val = status;
    return ptrInt.ptr;
}

/**
 * Executes the subscription task on a separate thread so that it can be
 * canceled.
 *
 * @pre                      The downstream LDM-7 state is locked.
 * @pre                      `exe_count(down7->executor) == 0`
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @retval    0              Success. `down7->sock`, `down7->up7proxy`, and
 *                           `down7->mcastInfo` are set.
 * @retval    LDM7_SHUTDOWN  LDM-7 was shut down.
 * @retval    LDM7_TIMEDOUT  Timeout occurred. `log_start()` called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). `log_start()`
 *                           called.
 * @retval    LDM7_INVAL     Invalid multicast group name.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 * @post                     The downstream LDM-7 state is locked.
 */
static int
execSubscriptionTask(
    Down7* const down7)
{
    int status;

    UASSERT(down7 != NULL && exe_count(down7->executor) == 0);

    Job* job;
    status = exe_submit(down7->executor, subscribe, down7, NULL, &job);

    if (status == EINVAL) {
        log_clear();
        status = LDM7_SHUTDOWN;
    }
    else if (status) {
        LOG_ADD0("Couldn't start task that subscribes to a feed");
        status = LDM7_SYSTEM;
    }
    else {
        unlockState(down7); // allow task to be stopped by `down7_stop()`
        job = exe_getCompleted(down7->executor);
        lockState(down7);

        PtrInt ptrInt;
        ptrInt.ptr = job_result(job);
        int    result = ptrInt.val;

        status = job_wasStopped(job)
            ? LDM7_SHUTDOWN
            : job_status(job)
              ? LDM7_SYSTEM
              : result;

        job_free(job);
    }

    udebug("execSubscriptionTask(): Returning %d", status);
    return status;
}

/**
 * Subscribes to a feed from an upstream LDM-7 and receives data-products.
 *
 * @pre                      The downstream LDM-7 state is locked.
 * @param[in] down7          Pointer to the downstream LDM-7 to be executed.
 * @retval    LDM7_INVAL     Invalid multicast group name. `log_start()` called.
 * @retval    LDM7_INVAL     Invalid port number or host identifier.
 *                           `log_start()` called.
 * @retval    LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval    LDM7_REFUSED   Remote host refused connection (server likely
 *                           isn't running). `log_start()` called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). `log_start()`
 *                           called.
 * @retval    LDM7_SHUTDOWN  `down7_stop()` was called.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. `log_start()` called.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 *                           `log_start()` called.
 * @post                     The downstream LDM-7 state is locked.
 */
static int
subscribeAndReceive(
    Down7* const down7)
{
    int status = execSubscriptionTask(down7); // on separate, cancelable thread
    if (status == 0) {
        status = receive(down7);

        mi_free(down7->mcastInfo); // NULL safe
        down7->mcastInfo = NULL;
        udebug("subscribeAndReceive(): Destroying client handle");
        // won't close externally-created socket
        freeClient(down7);
    } // `down7->up7proxy`, `down7->sock`, and `down7->mcastInfo` allocated

    udebug("subscribeAndReceive(): Returning %d", status);
    return status;
}

/**
 * Executes a downstream LDM-7. Doesn't return until the LDM-7 is shut down or
 * an error occurs.
 *
 * @pre                      The downstream LDM-7 state is locked.
 * @param[in] down7          Pointer to the downstream LDM-7 to be executed.
 * @retval    LDM7_INVAL     Invalid multicast group name. `log_start()` called.
 * @retval    LDM7_INVAL     Invalid port number or host identifier.
 *                           `log_start()` called.
 * @retval    LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval    LDM7_REFUSED   Remote host refused connection (server likely
 *                           isn't running). `log_start()` called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). `log_start()`
 *                           called.
 * @retval    LDM7_SHUTDOWN  LDM-7 was shut down.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. `log_start()` called.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 *                           `log_start()` called.
 * @post                     The downstream LDM-7 state is locked.
 */
static int
runDown7Once(
    Down7* const down7)
{
    down7->prevLastMcastSet = mrm_getLastMcastProd(down7->mrm,
            down7->prevLastMcast);
    int status = subscribeAndReceive(down7);
    exe_shutdown(down7->executor);
    exe_clear(down7->executor);
    udebug("runDown7Once(): Returning %d", status);
    return status;
}

/**
 * Waits a short time. Doesn't return until the time period is up or the
 * downstream LDM-7 is stopping.
 *
 * @pre                      The downstream LDM-7 state is locked.
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @post                     The downstream LDM-7 state is locked.
 */
static void
nap(
    Down7* const down7)
{
    int             status = 0;
    struct timespec absTime;

    absTime.tv_sec = time(NULL) + 60; // a time in the future
    absTime.tv_nsec = 0;

    while (down7->state == DOWN7_EXECUTING && status == 0) {
        udebug("nap(): Napping");
        status = pthread_cond_timedwait(&down7->napCond, &down7->stateMutex,
                &absTime);
    }

    UASSERT(status == 0 || status == ETIMEDOUT);
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
    // Products are also inserted on the multicast-receiver threads
    int status = pq_insert(pq, prod);

    if (status) {
        if (status == EINVAL) {
            uerror("Invalid argument");
            status = LDM7_SYSTEM;
        }
        else {
            char buf[LDM_INFO_MAX];

            (void)s_prod_info(buf, sizeof(buf), &prod->info, ulogIsDebug());

            if (0 == status) {
                uinfo("Inserted: %s", buf);
            }
            else if (status == PQUEUE_DUP) {
                uinfo("Duplicate data-product: %s", buf);
            }
            else {
                uwarn("Product too big for queue: %s", buf);
            }

            status = 0; // either too big or duplicate data-product
        }
    }

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
    char buf[LDM_INFO_MAX];

    LOG_ADD2("%s: %s", msg, s_prod_info(buf, sizeof(buf), info, ulogIsDebug()));
    log_log(LOG_ERR);
    (void)svcerr_systemerr(rqstp->rq_xprt);
    svc_destroy(rqstp->rq_xprt);
}

/**
 * Causes a napping downstream LDM-7 to wake up. Returns immediately.
 *
 * @pre              The state of the downstream LDM-7 is locked.
 * @param[in] down7  The running, downstream LDM-7.
 * @post             The state of the downstream LDM-7 is locked.
 */
static inline void
wakeUpNappingDown7(
        Down7* const down7)
{
    (void)pthread_cond_signal(&down7->napCond);
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
 * @param[in] mcastIface  IP address of interface to use for receiving multicast
 *                        packets. Caller may free upon return.
 * @param[in] pqPathname  Pathname of the product-queue.
 * @retval    NULL        Failure. `log_start()` called.
 * @return                Pointer to the new downstream LDM-7.
 */
Down7*
down7_new(
    const ServiceAddr* const restrict servAddr,
    const feedtypet                   feedtype,
    const char* const restrict        mcastIface,
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

    if ((status = pthread_cond_init(&down7->napCond, NULL)) != 0) {
        LOG_ERRNUM0(status,
                "Couldn't initialize condition-variable for napping");
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
                    PTHREAD_MUTEX_ERRORCHECK);

            if ((status = pthread_mutex_init(&down7->stateMutex, &mutexAttr))) {
                LOG_ERRNUM0(status, "Couldn't initialize state-mutex");
                (void)pthread_mutexattr_destroy(&mutexAttr);
                goto free_napCond;
            }

            (void)pthread_mutexattr_destroy(&mutexAttr);
        } // `mutexAttr` initialized
    }

    /*
     * `PQ_THREADSAFE` because the queue is accessed on 3 threads: VCMTP
     * multicast receiver, VCMTP unicast receiver, and LDM-7 data-product
     * receiver.
     */
    if ((status = pq_open(pqPathname, PQ_THREADSAFE, &down7->pq))) {
        LOG_ADD1("Couldn't open product-queue \"%s\"", pqPathname);
        goto free_stateMutex;
    }

    if ((down7->executor = exe_new()) == NULL)
        goto close_pq;

    down7->mcastIface = strdup(mcastIface);
    if (down7->mcastIface == NULL) {
        LOG_ADD1("Couldn't open product-queue \"%s\"", pqPathname);
        goto free_executor;
    }

    udebug("runDown7Once(): Opening multicast session memory");
    down7->mrm = mrm_open(down7->servAddr, down7->feedtype);
    if (down7->mrm == NULL) {
        LOG_ADD0("Couldn't open multicast session memory");
        goto free_mcastIface;
    }

    if ((status = pthread_once(&down7KeyControl, createDown7Key)) != 0)
        goto close_mcastReceiverMemory;

    (void)memset(down7->firstMcast, 0, sizeof(signaturet));
    (void)memset(down7->prevLastMcast, 0, sizeof(signaturet));
    down7->feedtype = feedtype;
    down7->up7proxy = NULL;
    down7->sock = -1;
    down7->mcastInfo = NULL;
    down7->mlr = NULL;
    down7->mcastWorking = false;
    down7->state = DOWN7_INITIALIZED;

    return down7;

close_mcastReceiverMemory:
    if (!mrm_close(down7->mrm))
        LOG_ADD0("Couldn't close multicast receiver memory");
free_mcastIface:
    free(down7->mcastIface);
free_executor:
    (void)exe_free(down7->executor);
close_pq:
    (void)pq_close(down7->pq);
free_stateMutex:
    pthread_mutex_destroy(&down7->stateMutex);
free_napCond:
    pthread_cond_destroy(&down7->napCond);
free_servAddr:
    sa_free(down7->servAddr);
free_down7:
    free(down7);
return_NULL:
    return NULL;
}

/**
 * Returns the product-queue associated with a downstream LDM-7.
 *
 * @param[in] down7  The downstream LDM-7.
 * @return           The associated product-queue.
 */
pqueue* down7_getPq(
        Down7* const down7)
{
    return down7->pq;
}

/**
 * Executes a downstream LDM-7. Doesn't return until `down7_stop()` is called
 * or an error occurs.
 *
 * @param[in] arg            Pointer to downstream LDM-7 object.
 * @retval    0              Success.
 * @retval    LDM7_INVAL     The downstream LDM-7 object is in the wrong state.
 *                           `log_start() called.
 * @retval    LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval    LDM7_SHUTDOWN  `down7_stop()` was called.
 * @retval    LDM7_SYSTEM    System error. `log_start()` called.
 */
Ldm7Status
down7_start(
        Down7* const down7)
{
    int status;

    lockState(down7);

    if (DOWN7_STOPPING == down7->state) {
        status = 0;
    }
    else if (DOWN7_INITIALIZED != down7->state) {
        status = LDM7_INVAL;
    }
    else {
        char* const addrStr = sa_format(down7->servAddr);
        unotice("Downstream LDM-7 starting up: remoteAddr=%s, feedtype=%s, "
                "pq=\"%s\"", addrStr, s_feedtypet(down7->feedtype),
                pq_getPathname(down7->pq));
        free(addrStr);

        down7->state = DOWN7_EXECUTING;
        do {
            status = runDown7Once(down7);

            if (LDM7_SYSTEM == status || LDM7_INVAL == status ||
                    LDM7_MCAST == status || LDM7_SHUTDOWN == status)
                break; // severe error or shut down

            log_log(LOG_NOTICE); // might log nothing

            if (LDM7_TIMEDOUT != status) {
                // Returns immediately if `down7_stop()` called
                nap(down7);
            }
        } while (down7->state == DOWN7_EXECUTING);

        if (down7->state != DOWN7_EXECUTING)
            status = LDM7_SHUTDOWN; // `down7_stop()` called

        down7->state = DOWN7_STOPPED;
    }

    unlockState(down7);

    udebug("down7_start(): Returning %d", status);
    return status;
}

/**
 * Stops a downstream LDM-7. Causes `down7_start()` to return if it hasn't
 * already. Returns immediately.
 *
 * @param[in] down7          The running downstream LDM-7 to be stopped.
 * @retval    0              Success. `down7_run()` should return.
 * @retval    LDM7_SYSTEM    The downstream LDM-7 couldn't be stopped due to a
 *                           system error. `log_log()` called.
 */
Ldm7Status
down7_stop(
        Down7* const down7)
{
    lockState(down7);
    down7->state = DOWN7_STOPPING;
    /*
     * The downstream LDM-7 is unlocked because the stop-functions of the jobs
     * in the job executor are allowed to call `lockState()`.
     */
    unlockState(down7);

    int status = exe_shutdown(down7->executor);
    if (status) {
        LOG_ERRNUM0(status, "Couldn't shut down executor");
        status = LDM7_SYSTEM;
    }
    else {
        lockState(down7);
        wakeUpNappingDown7(down7);
        unlockState(down7);
        status = 0;
    }

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
        lockState(down7);

        Down7State state = down7->state;

        if (DOWN7_INITIALIZED != state && DOWN7_STOPPED != state) {
            LOG_ADD1("Downstream LDM-7 is in an invalid state: %d", state);
            status = LDM7_INVAL;
        }
        else {
            unlockState(down7);
            udebug("%s:down7_free(): Closing multicast receiver memory",
                    __FILE__);
            if (!mrm_close(down7->mrm)) {
                LOG_ADD0("Couldn't close multicast receiver memory");
                status = -1;
            }
            if (exe_free(down7->executor)) { // frees jobs
                LOG_ADD0("Couldn't free task executor");
                status = -1;
            }
            if (pthread_mutex_destroy(&down7->stateMutex)) {
                LOG_ADD0("Couldn't destroy state-mutex");
                status = -1;
            }
            if (pthread_cond_destroy(&down7->napCond)) {
                LOG_ADD0("Couldn't destroy termination condition-variable");
                status = -1;
            }
            if (pq_close(down7->pq)) {
                LOG_ADD0("Couldn't close product-queue");
                status = -1;
            }
            free(down7->mcastIface);
            sa_free(down7->servAddr);
            free(down7);
        }
    }

    return status;
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
    udebug("%s:down7_missedProduct(): Entered: iProd=%lu", __FILE__,
            (unsigned long)iProd);
    (void)mrm_addMissedFile(down7->mrm, iProd);
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
    mrm_setLastMcastProd(down7->mrm, last->signature);

    if (!down7->mcastWorking) {
        down7->mcastWorking = true;
        (void)memcpy(down7->firstMcast, last->signature, sizeof(signaturet));

        int  status = exe_submit(down7->executor, requestSessionBacklog, down7,
                NULL, NULL);
        if (status == EINVAL) {
            log_clear(); // executor was shut down
        }
        else if (status) {
            LOG_ERRNUM0(status, "Couldn't start backlog-requesting task");
            log_log(LOG_ERR);
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

    if (!mrm_peekRequestedFileNoWait(down7->mrm, &iProd) ||
            iProd != missedProd->iProd) {
        deliveryFailure("Unexpected product received", info, rqstp);
    }
    else {
        // The queue can't be empty
        (void)mrm_removeRequestedFileNoWait(down7->mrm, &iProd);

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
