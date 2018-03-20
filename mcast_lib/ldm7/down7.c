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
#include "prod_index_queue.h"
#include "rpc/rpc.h"
#include "rpcutil.h"
#include "timestamp.h"
#include "VirtualCircuit.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
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
    DOWN7_SUBSCRIBING,
    DOWN7_RECEIVING,
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
    /**
     * IP address of interface to use for receiving multicast and unicast
     * packets
     */
    char*                 iface;
    Mlr*                  mlr;           ///< multicast LDM receiver
    /** Persistent multicast receiver memory */
    McastReceiverMemory*  mrm;
    Up7Proxy*             up7proxy;      ///< proxy for upstream LDM-7
    pthread_t             mainThread;
    pthread_t             mcastRecvThread;
    pthread_t             ucastRecvThread;
    pthread_t             missedProdReqThread;
    bool                  haveMainThread;
    bool                  haveUcastRecvThread;
    pthread_mutex_t       mutex;         ///< mutex for changing status
    pthread_cond_t        cond;          ///< condition-variable for napping
    pthread_mutex_t       numProdMutex;  /// Mutex for number of products
    uint64_t              numProds;      ///< number of inserted products
    /** Synchronizes multiple-thread access to client-side RPC handle */
    feedtypet             feedtype;      ///< feed-expression of multicast group
    VcEndPoint            vcEnd;         ///< Local virtual-circuit endpoint
    Ldm7Status            status;        ///< Downstream LDM-7 status
    int                   sock;          ///< socket with remote LDM-7
    volatile bool         mcastWorking;  ///< product received via multicast?
    /** Whether or not `prevLastMcast` is set: */
    bool                  prevLastMcastSet;
};

// Type for obtaining integer status from `pthread_create()` start-function.
typedef union {
    void* ptr;
    int   val;
} PtrInt;

/**
 * Sets whether or not `SIGINT` is blocked for the current thread.
 * @param[in] block   Whether or not to block or unblock
 * @retval    `true`  Iff SIGINT was previously blocked
 */
static bool
blockSigInt(const bool block)
{
    sigset_t sigSet, oldSigSet;
    (void)sigaddset(&sigSet, SIGINT);
    (void)pthread_sigmask(block ? SIG_BLOCK : SIG_UNBLOCK, &sigSet, &oldSigSet);
    return sigismember(&oldSigSet, SIGINT);
}

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
            log_add_errno(status,
                    "Couldn't initialize mutex for upstream LDM-7 proxy");
            status = LDM7_SYSTEM;
        }
        else {
            int sock = socket;
            CLIENT* const clnt = clnttcp_create(sockAddr, LDMPROG, SEVEN, &sock,
                    0, 0);

            if (clnt == NULL) {
                log_add_syserr("Couldn't create RPC client for host %s, port "
                        "%hu: %s", inet_ntoa(sockAddr->sin_addr),
                        ntohs(sockAddr->sin_port), clnt_spcreateerror(""));
                (void)pthread_mutex_destroy(&proxy->mutex);
                status = LDM7_RPC;
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
        Up7Proxy* const proxy = log_malloc(sizeof(Up7Proxy),
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
 * Deletes a proxy for an upstream LDM-7.
 * @param[in] proxy
 */
static void up7proxy_free(
        Up7Proxy* const proxy)
{
    if (proxy) {
        up7proxy_destroyClient(proxy);
        int status = pthread_mutex_destroy(&proxy->mutex);
        if (status) {
            log_errno(status, "Couldn't destroy mutex");
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
    log_assert(status == 0);
    log_assert(proxy->clnt != NULL);
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
    log_assert(status == 0);
}

/**
 * Subscribes to an upstream LDM-7 server.
 *
 * @param[in]  proxy       Proxy for the upstream LDM-7.
 * @param[in]  feed        Feed specification.
 * @param[in]  vcEnd       Local virtual-circuit endpoint
 * @param[out] mcastInfo   Information on the multicast group corresponding to
 *                         `feed`.
 * @retval     0           If and only if success. `*mcastInfo` is set. The
 *                         caller should call `mi_free(*mcastInfo)` when it's no
 *                         longer needed.
 * @retval     LDM7_INVAL  The upstream LDM-7 doesn't multicast `feed`.
 *                         `log_add()` called.
 * @threadsafety           Compatible but not safe
 */
static int
up7proxy_subscribe(
        Up7Proxy* const restrict         proxy,
        feedtypet                        feed,
        const VcEndPoint* const restrict vcEnd,
        McastInfo** const restrict       mcastInfo)
{
    int status;

    up7proxy_lock(proxy);

    CLIENT* const clnt = proxy->clnt;
    McastSubReq   request;
    request.feed = feed;
    request.vcEnd = *vcEnd;

    blockSigInt(false);
    SubscriptionReply* reply = subscribe_7(&request, clnt);
    blockSigInt(true);

    if (reply == NULL) {
        char buf[256];

        (void)sprint_feedtypet(buf, sizeof(buf), feed);
        log_add("Couldn't subscribe to feed %s: %s",  buf,
                clnt_errmsg(clnt));
        status = clntStatusToLdm7Status(clnt);
        up7proxy_destroyClient(proxy);
    }
    else {
        status = reply->status;
        if (status == LDM7_UNAUTH) {
            log_add("This host isn't authorized to receive feed %s",
                    s_feedtypet(feed));
        }
        else if (status == LDM7_NOENT) {
            log_add("Upstream LDM-7 doesn't multicast any part of feed %s",
                    s_feedtypet(feed));
        }
        else if (status != 0) {
            log_add("Couldn't subscribe to feed %s: status=%d",
                    s_feedtypet(feed), status);
        }
        else {
            McastInfo* const mi = &reply->SubscriptionReply_u.info.mcastInfo;
            if (log_is_enabled_debug) {
                char* miStr = mi_format(mi);
                log_debug("Subscription reply is %s", miStr);
                free(miStr);
            }
            *mcastInfo = mi_clone(mi);
        }
        xdr_free(xdr_SubscriptionReply, (char*)reply);
    }

    up7proxy_unlock(proxy);

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
 * @retval    LDM7_RPC  Error in RPC layer. `log_add()` called.
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
        log_add("Couldn't request session backlog: %s", clnt_errmsg(clnt));
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
 * @retval    LDM7_RPC    RPC error. `log_add()` called.
 */
static int
up7proxy_requestProduct(
    Up7Proxy* const      proxy,
    const FmtpProdIndex iProd)
{
    up7proxy_lock(proxy);

    CLIENT* clnt = proxy->clnt;
    int     status;

    log_debug("iProd=%lu", (unsigned long)iProd);
    // Asynchronous send => no reply
    (void)request_product_7((FmtpProdIndex*)&iProd, clnt); // safe cast

    if (clnt_stat(clnt) == RPC_TIMEDOUT) {
        /*
         * The status will always be RPC_TIMEDOUT unless an error occurs
         * because `request_product_7()` uses asynchronous message-passing.
         */
        status = 0;
    }
    else {
        log_add("Couldn't request missed data-product: iProd=%lu: %s",
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
 * @retval    LDM7_RPC  RPC error. `log_add()` called.
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
	log_add("test_connection_7() failure: %s", clnt_errmsg(proxy->clnt));
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
lock(
    Down7* const down7)
{
    log_debug("Locking state");
    int status = pthread_mutex_lock(&down7->mutex);
    log_assert(status == 0);
}

/**
 * Unlocks the state of a downstream LDM-7.
 *
 * @param[in] down7  Pointer to the downstream LDM-7 to have its state unlocked.
 */
static void
unlock(
    Down7* const down7)
{
    log_debug("Unlocking state");
    int status = pthread_mutex_unlock(&down7->mutex);
    log_assert(status == 0);
}

/**
 * Compares and sets the status of a downstream LDM-7.
 * @param[in,out] down7      Downstream LDM-7
 * @param[in]     expect     Expected status
 * @param[in]     newStatus  New status iff `down7->status == expect`
 */
static void
casStatus(
        Down7* const     down7,
        const Ldm7Status expect,
        const Ldm7Status newStatus)
{
    if (down7->status == expect)
        down7->status = newStatus;
}

/**
 * Changes the status of a downstream LDM-7 iff its current status is `LDM7_OK`
 * and signals its condition variable.
 * @param[in,out] down7      Downstream LDM-7
 * @param[in]     newStatus  New status iff `down7->status == LDM7_OK`
 */
static void
changeStatus(
        Down7* const     down7,
        const Ldm7Status newStatus)
{
    lock(down7);
    casStatus(down7, LDM7_UNSET, newStatus);
    pthread_cond_broadcast(&down7->cond);
    unlock(down7);
}

/**
 * Waits for a change in the status of a downstream LDM-7.
 * @param[in] down7  Downstream LDM-7
 */
static void
waitForStatusChange(Down7* const down7)
{
    lock(down7);
    while (down7->status == LDM7_UNSET) {
        int status = pthread_cond_wait(&down7->cond, &down7->mutex);
        log_assert(status == 0);
    }
    unlock(down7);
}

/**
 * Waits for a change in the status of a downstream LDM-7 or a timeout,
 * whichever comes first.
 * @param[in] down7  Downstream LDM-7
 */
static void
timedWaitForStatusChange(
        Down7* const     down7,
        struct timespec* duration)
{
    lock(down7);
    while (down7->status == LDM7_UNSET) {
        int status = pthread_cond_timedwait(&down7->cond, &down7->mutex,
                duration);
        log_assert(status == 0 || status == ETIMEDOUT);
    }
    unlock(down7);
}

/**
 * Returns a socket that's connected to an Internet server via TCP.
 *
 * @param[in]  servAddr       Pointer to the address of the server.
 * @param[in]  family         IP address family to try. One of AF_INET,
 *                            AF_INET6, or AF_UNSPEC.
 * @param[out] sock           Pointer to the socket to be set. The client should
 *                            call `close(*sock)` when it's no longer needed.
 * @param[out] sockAddr       Pointer to the socket address object to be set.
 * @retval     0              Success. `*sock` and `*sockAddr` are set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier.
 *                            `log_add()` called.
 * @retval     LDM7_IPV6      IPv6 not supported. `log_add()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection (server likely
 *                            isn't running). `log_add()` called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. `log_add()`
 *                            called.
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 */
static int
getSock(
    const ServiceAddr* const restrict       servAddr,
    const int                               family,
    int* const restrict                     sock,
    struct sockaddr_storage* const restrict sockAddr)
{
    struct sockaddr_storage addr;
    socklen_t               sockLen;
    int                     status = sa_getInetSockAddr(servAddr, family, false,
            &addr, &sockLen);

    if (status == 0) {
        const int         useIPv6 = addr.ss_family == AF_INET6;
        const char* const addrFamilyId = useIPv6 ? "IPv6" : "IPv4";
        const int         fd = socket(addr.ss_family, SOCK_STREAM, IPPROTO_TCP);

        if (fd == -1) {
            log_add_syserr("Couldn't create %s TCP socket", addrFamilyId);
            status = (useIPv6 && errno == EAFNOSUPPORT)
                    ? LDM7_IPV6
                    : LDM7_SYSTEM;
        }
        else {
            if (connect(fd, (struct sockaddr*)&addr, sockLen)) {
                log_add_syserr("Couldn't connect %s TCP socket to \"%s\", port "
                        "%hu", addrFamilyId, sa_getInetId(servAddr),
                        sa_getPort(servAddr));
                status = (errno == ETIMEDOUT)
                        ? LDM7_TIMEDOUT
                        : (errno == ECONNREFUSED)
                          ? LDM7_REFUSED
                          : LDM7_SYSTEM;
                (void)close(fd);
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
 * Returns a socket that's connected to an Internet server via TCP. Tries
 * address family AF_UNSPEC first, then AF_INET.
 *
 * @param[in]  servAddr       Pointer to the address of the server.
 * @param[out] sock           Pointer to the socket to be set. The client should
 *                            call `close(*sock)` when it's no longer needed.
 * @param[out] sockAddr       Pointer to the socket address object to be set.
 * @retval     0              Success. `*sock` and `*sockAddr` are set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier.
 *                            `log_add()` called.
 * @retval     LDM7_IPV6      IPv6 not supported. `log_add()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection (server likely
 *                            isn't running). `log_add()` called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. `log_add()`
 *                            called.
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 */
static int
getSocket(
    const ServiceAddr* const restrict       servAddr,
    int* const restrict                     sock,
    struct sockaddr_storage* const restrict sockAddr)
{
    struct sockaddr_storage addr;
    socklen_t               sockLen;
    int                     fd;
    int                     status = getSock(servAddr, AF_UNSPEC, &fd, &addr);

    if (status) {
        log_clear();
        status = getSock(servAddr, AF_INET, &fd, &addr);
    }

    if (status == 0) {
        *sock = fd;
        *sockAddr = addr;
    }

    return status;
}

/**
 * Creates a new client-side handle in a downstream LDM-7 for subscribing to
 * its remote LDM-7.
 *
 * @param[in]  down7          Pointer to the downstream LDM-7.
 * @retval     0              Success. `down7->up7proxy` and `down7->sock` are
 *                            set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier.
 *                            `log_add()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection. `log_add()`
 *                            called.
 * @retval     LDM7_RPC       RPC error. `log_add()` called.
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection (server likely
 *                            isn't running). `log_add()` called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. `log_add()`
 *                            called.
 * @retval     LDM7_UNAUTH    Not authorized. `log_add()` called.
 */
static int
newSubClient(
    Down7* const    down7)
{
    int                     sock;
    struct sockaddr_storage sockAddr;
    int                     status = getSocket(down7->servAddr, &sock,
            &sockAddr);

    if (status) {
        char* servAddrStr = sa_format(down7->servAddr);
        log_add("Couldn't create socket to %s", servAddrStr);
        free(servAddrStr);
    }
    else {
        status = up7proxy_new(&down7->up7proxy, sock,
                (struct sockaddr_in*)&sockAddr);
        if (status) {
            (void)close(sock);
        }
        else {
            down7->sock = sock;
        }
    } // `sock` is open

    return status;
}

/**
 * Tests the connection to the upstream LDM-7 of a downstream LDM-7 by sending
 * a no-op/no-reply message to it.
 *
 * @param[in] down7     Pointer to the downstream LDM-7.
 * @retval    0         Success. The connection is still good.
 * @retval    LDM7_RPC  RPC error. `log_add()` called.
 */
static inline int // inline because small and only called in one spot
testConnection(
    Down7* const down7)
{
    return up7proxy_testConnection(down7->up7proxy);
}

/**
 * Runs the RPC-based server of a downstream LDM-7. Destroys and unregisters the
 * service transport. Doesn't return until an error occurs or termination is
 * externally requested.
 *
 * @param[in]     down7          Pointer to the downstream LDM-7.
 * @param[in]     xprt           Pointer to the RPC service transport. Will be
 *                               destroyed upon return.
 * @retval        0              Success. The RPC transport was closed.
 * @retval        LDM7_RPC       Error in RPC layer. `log_add()` called.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 */
static int
run_svc(
    Down7* const restrict   down7,
    SVCXPRT* restrict       xprt)
{
    int           status;
    const int     sock = xprt->xp_sock;
    int           timeout = interval * 1000; // probably 30 seconds
    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;
    for (;;) {
        log_debug("Calling poll(): socket=%d", sock);
        blockSigInt(false);
        status = poll(&pfd, 1, timeout);
        blockSigInt(true);
        if (0 == status) {
            // Timeout
            status = testConnection(down7);
            if (status)
                break;
            continue;
        }
        if (0 > status) {
            log_add_syserr("poll() error on socket %d", sock);
            status = LDM7_SYSTEM;
            break;
        }
        if ((pfd.revents & POLLHUP) || (pfd.revents & POLLERR)) {
            log_debug("RPC transport socket closed or in error");
            status = 0;
            break;
        }
        if (pfd.revents & POLLIN) {
            svc_getreqsock(sock); // Process RPC message. Calls ldmprog_7()
        }
        if (!FD_ISSET(sock, &svc_fdset)) {
            // Here if the upstream LDM-7 closed the connection
            log_debug("The RPC layer destroyed the service transport");
            xprt = NULL;
            status = 0;
            break;
        }
    }
    if (xprt != NULL)
        svc_destroy(xprt);
    return status; // Eclipse IDE wants to see a return
}

/**
 * Runs the RPC-based data-product receiving service of a downstream LDM-7.
 * Destroys and unregisters the service transport. Doesn't return until an
 * error occurs or the server transport is closed.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @param[in] xprt           Pointer to the server-side transport object. Will
 *                           be destroyed upon return.
 * @retval    0              Success. The server transport is closed.
 * @retval    LDM7_RPC       An RPC error occurred. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
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
        log_errno(status,
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
        log_notice("Downstream LDM-7 server terminated");
    } // thread-specific pointer to downstream LDM-7 is set
    return status;
}

/**
 * Requests the backlog of data-products from the previous session. The backlog
 * comprises all products since the last product received by the associated
 * multicast LDM receiver from the previous session (or the time-offset if
 * that product isn't found) to the first product received by the associated
 * multicast LDM receiver of this session (or the current time if that product
 * isn't found).
 *
 * Called by `pthread_create()`.
 *
 * NB: If the current session ends before all backlog products have been
 * received, then the backlog products that weren't received will never be
 * received.
 *
 * This function blocks until the client-side handle is available.
 *
 * @param[in] arg       Pointer to downstream LDM-7.
 * @retval    0         Success.
 * @retval    LDM7_RPC  Error in RPC layer. `log_flush()` called.
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
    int status = up7proxy_requestSessionBacklog(down7->up7proxy, &spec);
    if (status)
        log_error("Couldn't request session backlog");
    log_free();
    return NULL;
}

/**
 * Requests data-products that were missed by the multicast LDM receiver.
 * Entries from the missed-but-not-requested queue are removed and converted
 * into requests for missed data-products, which are asynchronously sent to the
 * remote LDM-7. Doesn't return until `stopMissedProdRequester()` is called or
 * an unrecoverable error occurs.
 *
 * Called by `pthread_create()`.
 *
 * Attempts to set the downstream LDM-7 status.
 *
 * @param[in] arg            Pointer to the downstream LDM-7 object
 * @retval    LDM7_SHUTDOWN  `stopMissedProdRequester()` was called
 * @retval    LDM7_RPC       Error in RPC layer. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 * @see `stopMissedProdRequester()`
 */
static void*
runMissedProdRequester(void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    int          status;
    for (;;) {
        /*
         * The semantics and order of the following actions are necessary to
         * preserve the meaning of the two queues and to ensure that all missed
         * data-products are received following a restart.
         */
        FmtpProdIndex iProd;
        if (!mrm_peekMissedFileWait(down7->mrm, &iProd)) {
            log_debug("The queue of missed data-products has been shutdown");
            status = 0;
            break;
        }
        else {
            if (!mrm_addRequestedFile(down7->mrm, iProd)) {
                log_add("Couldn't add FMTP product-index to requested-queue");
                status = LDM7_SYSTEM;
                break;
            }
            else {
                /* The queue can't be empty */
                (void)mrm_removeMissedFileNoWait(down7->mrm, &iProd);
                status = up7proxy_requestProduct(down7->up7proxy, iProd);
                if (status) {
                    log_add("Couldn't request product");
                    break;
                }
            } // product-index added to requested-but-not-received queue
        } // have a peeked-at product-index from the missed-but-not-requested queue
    }
    changeStatus(down7, status);
    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);
    log_free();
    return NULL;
}

/**
 * Starts a thread on which data-products that were missed by the multicast LDM
 * receiver are requested. Entries from the missed-but-not-requested queue are
 * removed and converted into requests for missed data-products, which are
 * asynchronously sent to the remote LDM-7. Doesn't block.
 *
 * @param[in] arg            Pointer to the downstream LDM-7 object.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 * @see `stopMissedProdRequester()`
 */
static Ldm7Status
startMissedProdRequester(Down7* const down7)
{
    int status;
    log_debug("Opening multicast session memory");
    lock(down7);
    down7->mrm = mrm_open(down7->servAddr, down7->feedtype);
    if (down7->mrm == NULL) {
        log_add("Couldn't open multicast session memory");
        status = LDM7_SYSTEM;
    }
    else {
        down7->prevLastMcastSet = mrm_getLastMcastProd(down7->mrm,
                down7->prevLastMcast);
        status = pthread_create(&down7->missedProdReqThread, NULL,
                runMissedProdRequester, down7);
        if (status) {
            log_add_errno(status, "Couldn't start missed-product requesting "
                    "thread");
            mrm_close(down7->mrm);
            down7->mrm = NULL;
            status = LDM7_SYSTEM;
        }
    } // Multicast session memory open
    unlock(down7);
    return status;
}

/**
 * Cleanly stops the concurrent task of a downstream LDM-7 that's requesting
 * data-products that were missed by the multicast LDM receiver by shutting down
 * the queue of missed products and shutting down the socket to the remote LDM-7
 * for writing. Returns immediately.
 *
 * Idempotent.
 *
 * @param[in,out] down7  Downstream LDM-7 whose requesting task is to be stopped.
 */
static Ldm7Status
stopMissedProdRequester(Down7* const down7)
{
    int status;
    log_debug("Entered");
    lock(down7);
    if (!down7->mrm) {
        unlock(down7);
    }
    else {
        unlock(down7);
        log_debug("Stopping missed-product requester");
        mrm_shutDownMissedFiles(down7->mrm);
        if (!mrm_close(down7->mrm)) {
            log_add("Couldn't close multicast receiver memory");
            status = LDM7_SYSTEM;
        }
        else {
            status = pthread_join(down7->missedProdReqThread, NULL);
            if (status) {
                log_add_errno(status, "Couldn't join missed-product requesting "
                        "thread");
                status = LDM7_SYSTEM;
            }
            else {
                lock(down7);
                down7->mrm = NULL;
                unlock(down7);
            }
        }
    } // Multicast receiver session-memory is open
    if (0 <= down7->sock)
        (void)shutdown(down7->sock, SHUT_WR);
    return status;
}

/**
 * Creates an RPC transport for receiving unicast data-product from an upstream
 * LDM-7.
 *
 * @param[in]  sock         The TCP socket connected to the upstream LDM-7.
 * @param[out] rpcXprt      The created RPC transport. Caller should call
 *                          `svc_destroy(xprt)` when it's no longer needed.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 * @retval     LDM7_RPC     RPC error. `log_add()` called.
 */
static int
createUcastRecvXprt(
    const int       sock,
    SVCXPRT** const rpcXprt)
{
    struct sockaddr_in addr;
    socklen_t          addrLen = sizeof(addr);
    int                status = getpeername(sock, (struct sockaddr*)&addr,
            &addrLen);
    if (status) {
        log_add_syserr("Couldn't get Internet address of upstream LDM-7");
        status = LDM7_SYSTEM;
    }
    else {
        SVCXPRT* const xprt = svcfd_create(sock, 0, MAX_RPC_BUF_NEEDED);
        if (xprt == NULL) {
            log_add("Couldn't create server-side RPC transport for receiving "
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
 * part of the backlog. Doesn't complete until an error occurs, or
 * `stopUcastRcvr()` is called.
 *
 * Called by `pthread_create().
 *
 * Attempts to set the downstream LDM-7 status.
 *
 * NB: When this task completes, the TCP socket will have been closed.
 *
 * @param[in] arg  Downstream LDM-7
 * @see            `stopUcastRcvr()`
 */
static void*
runUcastRcvr(void* const arg)
{
    Down7* const down7 = (Down7*)arg;
    SVCXPRT*     xprt;
    int          status = createUcastRecvXprt(down7->sock, &xprt);
    if (0 == status) {
        // Last argument == 0 => don't register with portmapper
        if (!svc_register(xprt, LDMPROG, SEVEN, ldmprog_7, 0)) {
            char* sockSpec = sa_format(down7->servAddr);
            log_add("Couldn't register RPC server for receiving "
                    "data-products from upstream LDM-7 at \"%s\"",  sockSpec);
            free(sockSpec);
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
        } // `ldmprog_7` registered
    } // `xprt` initialized
    changeStatus(down7, status);
    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);
    log_free();
    return NULL;
}

/**
 * Starts a thread on which unicast data-products from the associated upstream
 * LDM-7 are received -- either because they were missed by the multicast LDM
 * receiver or because they are part of the backlog. Doesn't block.
 * @param[in,out] down7        Downstream LDM-7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 * @see   `stopUcastRcvr()`
 */
static Ldm7Status
startUcastRcvr(Down7* const down7)
{
    lock(down7);
    int status = pthread_create(&down7->ucastRecvThread, NULL, runUcastRcvr,
            down7);
    if (status) {
        log_add_errno(status, "Couldn't create unicast receiver thread");
        status = LDM7_SYSTEM;
    }
    else {
        down7->haveUcastRecvThread = true;
    }
    unlock(down7);
    return status;
}

/**
 * Stops the unicast receiver of backlog and missed data-products.
 *
 * Idempotent.
 *
 * @param[in,out] down7        Downstream LDM-7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
stopUcastRcvr(Down7* const down7)
{
    Ldm7Status status;
    lock(down7);
    if (!down7->haveUcastRecvThread) {
        unlock(down7);
    }
    else {
        log_debug("Stopping unicast receiver");
        status = pthread_kill(down7->ucastRecvThread, SIGINT);
        unlock(down7);
        if (status) {
            log_add_errno(status, "Couldn't signal unicast receiving thread");
            status = LDM7_SYSTEM;
        }
        else {
            status = pthread_join(down7->ucastRecvThread, NULL);
            if (status) {
                log_add_errno(status, "Couldn't join unicast receiving thread");
                status = LDM7_SYSTEM;
            }
            else {
                lock(down7);
                down7->haveUcastRecvThread = false;
                unlock(down7);
            }
        } // Unicast receiving thread successfully signaled
    } // `down7->haveUcastRecvThread` is true
    return status;
}

/**
 * Receives data-products via multicast. Doesn't return until
 * `stopMcastRecvTask()` is called or an error occurs.
 *
 * Called by `pthread_create()`.
 *
 * Attempts to set the downstream LDM-7 status.
 *
 * @param[in] arg            Downstream LDM-7
 * @retval    LDM7_SHUTDOWN  `stopMcastRcvr()` was called
 * @retval    LDM7_MCAST     Multicast reception error. `log_add()` called.
 * @see `stopMcastRcvr()`
 */
static void*
runMcastRcvr(void* const arg)
{
    log_debug("Entered");
    Down7* const down7 = (Down7*)arg;
    int          status = mlr_start(down7->mlr); // Blocks
    changeStatus(down7, status);
    // Because end of task
    const log_level_t level = (status && status != LDM7_SHUTDOWN)
            ? LOG_LEVEL_ERROR
            : LOG_LEVEL_INFO;
    log_log(level, "Terminating");
    log_free();
    return NULL;
}

/**
 * Starts a thread that receives data-products via multicast. Doesn't block.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 * @retval    LDM7_SHUTDOWN  `stopMcastRcvr()` was called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 * @see `stopMcastRcvr()`
 */
static int
startMcastRcvr(Down7* const down7)
{
    log_debug("Entered");
    int status;
    lock(down7);
    down7->mlr = mlr_new(down7->mcastInfo, down7->iface, down7);
    if (down7->mlr == NULL) {
        log_add("Couldn't create a new multicast LDM receiver");
        status = LDM7_SYSTEM;
    }
    else {
        /*
         * `down7->mlr` must exist before a separate thread is created so that
         * the task can be stopped by `stopMcastRcvr()`.
         */
        status = pthread_create(&down7->mcastRecvThread, NULL, runMcastRcvr,
                down7);
        if (status) {
            log_add_errno(status, "Couldn't create multicast receiving thread");
            mlr_free(down7->mlr);
            down7->mlr = NULL;
            status = LDM7_SYSTEM;
        }
    } // `down7->mlr` allocated
    unlock(down7);
    return status;
}

/**
 * Stops the receiver of multicast data-products of a downstream LDM-7.
 *
 * Idempotent.
 *
 * @param[in] down7        Downstream LDM-7.
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
stopMcastRcvr(Down7* const down7)
{
    log_debug("Entered");
    int status;
    lock(down7);
    if (down7->mlr == NULL) {
        unlock(down7);
        status = 0;
    }
    else {
        unlock(down7);
        log_debug("Stopping multicast receiver");
        mlr_stop(down7->mlr);
        status = pthread_join(down7->mcastRecvThread, NULL);
        if (status) {
            log_add_errno(status, "Couldn't join multicast receiving thread");
            status = LDM7_SYSTEM;
        }
        else {
            lock(down7);
            mlr_free(down7->mlr);
            down7->mlr = NULL;
            unlock(down7);
        }
    }
    return status;
}

/**
 * Starts the concurrent threads of a downstream LDM-7 that collectively receive
 * data-products. Returns immediately.
 *
 * @param[in]  down7          Pointer to the downstream LDM-7.
 * @retval     0              Success.
 * @retval     LDM7_SHUTDOWN  The LDM-7 has been shut down. No task is running.
 * @retval     LDM7_SYSTEM    Error. `log_add()` called. No task is running.
 */
static int
startRecvThreads(
    Down7* const             down7)
{
    int status = startUcastRcvr(down7);
    if (status) {
        log_add_errno(status, "Couldn't start unicast receiver");
    }
    else {
        status = startMissedProdRequester(down7);
        if (status) {
            log_add("Couldn't start missing-product requester");
        }
        else {
            status = startMcastRcvr(down7);
            if (status) {
                log_add("Couldn't start multicast receiver");
                stopMissedProdRequester(down7);
            }
        } // Missed-product requester started
        if (status)
            stopUcastRcvr(down7);
    } // Unicast receiver started
    return status;
}

/**
 * Stops the threads of a downstream LDM-7 that are receiving data-products.
 *
 * Idempotent.
 *
 * @param[in,out] down7  Downstream LDM-7
 */
static void
stopRecvThreads(Down7* const down7)
{
    stopMcastRcvr(down7);
    stopMissedProdRequester(down7);
    stopUcastRcvr(down7);
}

/**
 * Frees the resources of the subscription client.
 * @param[in] arg  Downstream LDM7
 */
static void
freeSubClient(
        void* arg)
{
    Down7* const   down7 = (Down7*)arg;
    up7proxy_free(down7->up7proxy); // won't close externally-created socket
    down7->up7proxy = NULL;
    (void)close(down7->sock);
    down7->sock = -1;
}

/**
 * Executes a downstream LDM-7. Doesn't return until an error occurs (which
 * includes `down7stop()` being called).
 *
 * @param[in] down7          Pointer to the downstream LDM-7 to be executed.
 * @retval    0              Success
 * @retval    LDM7_INVAL     Invalid port number or host identifier.
 *                           `log_add()` called.
 * @retval    LDM7_NOENT     No such feed. `log_add()` called.
 * @retval    LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval    LDM7_REFUSED   Remote host refused connection (server likely
 *                           isn't running). `log_add()` called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). `log_add()`
 *                           called.
 * @retval    LDM7_SHUTDOWN  LDM-7 was shut down.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. `log_add()` called.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 *                           `log_add()` called.
 */
static int
runDown7Once(Down7* const down7)
{
    int status = newSubClient(down7); // sets `down7->{up7proxy,sock}`
    if (status) {
        log_add("Couldn't create client for subscribing to feed");
    }
    else {
        // Blocks until error, reply, or timeout
        status = up7proxy_subscribe(down7->up7proxy, down7->feedtype,
                &down7->vcEnd, &down7->mcastInfo);
        if (status) {
            log_add("Couldn't subscribe to feed");
        }
        else {
            status = startRecvThreads(down7);
            if (status) {
                log_add("Error starting data-product reception threads");
            }
            else {
                waitForStatusChange(down7);
                stopRecvThreads(down7);
            } // Product reception threads started
            mi_free(down7->mcastInfo); // NULL safe
            down7->mcastInfo = NULL;
        } // `down7->mcastInfo` allocated
        log_debug("Destroying subscribing client");
        freeSubClient(down7);
    } // Subscription client allocated
    return status;
}

/**
 * Waits a short time. Doesn't return until the time period is up or the
 * downstream LDM-7 is stopping.
 *
 * @param[in] down7          Pointer to the downstream LDM-7.
 */
static void
nap(Down7* const down7)
{
    log_debug("Napping");
    struct timespec duration;
    duration.tv_sec = time(NULL) + 60; // a time in the future
    duration.tv_nsec = 0;
    timedWaitForStatusChange(down7, &duration);
}

/**
 * Processes a data-product from a remote LDM-7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM-7.
 *
 * @param[in] down7        The downstream LDM-7.
 * @param[in] prod         The data-product.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static int
deliver_product(
        Down7* const restrict   down7,
        product* const restrict prod)
{
    // Products are also inserted on the multicast-receiver threads
    pqueue* const restrict  pq = down7->pq;
    int                     status = pq_insert(pq, prod);

    if (status == 0) {
        if (log_is_enabled_info) {
            char buf[LDM_INFO_MAX];

            (void)s_prod_info(buf, sizeof(buf), &prod->info,
                    log_is_enabled_debug);
            log_info("Inserted: %s", buf);
        }
        down7_incNumProds(down7);
    }
    else {
        if (status == EINVAL) {
            log_error("Invalid argument");
            status = LDM7_SYSTEM;
        }
        else {
            char buf[LDM_INFO_MAX];

            (void)s_prod_info(buf, sizeof(buf), &prod->info,
                    log_is_enabled_debug);

            if (status == PQUEUE_DUP) {
                log_info("Duplicate data-product: %s", buf);
            }
            else {
                log_warning("Product too big for queue: %s", buf);
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
        log_errno(status,
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

    log_error("%s: %s", msg, s_prod_info(buf, sizeof(buf), info,
            log_is_enabled_debug));
    (void)svcerr_systemerr(rqstp->rq_xprt);
    svc_destroy(rqstp->rq_xprt);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new downstream LDM-7. The instance doesn't receive anything until
 * `down7_start()` is called.
 *
 * @param[in] servAddr    Pointer to the address of the server from which to
 *                        obtain multicast information, backlog products, and
 *                        products missed by the FMTP layer. Caller may free
 *                        upon return.
 * @param[in] feedtype    Feedtype of multicast group to receive.
 * @param[in] mcastIface  IP address of interface to use for receiving multicast
 *                        packets. Caller may free upon return.
 * @param[in] down7Pq     The product-queue. Must be thread-safe (i.e.,
 *                        `pq_getFlags(pq) | PQ_THREADSAFE` must be true).
 * @param[in] vcEnd       Local virtual-circuit endpoint
 * @retval    NULL        Failure. `log_add()` called.
 * @return                Pointer to the new downstream LDM-7.
 * @see `down7_start()`
 */
Down7*
down7_new(
    const ServiceAddr* const restrict servAddr,
    const feedtypet                   feedtype,
    const char* const restrict        mcastIface,
    const VcEndPoint* const restrict  vcEnd,
    pqueue* const restrict            down7Pq)
{
    Down7* const down7 = log_malloc(sizeof(Down7), "downstream LDM-7");
    int          status;

    if (down7 == NULL)
        goto return_NULL;

    /*
     * `PQ_THREADSAFE` because the queue is accessed by this module on 3
     * threads: FMTP multicast receiver, FMTP unicast receiver, and LDM-7
     * data-product receiver.
     */
    if (!(pq_getFlags(down7Pq) | PQ_THREADSAFE)) {
        log_add("Product-queue not thread-safe: %0x", pq_getFlags(down7Pq));
        goto free_down7;
    }

    if ((down7->servAddr = sa_clone(servAddr)) == NULL) {
        char buf[256];

        (void)sa_snprint(servAddr, buf, sizeof(buf));
        log_add("Couldn't clone server address \"%s\"", buf);
        goto free_down7;
    }

    if ((status = pthread_cond_init(&down7->cond, NULL)) != 0) {
        log_errno(status,
                "Couldn't initialize condition-variable for napping");
        goto free_servAddr;
    }

    {
        pthread_mutexattr_t mutexAttr;

        status = pthread_mutexattr_init(&mutexAttr);
        if (status) {
            log_errno(status,
                    "Couldn't initialize attributes of state-mutex");
        }
        else {
            (void)pthread_mutexattr_setprotocol(&mutexAttr,
                    PTHREAD_PRIO_INHERIT);
            (void)pthread_mutexattr_settype(&mutexAttr,
                    PTHREAD_MUTEX_ERRORCHECK );

            if ((status = pthread_mutex_init(&down7->mutex, &mutexAttr))) {
                log_errno(status, "Couldn't initialize state-mutex");
                (void)pthread_mutexattr_destroy(&mutexAttr);
                goto free_cond;
            }

            (void)pthread_mutexattr_destroy(&mutexAttr);
        } // `mutexAttr` initialized
    }

    down7->iface = strdup(mcastIface);
    if (down7->iface == NULL) {
        log_add("Couldn't clone multicast interface specification");
        goto free_stateMutex;
    }

    if (!vcEndPoint_copy(&down7->vcEnd, vcEnd)) {
        log_add("Couldn't copy receiver-side virtual-circuit endpoint");
        goto free_mcastIface;
    }

    if ((status = pthread_once(&down7KeyControl, createDown7Key)) != 0)
        goto free_vcEnd;

    down7->pq = down7Pq;
    (void)memset(down7->firstMcast, 0, sizeof(signaturet));
    (void)memset(down7->prevLastMcast, 0, sizeof(signaturet));
    down7->feedtype = feedtype;
    down7->up7proxy = NULL;
    down7->sock = -1;
    down7->mcastInfo = NULL;
    down7->mlr = NULL;
    down7->mcastWorking = false;
    (void)pthread_mutex_init(&down7->numProdMutex, NULL);
    down7->numProds = 0;
    down7->status = LDM7_UNSET;
    down7->haveUcastRecvThread = false;
    down7->haveMainThread = false;

    return down7;

free_vcEnd:
    vcEndPoint_destroy(&down7->vcEnd);
free_mcastIface:
    free(down7->iface);
close_pq:
    (void)pq_close(down7->pq);
free_stateMutex:
    pthread_mutex_destroy(&down7->mutex);
free_cond:
    pthread_cond_destroy(&down7->cond);
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
 * @param[in,out] down7          downstream LDM-7
 * @retval        LDM7_LOGIC     No prior call to `down7_stop()`. `log_add()`
 *                               called.
 * @retval        LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval        LDM7_SHUTDOWN  `down7_stop()` was called.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 * @see `down7_stop()`
 */
Ldm7Status
down7_start(Down7* const down7)
{
    /*
     * NB: This module uses `SIGINT` to terminate threads; consequently, the
     * code implicitly assumes that `SIGINT` is blocked.
     */
    bool wasBlocked = blockSigInt(true);
    int status;
    lock(down7);
    if (down7->haveMainThread) {
        unlock(down7);
        log_add("Downstream LDM-7 is already running");
        status = LDM7_LOGIC;
    }
    else {
        down7->mainThread = pthread_self();
        down7->haveMainThread = true;
        char* const sockAddr = sa_format(down7->servAddr);
        log_notice("Downstream LDM-7 starting up: remoteAddr=%s, feed=%s, "
                "pq=\"%s\"", sockAddr, s_feedtypet(down7->feedtype),
                pq_getPathname(down7->pq));
        free(sockAddr);
        unlock(down7);
        for (;;) {
            status = runDown7Once(down7);
            switch (status) {
            case LDM7_MCAST:
            case LDM7_SHUTDOWN:
            case LDM7_SYSTEM:
                break;
            case LDM7_TIMEDOUT:
                continue;
            default:
                nap(down7); // Returns immediately if `down7_stop()` called
                continue;
            }
            break;
        }
        lock(down7);
        down7->haveMainThread = false;
        unlock(down7);
    }
    blockSigInt(wasBlocked);
    return status;
}

/**
 * Stops a downstream LDM-7. Causes `down7_start()` to return if it hasn't
 * already. Returns immediately.
 *
 * @param[in] down7          The running downstream LDM-7 to be stopped.
 * @retval    0              Success. `down7_start()` should return.
 * @retval    LDM7_LOGIC     No prior call to `down7_start()`. `log_add()`
 *                           called.
 * @retval    LDM7_SYSTEM    The downstream LDM-7 couldn't be stopped due to a
 *                           system error. `log_flush()` called.
 */
Ldm7Status
down7_stop(Down7* const down7)
{
    int status;
    lock(down7);
    if (!down7->haveMainThread) {
        unlock(down7);
        log_add("Downstream LDM-7 isn't running");
        status = LDM7_LOGIC;
    }
    else {
        unlock(down7);
        changeStatus(down7, LDM7_SHUTDOWN);
        status = pthread_kill(down7->mainThread, SIGINT);
        if (status) {
            log_add_errno(status, "Couldn't signal downstream LDM7's main "
                    "thread");
            status = LDM7_SYSTEM;
        }
        else {
            lock(down7);
            down7->haveMainThread = false;
            unlock(down7);
        }
    }
    return status;
}

/**
 * Increments the number of data-products successfully inserted into the
 * product-queue of a downstream LDM-7.
 *
 * @param[in] down7  The downstream LDM-7.
 */
void
down7_incNumProds(
        Down7* const down7)
{
    int status = pthread_mutex_lock(&down7->numProdMutex);
    log_assert(status == 0);
    down7->numProds++;
    pthread_mutex_unlock(&down7->numProdMutex);
    log_assert(status == 0);
}

/**
 * Returns the number of data-products successfully inserted into the product-
 * queue of a downstream LDM-7.
 *
 * @param[in] down7  The downstream LDM-7.
 * @return           The number of successfully inserted data-products.
 */
uint64_t
down7_getNumProds(
        Down7* const down7)
{
    int status = pthread_mutex_lock(&down7->numProdMutex);
    log_assert(status == 0);
    uint64_t num = down7->numProds;
    pthread_mutex_unlock(&down7->numProdMutex);
    log_assert(status == 0);
    return num;
}

/**
 * Returns the number of reserved spaces in the product-queue for which
 * pqe_insert() or pqe_discard() have not been called.
 *
 * @param[in] down7  The downstream LDM-7.
 */
long
down7_getPqeCount(
        Down7* const down7)
{
    return pqe_get_count(down7->pq);
}

/**
 * Frees the resources of a downstream LDM-7 returned by `down7_new()` that
 * either wasn't started or has been stopped.
 *
 * @pre                    The downstream LDM-7 was returned by `down7_new()`
 *                         and either `down7_start()` has not been called on it
 *                         or `down7_stop()` has been called on it.
 * @param[in] down7        Pointer to the downstream LDM-7 to be freed or NULL.
 * @retval    0            Success.
 * @retval    LDM7_LOGIC   `down7_start()` has been called but no subsequent
 *                         `down7_stop()`. `log_add()` called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
int
down7_free(Down7* const down7)
{
    int status = 0;
    if (down7) {
        lock(down7);
        if (down7->haveMainThread) {
            unlock(down7);
            log_add("Downstream LDM-7 is running!");
            status = LDM7_LOGIC;
        }
        else {
            unlock(down7);
            (void)pthread_mutex_destroy(&down7->numProdMutex);
            log_debug("Closing multicast receiver memory");
            if (pthread_mutex_destroy(&down7->mutex)) {
                log_add("Couldn't destroy downstream LDM-7 mutex");
                status = LDM7_SYSTEM;
            }
            if (pthread_cond_destroy(&down7->cond)) {
                log_add("Couldn't destroy downstream LDM-7 condition-variable");
                status = LDM7_SYSTEM;
            }
            free(down7->iface);
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
 * @param[in] iProd   Index of the missed FMTP product.
 */
void
down7_missedProduct(
    Down7* const         down7,
    const FmtpProdIndex iProd)
{
    /*
     * Cancellation of the operation of the missed-but-not-requested queue is
     * ignored because nothing can be done about it at this point and no harm
     * should result.
     */
    log_debug("Entered: iProd=%lu", (unsigned long)iProd);
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
        pthread_t thread;
        int       status = pthread_create(&thread, NULL, requestSessionBacklog,
                down7);
        if (status) {
            log_errno(status, "Couldn't start backlog-requesting task");
        }
        else {
            pthread_detach(thread);
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
    FmtpProdIndex   iProd;

    if (!mrm_peekRequestedFileNoWait(down7->mrm, &iProd) ||
            iProd != missedProd->iProd) {
        deliveryFailure("Unexpected product received", info, rqstp);
    }
    else {
        // The queue can't be empty
        (void)mrm_removeRequestedFileNoWait(down7->mrm, &iProd);

        if (deliver_product(down7, &missedProd->prod) != 0)
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
    FmtpProdIndex* const missingIprod,
    struct svc_req* const rqstp)
{
    Down7*         down7 = pthread_getspecific(down7Key);
    FmtpProdIndex iProd;

    if (!mrm_peekRequestedFileNoWait(down7->mrm, &iProd) ||
        iProd != *missingIprod) {
        log_add("Downstream LDM-7 wasn't waiting for product %lu",
                (unsigned long)*missingIprod);
    }
    else {
        // The queue can't be empty
        (void)mrm_removeRequestedFileNoWait(down7->mrm, &iProd);

        log_warning("Upstream LDM-7 says requested product doesn't exist: "
                "prodIndex=%lu", (unsigned long)*missingIprod);
    }

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

    if (deliver_product(down7, prod))
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

    log_notice("All backlog data-products received: feedtype=%s, server=%s",
            s_feedtypet(down7->feedtype),
            sa_snprint(down7->servAddr, saStr, sizeof(saStr)));

    return NULL; // causes RPC dispatcher to not reply
}
