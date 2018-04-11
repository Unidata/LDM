/**
 * This file implements a downstream LDM7. After subscribing to a multicast
 * feed, separate threads are executed to
 *     - Receive multicast data-products;
 *     - Request the backlog of data-products since the previous session;
 *     - Request data-products that were missed by the multicast receiver, and
 *     - Receive those requested data-products.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down7.c
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "CidrAddr.h"
#include "down7.h"
#include "fmtp.h"
#include "globals.h"
#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
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
 * Key for getting the pointer to a downstream LDM7 that's associated with a
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
 * Thread-safe proxy for an upstream LDM7 associated with a downstream LDM7.
 */
typedef struct {
    char*                 remoteId; ///< Socket address of upstream LDM7
    CLIENT*               clnt;     ///< client-side RPC handle
    pthread_mutex_t       mutex;    ///< because accessed by multiple threads
} Up7Proxy;

/**
 * The data structure of a downstream LDM7:
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
    ServiceAddr*          servAddr;      ///< socket address of remote LDM7
    McastInfo*            mcastInfo;     ///< information on multicast group
    /**
     * IP address of interface to use for receiving multicast and unicast
     * packets
     */
    char*                 iface;
    Mlr*                  mlr;           ///< multicast LDM receiver
    /// Persistent multicast receiver memory
    McastReceiverMemory*  mrm;
    Up7Proxy*             up7proxy;      ///< proxy for upstream LDM7
    pthread_t             mainThread;
    pthread_t             mcastRecvThread;
    pthread_t             ucastRecvThread;
    pthread_t             missedProdReqThread;
    bool                  haveMainThread;
    bool                  haveUcastRecvThread;
    pthread_mutex_t       mutex;         ///< Mutex for state changes
    pthread_cond_t        cond;          ///< Condition-variable for status
    pthread_mutex_t       numProdMutex;  ///< Mutex for number of products
    uint64_t              numProds;      ///< Number of inserted products
    feedtypet             feedtype;      ///< Feed of multicast group
    VcEndPoint            vcEnd;         ///< Local virtual-circuit endpoint
    Ldm7Status            status;        ///< Downstream LDM7 status
    int                   sock;          ///< Socket with remote LDM7
    volatile bool         mcastWorking;  ///< Product received via multicast?
    bool                  prevLastMcastSet; ///< `prevLastMcast` set?
    char*                 upId;          ///< ID of upstream LDM7
    char*                 feedId;        ///< Desired feed specification
    bool                  done;          ///< Are we done yet?
};

// Type for obtaining integer status from `pthread_create()` start-function.
typedef union {
    void* ptr;
    int   val;
} PtrInt;

/**
 * Sets whether or not `SIGTERM` is blocked for the current thread.
 * @param[in] block   Whether or not to block or unblock
 * @retval    `true`  Iff SIGTERM was previously blocked
 */
static bool
setSigTerm(const bool block)
{
    sigset_t sigSet, oldSigSet;
    sigemptyset(&sigSet);
    int status = sigaddset(&sigSet, SIGTERM);
    if (status)
        log_abort("Couldn't add SIGTERM to signal-set");
    status = pthread_sigmask(block ? SIG_BLOCK : SIG_UNBLOCK, &sigSet,
            &oldSigSet);
    if (status)
        log_abort("Couldn't %sblock SIGTERM", block ? "" : "un");
    return sigismember(&oldSigSet, SIGTERM);
}

inline static bool
blockSigTerm()
{
    return setSigTerm(true);
}

inline static bool
unblockSigTerm()
{
    return setSigTerm(false);
}

static void
assertSigTermTest(const bool isBlocked)
{
#ifndef NDEBUG
    sigset_t sigSet;
    int      status = pthread_sigmask(SIG_BLOCK, NULL, &sigSet);
    log_assert(status == 0);
    if (isBlocked != sigismember(&sigSet, SIGTERM))
        log_abort("SIGTERM is%s blocked", isBlocked ? "n't" : "");
#endif
}

inline static void
assertSigTermBlocked()
{
    assertSigTermTest(true);
}

inline static void
assertSigTermUnblocked()
{
    assertSigTermTest(false);
}

/**
 * Asserts that a non-recursive mutex is locked. Calls `log_abort()` if it
 * isn't.
 * @param[in] mutex     Non-recursive mutex
 */
inline static void
assertLocked(pthread_mutex_t* const mutex)
{
#ifndef NDEBUG
    int status = pthread_mutex_trylock(mutex);
    if (status == 0)
        log_abort("Mutex wasn't locked");
#endif
}

/**
 * Asserts that a non-recursive mutex is unlocked. Calls `log_abort()` if it
 * isn't.
 * @param[in] mutex     Non-recursive mutex
 */
inline static void
assertUnlocked(pthread_mutex_t* const mutex)
{
#ifndef NDEBUG
    int status = pthread_mutex_trylock(mutex);
    if (status != 0)
        log_abort("Mutex was locked");
    (void)pthread_mutex_unlock(mutex);
#endif
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
            log_add_errno(status, "Couldn't initialize mutex");
            status = LDM7_SYSTEM;
        }
        else {
            proxy->remoteId = sockAddrIn_format(sockAddr);
            if (proxy->remoteId == NULL) {
                log_add("Couldn't format socket address of upstream LDM7");
                status = LDM7_SYSTEM;
            }
            else {
                int sock = socket;
                proxy->clnt = clnttcp_create(sockAddr, LDMPROG, SEVEN,
                        &sock, 0, 0);
                if (proxy->clnt == NULL) {
                    log_add_syserr("Couldn't create RPC client for %s: %s",
                            proxy->remoteId);
                    (void)pthread_mutex_destroy(&proxy->mutex);
                    status = LDM7_RPC;
                }
                else {
                    status = 0;
                }
                if (status)
                    free(proxy->remoteId);
            } // `proxy->remoteId` allocated
            if (status)
                pthread_mutex_destroy(&proxy->mutex);
        } // `proxy->mutex` allocated
    } // Non-NULL input arguments

    return status;
}

/**
 * Returns a new proxy for an upstream LDM7.
 *
 * @param[out] up7proxy     Pointer to the new proxy.
 * @param[in]  socket       The socket to use.
 * @param[in]  sockAddr     The address of the upstream LDM7 server.
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
                "upstream LDM7 proxy");

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
 * @param[in] proxy  Upstream LDM7 proxy.
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
 * Deletes a proxy for an upstream LDM7.
 * @param[in] proxy
 */
static void up7proxy_free(
        Up7Proxy* const proxy)
{
    if (proxy) {
        up7proxy_destroyClient(proxy);
        int status = pthread_mutex_destroy(&proxy->mutex);
        if (status)
            log_errno_q(status, "Couldn't destroy mutex");
        free(proxy->remoteId);
        free(proxy);
    }
}

/**
 * Locks an upstream LDM7 proxy for exclusive access.
 *
 * @pre                   `proxy->clnt != NULL`
 * @param[in] proxy       Pointer to the upstream LDM7 proxy to be locked.
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
 * Unlocks an upstream LDM7 proxy.
 *
 * @param[in] proxy       Pointer to the upstream LDM7 proxy to be unlocked.
 */
static void
up7proxy_unlock(
    Up7Proxy* const proxy)
{
    int status = pthread_mutex_unlock(&proxy->mutex);
    log_assert(status == 0);
}

/**
 * Subscribes to an upstream LDM7 server.
 *
 * @param[in]  proxy          Proxy for the upstream LDM7.
 * @param[in]  feed           Feed specification.
 * @param[in]  vcEnd          Local virtual-circuit endpoint
 * @param[out] mcastInfo      Information on the multicast group corresponding
 *                            to `feed`.
 * @retval     0              If and only if success. `*mcastInfo` is set. The
 *                            caller should call `mi_free(*mcastInfo)` when it's
 *                            no longer needed.
 * @retval     LDM7_NOENT     The upstream LDM7 doesn't multicast `feed`.
 *                            `log_add()` called.
 * @retval     LDM7_TIMEDOUT  Subscription request timed-out. `log_add()`
 *                            called.
 * @retval     LDM7_REFUSED   Upstream host refused connection (LDM7 not
 *                            running?). `log_add()` called.
 * @retval     LDM7_SYSTEM    System failure. `log_add()` called.
 * @retval     LDM7_UNAUTH    Upstream LDM7 denied request. `log_add()` called.
 * @retval     LDM7_RPC       Generic RPC error. `log_add()` called.
 * @threadsafety              Compatible but not safe
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

    unblockSigTerm(); // Because on main thread
    /*
     * WARNING: If a standard RPC implementation is used, then it is likely that
     * `subscribe_7()` won't return when `SIGTERM` is received because
     * `readtcp()` in `clnt_tcp.c` ignores `EINTR`. The RPC implementation
     * included with the LDM package has been modified to not have this problem.
     * -- Steve Emmerson 2018-03-26
     */
    SubscriptionReply* reply = subscribe_7(&request, clnt);
    blockSigTerm();

    if (reply == NULL) {
        char* feedStr = feedtypet_format(feed);
        log_add("Couldn't subscribe to feed %s from %s: %s",  feedStr,
                proxy->remoteId, clnt_errmsg(clnt));
        free(feedStr);
        status = clntStatusToLdm7Status(clnt);
        up7proxy_destroyClient(proxy);
    }
    else {
        status = reply->status;
        if (status == LDM7_UNAUTH) {
            log_add("Subscription to feed %s denied by %s ",
                    s_feedtypet(feed), proxy->remoteId);
        }
        else if (status == LDM7_NOENT) {
            log_add("%s doesn't multicast any part of feed %s", proxy->remoteId,
                    s_feedtypet(feed));
        }
        else if (status != 0) {
            log_add("Couldn't subscribe to feed %s from %s: status=%d",
                    s_feedtypet(feed), proxy->remoteId, status);
        }
        else {
            const McastInfo* const mi =
                    &reply->SubscriptionReply_u.info.mcastInfo;
            char*                  miStr = mi_format(mi);
            const CidrAddr* const  fmtpAddr =
                    &reply->SubscriptionReply_u.info.fmtpAddr;
            char*                  fmtpAddrStr = cidrAddr_format(fmtpAddr);
            log_notice_q("Subscription reply from %s: mcastGroup=%s,"
                    "fmtpAddr=%s", proxy->remoteId, miStr, fmtpAddrStr);
            free(fmtpAddrStr);
            free(miStr);
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
 * @param[in] arg       Pointer to upstream LDM7 proxy.
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
 * @param[in] proxy       Pointer to the upstream LDM7 proxy.
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
    log_debug_1("iProd=%lu", (unsigned long)iProd);
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
 * Tests the connection to an upstream LDM7 by sending a no-op/no-reply message
 * to it.
 *
 * @param[in] proxy     Pointer to the proxy for the upstream LDM7.
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

inline static void
down7_assertLocked(Down7* const down7)
{
    assertLocked(&down7->mutex);
}

inline static void
down7_assertUnlocked(Down7* const down7)
{
    assertUnlocked(&down7->mutex);
}

/**
 * Locks the state of a downstream LDM7.
 *
 * @pre              Downstream LDM7 is unlocked
 * @param[in] down7  Downstream LDM7 to have its state locked
 * @post             Downstream LDM7 is locked
 */
static void
down7_lock(Down7* const down7)
{
    log_debug_1("Locking state");
    int status = pthread_mutex_lock(&down7->mutex);
#ifndef NDEBUG
    if (status) {
        log_errno_q(status, "Mutex can't be locked");
        abort();
    }
#endif
}

/**
 * Unlocks the state of a downstream LDM7.
 *
 * @param[in] down7  Pointer to the downstream LDM7 to have its state unlocked.
 */
static void
down7_unlock(Down7* const down7)
{
    log_debug_1("Unlocking state");
    int status = pthread_mutex_unlock(&down7->mutex);
#ifndef NDEBUG
    if (status) {
        log_errno_q(status, "Mutex can't be unlocked");
        abort();
    }
#endif
}

static void
down7_assertUnstoppable(Down7* const down7)
{
#ifndef NDEBUG
    assertSigTermBlocked();
    down7_assertLocked(down7);
#endif
}

static void
down7_makeStoppable(Down7* const down7)
{
    assertSigTermBlocked();
    down7_assertLocked(down7);
    unblockSigTerm();
    down7_unlock(down7);
}

/**
 * Makes a downstream LDM7 and the current thread immune to `down7_stop()` by
 * calling `down7_lock()` and `blockSigTerm()`.
 * @pre                   Downstream LDM7 is unlocked
 * @param[in,out] down7   Downstream LDM7
 * @retval        `true`  Iff `SIGTERM` was already blocked
 * @post                  Downstream LDM7 is locked and current thread blocks
 *                        `SIGTERM`
 */
static bool
down7_makeUnstoppable(Down7* const down7)
{
    down7_assertUnlocked(down7);
    down7_lock(down7);
    return blockSigTerm();
}

/**
 * Sets the status of a downstream LDM7 and signals the condition variable.
 * @pre                      Downstream LDM7 is locked
 * @param[in,out] down7      Downstream LDM7
 * @param[in]     newStatus  New status iff `down7->status == LDM7_OK`
 * @post                     Downstream LDM7 is locked
 */
static void
down7_setStatus(
        Down7* const     down7,
        const Ldm7Status newStatus)
{
    down7_assertLocked(down7);
    down7->status = newStatus;
    // Not `pthread_cond_broadcast()` because only the main-thread waits
    pthread_cond_signal(&down7->cond);
}

/**
 * Compares and sets the status of a downstream LDM7.
 * @pre                      Downstream LDM7 is locked
 * @param[in,out] down7      Downstream LDM7
 * @param[in]     expect     Expected status
 * @param[in]     newStatus  New status iff `down7->status == expect`
 * @return                   Current status
 * @post                     Downstream LDM7 is locked
 */
static Ldm7Status
down7_casStatus(
        Down7* const     down7,
        const Ldm7Status expect,
        const Ldm7Status newStatus)
{
    down7_assertLocked(down7);
    if (down7->status == expect)
        down7_setStatus(down7, newStatus);
    return down7->status;
}

/**
 * Waits for a change in the status of a downstream LDM7 or a timeout,
 * whichever comes first.
 * @pre              Downstream LDM7 is locked
 * @param[in] down7  Downstream LDM7
 * @post             Downstream LDM7 is locked
 */
static void
down7_timedWaitForStatusChange(
        Down7* const     down7,
        struct timespec* duration)
{
    down7_assertLocked(down7);
    while (!down7->done && down7->status == LDM7_OK) {
        int status = pthread_cond_timedwait(&down7->cond, &down7->mutex,
                duration);
        log_assert(status == 0 || status == ETIMEDOUT);
    }
}

/**
 * Waits for a change in the status of a downstream LDM7.
 * @pre              Downstream LDM7 is locked
 * @param[in] down7  Downstream LDM7
 * @return           Current status of downstream LDM7
 * @post             Downstream LDM7 is locked
 */
static Ldm7Status
down7_waitForStatusChange(Down7* const down7)
{
    down7_assertLocked(down7);
    while (!down7->done && down7->status == LDM7_OK) {
        int status = pthread_cond_wait(&down7->cond, &down7->mutex);
        log_assert(status == 0);
    }
    int status = down7->status;
    return status;
}

/**
 * Clears the status of a downstream LDM7 by setting it to `LDM7_OK` and signals
 * its condition variable.
 * @param[in,out] down7      Downstream LDM7
 */
static void
down7_clearStatus(Down7* const down7)
{
    down7_lock(down7);
    down7->status = LDM7_OK;
    // Not `pthread_cond_broadcast()` because only the main-thread waits
    pthread_cond_signal(&down7->cond);
    down7_unlock(down7);
}

/**
 * Changes the status of a downstream LDM7 iff its current status is `LDM7_OK`
 * and signals its condition variable.
 * @pre                      Downstream LDM7 is unlocked
 * @param[in,out] down7      Downstream LDM7
 * @param[in]     newStatus  New status iff `down7->status == LDM7_OK`
 * @return                   Current status of downstream LDM7
 * @pre                      Downstream LDM7 is unlocked
 */
static Ldm7Status
down7_setStatusIfOk(
        Down7* const     down7,
        const Ldm7Status newStatus)
{
    down7_lock(down7);
    int status = down7_casStatus(down7, LDM7_OK, newStatus);
    down7_unlock(down7);
    return status;
}

/**
 * Returns the status of a downstream LDM7.
 * @pre          Downstream LDM7 is unlocked
 * @param down7  Downstream LDM7
 * @pre          Downstream LDM7 is unlocked
 * @return
 */
static Ldm7Status
down7_getStatus(Down7* const down7)
{
    down7_lock(down7);
    Ldm7Status status = down7->status;
    down7_unlock(down7);
    return status;
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
 * @retval     LDM7_INTR      Signal caught
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
                char* sockSpec = sa_format(servAddr);
                log_add_syserr("Couldn't connect %s TCP socket to \"%s\"",
                        addrFamilyId, sockSpec);
                free(sockSpec);
                status = (errno == ETIMEDOUT)
                        ? LDM7_TIMEDOUT
                        : (errno == ECONNREFUSED)
                          ? LDM7_REFUSED
                          : (errno == EINTR)
                            ? LDM7_INTR
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
 * @retval     LDM7_INTR      Signal caught
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

    if (status == LDM7_IPV6 || status == LDM7_REFUSED ||
            status == LDM7_TIMEDOUT) {
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
 * Creates a new client-side handle in a downstream LDM7 for subscribing to
 * its remote LDM7.
 *
 * @param[in]  down7          Pointer to the downstream LDM7.
 * @retval     0              Success. `down7->up7proxy` and `down7->sock` are
 *                            set.
 * @retval     LDM7_INTR      Signal caught
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
newSubscriptionClient(Down7* const down7)
{
    int                     sock;
    struct sockaddr_storage sockAddr;
    int                     status = getSocket(down7->servAddr, &sock,
            &sockAddr);

    if (status == LDM7_OK) {
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
 * Runs the RPC-based server of a downstream LDM7. Destroys and unregisters the
 * service transport. Doesn't return until `stopUcastRcvr()` is called or an
 * error occurs
 *
 * @param[in]     down7          Pointer to the downstream LDM7.
 * @param[in]     xprt           Pointer to the RPC service transport. Will be
 *                               destroyed upon return.
 * @retval        0              `stopUcastRcvr()` was called. The RPC transport
 *                               is closed.
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
        log_debug_1("Calling poll(): socket=%d", sock);
        unblockSigTerm(); // Because `SIGTERM` used to stop this thread
        status = poll(&pfd, 1, timeout);
        blockSigTerm();
        if (0 == status) {
            // Timeout
            status = up7proxy_testConnection(down7->up7proxy);
            if (status)
                break;
            continue;
        }
        if (0 > status) {
            if (errno == EINTR) {
                status = LDM7_OK;
            }
            else {
                log_add_syserr("poll() error on socket %d with %s", sock,
                        down7->upId);
                status = LDM7_SYSTEM;
            }
            break;
        }
        if ((pfd.revents & POLLHUP) || (pfd.revents & POLLERR)) {
            log_debug_1("RPC transport socket with %s closed or in error",
                    down7->upId);
            status = 0;
            break;
        }
        if (pfd.revents & POLLIN) {
            svc_getreqsock(sock); // Process RPC message. Calls ldmprog_7()
        }
        if (!FD_ISSET(sock, &svc_fdset)) {
            // Here if the upstream LDM7 closed the connection
            log_debug_1("The RPC layer destroyed the service transport with %s",
                    down7->upId);
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
 * Runs the RPC-based data-product receiving service of a downstream LDM7.
 * Destroys and unregisters the service transport. Doesn't return until
 * `stopUcastRcvr()` is called or an error occurs
 *
 * @param[in] down7          Pointer to the downstream LDM7.
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
     * The downstream LDM7 RPC functions don't know their associated downstream
     * LDM7; therefore, a thread-specific pointer to the downstream LDM7 is
     * set to provide context to those that need it.
     */
    int status = pthread_setspecific(down7Key, down7);
    if (status) {
        log_errno_q(status,
                "Couldn't set thread-specific pointer to downstream LDM7");
        svc_destroy(xprt);
        status = LDM7_SYSTEM;
    }
    else {
        /*
         * The following executes until an error occurs or a signal is caught.
         * It destroys and unregisters the service transport, which will close
         * the downstream LDM7's client socket.
         */
        status = run_svc(down7, xprt);
        log_notice_q("Downstream LDM7 missed-product receiver terminated");
    } // thread-specific pointer to downstream LDM7 is set
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
 * @param[in] arg       Pointer to downstream LDM7.
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
        log_error_q("Couldn't request session backlog");
    log_free();
    return NULL;
}

/**
 * Requests data-products that were missed by the multicast LDM receiver.
 * Entries from the missed-but-not-requested queue are removed and converted
 * into requests for missed data-products, which are asynchronously sent to the
 * remote LDM7. Doesn't return until `stopMissedProdRequester()` is called or
 * an unrecoverable error occurs.
 *
 * Called by `pthread_create()`.
 *
 * Attempts to set the downstream LDM7 status.
 *
 * @param[in] arg            Pointer to the downstream LDM7 object
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
            log_debug_1("The queue of missed data-products has been shutdown");
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
        } // have peeked-at product-index from missed-but-not-requested queue
    }
    down7_setStatusIfOk(down7, status);
    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);
    log_free();
    return NULL;
}

/**
 * Starts a thread on which data-products that were missed by the multicast LDM
 * receiver are requested. Entries from the missed-but-not-requested queue are
 * removed and converted into requests for missed data-products, which are
 * asynchronously sent to the remote LDM7. Doesn't block.
 *
 * @pre                          Downstream LDM7 is locked
 * @param[in,out] arg            Pointer to the downstream LDM7 object.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 * @post                         Downstream LDM7 is locked
 * @see           `stopMissedProdRequester()`
 */
static Ldm7Status
startMissedProdRequester(Down7* const down7)
{
    int status;
    log_debug_1("Opening multicast session memory");
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
    return status;
}

/**
 * Cleanly stops the concurrent task of a downstream LDM7 that's requesting
 * data-products that were missed by the multicast LDM receiver by shutting down
 * the queue of missed products and shutting down the socket to the remote LDM7
 * for writing. Returns immediately.
 *
 * Idempotent.
 *
 * @pre                        Downstream LDM7 is locked
 * @param[in,out] down7        Downstream LDM7 whose requesting task is to be
 *                             stopped.
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 * @post                       Downstream LDM7 is locked
 */
static Ldm7Status
stopMissedProdRequester(Down7* const down7)
{
    down7_assertLocked(down7);
    int status;
    log_debug_1("Entered");
    if (down7->mrm == NULL) {
        status = LDM7_OK;
    }
    else {
        log_debug_1("Stopping missed-product requester");
        mrm_shutDownMissedFiles(down7->mrm);
        if (!mrm_close(down7->mrm)) {
            log_add("Couldn't close multicast receiver memory");
            status = LDM7_SYSTEM;
        }
        else {
            down7_unlock(down7);
            status = pthread_join(down7->missedProdReqThread, NULL);
            down7_lock(down7);
            if (status) {
                log_add_errno(status, "Couldn't join missed-product requesting "
                        "thread");
                status = LDM7_SYSTEM;
            }
            else {
                down7->mrm = NULL;
            }
        }
    } // Multicast receiver session-memory is open
    if (0 <= down7->sock)
        (void)shutdown(down7->sock, SHUT_WR);
    return status;
}

/**
 * Creates an RPC transport for receiving unicast data-product from an upstream
 * LDM7.
 *
 * @param[in]  sock         The TCP socket connected to the upstream LDM7.
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
        log_add_syserr("Couldn't get Internet address of upstream LDM7");
        status = LDM7_SYSTEM;
    }
    else {
        SVCXPRT* const xprt = svcfd_create(sock, 0, MAX_RPC_BUF_NEEDED);
        if (xprt == NULL) {
            log_add("Couldn't create server-side RPC transport for receiving "
                    "data-products from \"%s\"", inet_ntoa(addr.sin_addr));
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
 * Receives unicast data-products from the associated upstream LDM7 -- either
 * because they were missed by the multicast LDM receiver or because they are
 * part of the backlog. Doesn't return until `stopUcastRcvr()` is called or an
 * error occurs. On return
 *   - `down7_setStatusIfOk()` will have been called; and
 *   - The TCP socket will be closed.
 *
 * Called by `pthread_create().
 *
 * @param[in] arg   Downstream LDM7
 * @retval    NULL  Always
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
                    "data-products from \"%s\"",  sockSpec);
            free(sockSpec);
            svc_destroy(xprt);
            status = LDM7_RPC;
        }
        else {
            /*
             * The following executes until an error occurs or termination is
             * externally requested. It destroys and unregisters the service
             * transport, which will close the downstream LDM7's client socket.
             */
            status = run_down7_svc(down7, xprt);
        } // `ldmprog_7` registered
    } // `xprt` initialized
    down7_setStatusIfOk(down7, status);
    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);
    log_free();
    return NULL;
}

/**
 * Starts a thread on which unicast data-products from the associated upstream
 * LDM7 are received -- either because they were missed by the multicast LDM
 * receiver or because they are part of the backlog. Doesn't block.
 * @pre                        Downstream LDM7 is locked
 * @param[in,out] down7        Downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 * @post                       Downstream LDM7 is locked
 * @see          `stopUcastRcvr()`
 */
static Ldm7Status
startUcastRcvr(Down7* const down7)
{
    int status = pthread_create(&down7->ucastRecvThread, NULL, runUcastRcvr,
            down7);
    if (status) {
        log_add_errno(status, "Couldn't create unicast receiver thread");
        status = LDM7_SYSTEM;
    }
    else {
        down7->haveUcastRecvThread = true;
    }
    return status;
}

/**
 * Stops the unicast receiver of backlog and missed data-products.
 *
 * Idempotent.
 *
 * @pre                        Downstream LDM7 is locked
 * @param[in,out] down7        Downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 * @post                       Downstream LDM7 is locked
 */
static Ldm7Status
stopUcastRcvr(Down7* const down7)
{
    down7_assertLocked(down7);
    /*
     * Because the blocking system function `poll()` used by the unicast
     * data-product receiver doesn't respond to the socket being closed,
     * the mechanism for stopping the receiver must differ from the other
     * `stop...()` functions. Possibilities include using
     *   - A second "close requested" file-descriptor: Definitely possible at
     *     the cost of a single file-descriptor.
     *   - `pthread_kill()`: Possible if the RPC package that's included with
     *     the LDM is used rather than a standard RPC implementation (see call
     *     to `subscribe_7()` for more information). Requires that a signal
     *     handler be installed (one is in the top-level LDM). The same solution
     *     will also work to interrupt the `connect()` system call on the main
     *     thread.
     *   - `pthread_cancel()`: The resulting code is considerably more complex
     *     than for `pthread_kill()` and, consequently, more difficult to reason
     *     about.
     * The `pthread_kill()` solution was chosen.
     */
    Ldm7Status status;
    if (!down7->haveUcastRecvThread) {
        status = 0;
    }
    else {
        log_debug_1("Stopping unicast receiver");
        status = pthread_kill(down7->ucastRecvThread, SIGTERM);
        if (status) {
            log_add_errno(status, "Couldn't signal unicast receiving thread");
            status = LDM7_SYSTEM;
        }
        else {
            down7_unlock(down7);
            status = pthread_join(down7->ucastRecvThread, NULL);
            down7_lock(down7);
            if (status) {
                log_add_errno(status, "Couldn't join unicast receiving thread");
                status = LDM7_SYSTEM;
            }
            else {
                down7->haveUcastRecvThread = false;
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
 * Attempts to set the downstream LDM7 status.
 *
 * @param[in] arg            Downstream LDM7
 * @retval    LDM7_SHUTDOWN  `stopMcastRcvr()` was called
 * @retval    LDM7_MCAST     Multicast reception error. `log_add()` called.
 * @see `stopMcastRcvr()`
 */
static void*
runMcastRcvr(void* const arg)
{
    log_debug_1("Entered");
    Down7* const down7 = (Down7*)arg;
    int          status = mlr_start(down7->mlr); // Blocks
    down7_setStatusIfOk(down7, status);
    // Because end of task
    log_log_q(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO, "Terminating");
    log_free();
    return NULL;
}

/**
 * Starts a thread that receives data-products via multicast. Doesn't block.
 *
 * @pre                          Downstream LDM7 is locked
 * @param[in,out] down7          Pointer to the downstream LDM7.
 * @retval        LDM7_SHUTDOWN  `stopMcastRcvr()` was called.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 * @see           `stopMcastRcvr()`
 */
static int
startMcastRcvr(Down7* const down7)
{
    log_debug_1("Entered");
    int status;
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
    return status;
}

/**
 * Stops the receiver of multicast data-products of a downstream LDM7.
 *
 * Idempotent.
 *
 * @pre                    Downstream LDM7 is locked
 * @param[in] down7        Downstream LDM7.
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @post                   Downstream LDM7 is locked
 */
static Ldm7Status
stopMcastRcvr(Down7* const down7)
{
    down7_assertLocked(down7);
    log_debug_1("Entered");
    int status;
    if (down7->mlr == NULL) {
        status = 0;
    }
    else {
        log_debug_1("Stopping multicast receiver");
        mlr_stop(down7->mlr);
        down7_unlock(down7);
        status = pthread_join(down7->mcastRecvThread, NULL);
        down7_lock(down7);
        if (status) {
            log_add_errno(status, "Couldn't join multicast receiving thread");
            status = LDM7_SYSTEM;
        }
        else {
            mlr_free(down7->mlr);
            down7->mlr = NULL;
        }
    }
    return status;
}

/**
 * Starts the concurrent threads of a downstream LDM7 that collectively receive
 * data-products. Returns immediately.
 *
 * @pre                       Downstream LDM7 is locked
 * @param[in]  down7          Pointer to the downstream LDM7.
 * @retval     0              Success.
 * @retval     LDM7_SHUTDOWN  The LDM7 has been shut down. No task is running.
 * @retval     LDM7_SYSTEM    Error. `log_add()` called. No task is running.
 * @post                      Downstream LDM7 is locked
 */
static int
startRecvThreads(Down7* const down7)
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
 * Ensures that the threads of a downstream LDM7 that are receiving
 * data-products are stopped.
 *
 * Idempotent.
 *
 * @pre                  Downstream LDM7 is locked
 * @param[in,out] down7  Downstream LDM7
 * @post                 Downstream LDM7 is locked
 */
static void
ensureRecvThreadsStopped(Down7* const down7)
{
    down7_assertLocked(down7);
    stopMcastRcvr(down7);
    stopMissedProdRequester(down7);
    stopUcastRcvr(down7);
}

/**
 * Frees the resources of the subscription client.
 * @param[in] arg  Downstream LDM7
 */
static void
freeSubscriptionClient(
        void* arg)
{
    Down7* const   down7 = (Down7*)arg;
    up7proxy_free(down7->up7proxy); // won't close externally-created socket
    down7->up7proxy = NULL;
    (void)close(down7->sock);
    down7->sock = -1;
}

/**
 * Executes a downstream LDM7. Doesn't return until an error occurs or
 * `down7stop()` is called. Sets `down7->status` to the returned value.
 * @pre                      Downstream LDM7 is locked
 * @pre                      `SIGTERM` is blocked for the current thread
 * @param[in] down7          Pointer to the downstream LDM7 to be executed.
 * @retval    LDM7_INTR      Signal caught
 * @retval    LDM7_INVAL     Invalid port number or host identifier. `log_add()`
 *                           called.
 * @retval    LDM7_NOENT     No such feed. `log_add()` called.
 * @retval    LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval    LDM7_REFUSED   Remote host refused connection (server likely isn't
 *                           running). `log_add()` called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). `log_add()`
 *                           called.
 * @retval    LDM7_SHUTDOWN  `down7_stop()` was called
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. `log_add()` called.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 *                           `log_add()` called.
 * @post                     Downstream LDM7 is locked
 * @post                     `SIGTERM` is blocked for the current thread
 */
static Ldm7Status
runDown7Once(Down7* const down7)
{
    down7_makeStoppable(down7);
    int status = newSubscriptionClient(down7); // sets `down7->{up7proxy,sock}`
    if (status) {
        char* feedSpec = feedtypet_format(down7->feedtype);
        log_add("Couldn't create client for subscribing to feed %s from %s",
                down7->feedId, down7->upId);
        free(feedSpec);
        down7_makeUnstoppable(down7);
    }
    else {
        // Blocks until error, reply, timeout, or `down7_stop()`
        status = up7proxy_subscribe(down7->up7proxy, down7->feedtype,
                &down7->vcEnd, &down7->mcastInfo);
        down7_makeUnstoppable(down7);
        if (status) {
            log_add("Couldn't subscribe to feed %s from %s", down7->feedId,
                    down7->upId);
        }
        else {
            status = startRecvThreads(down7);
            if (status) {
                log_add("Error starting data-product reception threads to "
                        "receive feed %s from %s", down7->feedId,
                        down7->upId);
            }
            else {
                // Temporarily unblocks `down7`
                status = down7_waitForStatusChange(down7);
                down7_assertLocked(down7);
                ensureRecvThreadsStopped(down7);
            } // Product reception threads started
            mi_free(down7->mcastInfo); // NULL safe
            down7->mcastInfo = NULL;
        } // `down7->mcastInfo` allocated
        freeSubscriptionClient(down7);
    } // Subscription client allocated
    return down7->status = status;
}

/**
 * Waits a short time. Doesn't return until the time period is up or the status
 * of the downstream LDM7 changes.
 *
 * @pre                      Downstream LDM7 is locked
 * @param[in] down7          Pointer to the downstream LDM7.
 * @return                   `down7->status`
 * @post                     Downstream LDM7 is locked
 */
static Ldm7Status
down7_nap(Down7* const down7)
{
    down7_assertLocked(down7);
    log_debug_1("Napping");
    struct timespec duration;
    duration.tv_sec = time(NULL) + 60; // a time in the future
    duration.tv_nsec = 0;
    down7_timedWaitForStatusChange(down7, &duration);
    return down7->status;
}

/**
 * Processes a data-product from a remote LDM7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM7.
 *
 * @param[in] down7        The downstream LDM7.
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
            log_info_q("Inserted: %s", buf);
        }
        down7_incNumProds(down7);
    }
    else {
        if (status == EINVAL) {
            log_error_q("Invalid argument");
            status = LDM7_SYSTEM;
        }
        else {
            char buf[LDM_INFO_MAX];

            (void)s_prod_info(buf, sizeof(buf), &prod->info,
                    log_is_enabled_debug);

            if (status == PQUEUE_DUP) {
                log_info_q("Duplicate data-product: %s", buf);
            }
            else {
                log_warning_q("Product too big for queue: %s", buf);
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
        log_errno_q(status, "Couldn't create thread-specific data-key");
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

    log_error_q("%s: %s", msg, s_prod_info(buf, sizeof(buf), info,
            log_is_enabled_debug));
    (void)svcerr_systemerr(rqstp->rq_xprt);
    svc_destroy(rqstp->rq_xprt);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new downstream LDM7. The instance doesn't receive anything until
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
 * @return                Pointer to the new downstream LDM7.
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
    Down7* const down7 = log_malloc(sizeof(Down7), "downstream LDM7");
    int          status;

    if (down7 == NULL)
        goto return_NULL;

    /*
     * `PQ_THREADSAFE` because the queue is accessed by this module on 3
     * threads: FMTP multicast receiver, FMTP unicast receiver, and LDM7
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
        log_errno_q(status, "Couldn't initialize condition-variable");
        goto free_servAddr;
    }

    {
        pthread_mutexattr_t mutexAttr;

        status = pthread_mutexattr_init(&mutexAttr);
        if (status) {
            log_errno_q(status, "Couldn't initialize attributes of state-mutex");
        }
        else {
            (void)pthread_mutexattr_setprotocol(&mutexAttr,
                    PTHREAD_PRIO_INHERIT);
            (void)pthread_mutexattr_settype(&mutexAttr,
                    PTHREAD_MUTEX_ERRORCHECK );

            if ((status = pthread_mutex_init(&down7->mutex, &mutexAttr))) {
                log_errno_q(status, "Couldn't initialize mutex");
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

    down7->upId = sa_format(servAddr);
    if (down7->upId == NULL) {
        log_add("Couldn't format socket address of upstream LDM7");
        goto free_vcEnd;
    }

    status = pthread_mutex_init(&down7->numProdMutex, NULL);
    if (status) {
        log_syserr_q("Couldn't initialize number-of-products mutex from %s",
                down7->upId);
        goto free_upId;
    }

    down7->feedId = feedtypet_format(feedtype);
    if (down7->feedId == NULL) {
        log_add("Couldn't format desired feed specification");
        goto free_numProdMutex;
    }

    if ((status = pthread_once(&down7KeyControl, createDown7Key)) != 0) {
        log_add("Couldn't create thread-key");
        goto free_feedId;
    }

    down7->pq = down7Pq;
    (void)memset(down7->firstMcast, 0, sizeof(signaturet));
    (void)memset(down7->prevLastMcast, 0, sizeof(signaturet));
    down7->feedtype = feedtype;
    down7->up7proxy = NULL;
    down7->sock = -1;
    down7->mcastInfo = NULL;
    down7->mlr = NULL;
    down7->mcastWorking = false;
    down7->numProds = 0;
    down7->haveUcastRecvThread = false;
    down7->haveMainThread = false;
    down7->status = LDM7_OK;
    down7->done = false;

    return down7;

free_feedId:
    free(down7->feedId);
free_numProdMutex:
    (void)pthread_mutex_destroy(&down7->numProdMutex);
free_upId:
    free(down7->upId);
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
 * Returns the product-queue associated with a downstream LDM7.
 *
 * @param[in] down7  The downstream LDM7.
 * @return           The associated product-queue.
 */
pqueue* down7_getPq(
        Down7* const down7)
{
    return down7->pq;
}

/**
 * Executes a downstream LDM7. Doesn't return until `down7_stop()` is called
 * or an error occurs.
 *
 * @param[in,out] down7          downstream LDM7
 * @retval        0              `down7_stop()` was called. `log_add()` called.
 * @retval        LDM7_INTR      Signal caught. `log_add()` called.
 * @retval        LDM7_INVAL     Invalid port number or host identifier.
 *                               `log_add()` called.
 * @retval        LDM7_LOGIC     No prior call to `down7_stop()`. `log_add()`
 *                               called.
 * @retval        LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval        LDM7_OK        `down7_stop()` was called. No problems
 *                               occurred.
 * @retval        LDM7_RPC       RPC failure (including interrupt). `log_add()`
 *                               called.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 * @see           `down7_stop()`
 */
Ldm7Status
down7_start(Down7* const down7)
{
    /*
     * NB: This module uses `SIGTERM` to terminate some threads; consequently,
     * the code implicitly assumes that `SIGTERM` is blocked except at points
     * where termination is anticipated.
     */
    bool wasBlocked = down7_makeUnstoppable(down7);
    int  status;
    if (down7->haveMainThread) {
        down7_unlock(down7);
        log_add("Main thread is already running");
        status = LDM7_LOGIC;
    }
    else {
        status = 0;
        down7->mainThread = pthread_self();
        down7->haveMainThread = true;
        log_notice_q("Downstream LDM7 starting up: remoteLDM7=%s, feed=%s, "
                "pq=\"%s\"", down7->upId, s_feedtypet(down7->feedtype),
                pq_getPathname(down7->pq));
        while (!down7->done) {
            status = runDown7Once(down7);
            down7_assertUnstoppable(down7);
            switch (status) {
            case LDM7_INTR:
                log_add("Signal caught");
                down7->done = true;
                break;
            case LDM7_OK:
            case LDM7_SHUTDOWN:
                log_add("Shutdown requested");
                down7->done = true;
                break;
            case LDM7_TIMEDOUT:
                log_flush_warning();
                down7->status = LDM7_OK; // Try again
                break;
            case LDM7_REFUSED:
            case LDM7_NOENT:
            case LDM7_UNAUTH:
                // Possibly temporary problem
                log_flush_warning();
                down7->status = LDM7_OK; // Try again
                down7_nap(down7); // `down7_stop()` => return
                break;
            case LDM7_INVAL:
            case LDM7_MCAST:
            case LDM7_RPC:
            case LDM7_SYSTEM:
            default:
                // Fatal problem
                log_add("Execution error");
                down7->done = true;
                break;
            } // `status` switch
        } // While not done execution-loop
        down7->haveMainThread = false;
    } // Main thread wasn't running
    setSigTerm(wasBlocked);
    down7_unlock(down7);
    return status == LDM7_SHUTDOWN ? 0 : status;
}

/**
 * Stops a downstream LDM7. Causes `down7_start()` to return if it hasn't
 * already. Returns immediately.
 *
 * @param[in] down7          The running downstream LDM7 to be stopped.
 * @retval    0              Success. `down7_start()` should return.
 * @retval    LDM7_LOGIC     No prior call to `down7_start()`. `log_add()`
 *                           called.
 * @retval    LDM7_SYSTEM    The downstream LDM7 couldn't be stopped due to a
 *                           system error. `log_flush()` called.
 */
Ldm7Status
down7_stop(Down7* const down7)
{
    int status;
    down7_lock(down7);
    if (!down7->haveMainThread) {
        down7_unlock(down7);
        log_add("Main thread isn't running");
        status = LDM7_LOGIC;
    }
    else {
        down7->done = true;
        // Not `pthread_cond_broadcast()` because only the main-thread waits
        pthread_cond_signal(&down7->cond);
        /*
         * WARNING: If a standard RPC implementation is used, then it is likely
         * that the following statement won't stop the main thread if it is in
         * `subscribe_7()`. See the call of that function for more information.
         * -- Steve Emmerson 2018-03-26
         */
        status = pthread_kill(down7->mainThread, SIGTERM);
        if (status) {
            log_add_errno(status, "Couldn't signal main thread");
            status = LDM7_SYSTEM;
        }
        else {
            down7->haveMainThread = false;
        }
        down7_unlock(down7);
    } // Main thread is active
    return status;
}

/**
 * Increments the number of data-products successfully inserted into the
 * product-queue of a downstream LDM7.
 *
 * @param[in] down7  The downstream LDM7.
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
 * queue of a downstream LDM7.
 *
 * @param[in] down7  The downstream LDM7.
 * @return           The number of successfully inserted data-products.
 */
uint64_t
down7_getNumProds(
        Down7* const down7)
{
    int status = pthread_mutex_lock(&down7->numProdMutex);
    log_assert(status == 0);
    uint64_t num = down7->numProds;
    status = pthread_mutex_unlock(&down7->numProdMutex);
    log_assert(status == 0);
    return num;
}

/**
 * Returns the number of reserved spaces in the product-queue for which
 * pqe_insert() or pqe_discard() have not been called.
 *
 * @param[in] down7  The downstream LDM7.
 */
long
down7_getPqeCount(
        Down7* const down7)
{
    return pqe_get_count(down7->pq);
}

/**
 * Frees the resources of a downstream LDM7 returned by `down7_new()` that
 * either wasn't started or has been stopped.
 *
 * @pre                    The downstream LDM7 was returned by `down7_new()`
 *                         and either `down7_start()` has not been called on it
 *                         or `down7_stop()` has been called on it.
 * @pre                    Downstream LDM7 is unlocked
 * @param[in] down7        Pointer to the downstream LDM7 to be freed or NULL.
 * @retval    0            Success.
 * @retval    LDM7_LOGIC   `down7_start()` has been called but no subsequent
 *                         `down7_stop()`. `log_add()` called.
 * @retval    LDM7_LOGIC   A mutex is locked. `log_add()` called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
int
down7_free(Down7* const down7)
{
    int status = 0;
    if (down7) {
        down7_lock(down7);
        if (down7->haveMainThread) {
            down7_unlock(down7);
            log_add("Main thread is running!");
            status = LDM7_LOGIC;
        }
        else {
            down7_unlock(down7);
            free(down7->feedId);
            status = pthread_mutex_destroy(&down7->numProdMutex);
            if (status) {
                log_add_errno(status,
                        "Couldn't destroy number-of-products mutex");
                status = LDM7_LOGIC;
            }
            free(down7->upId);
            vcEndPoint_destroy(&down7->vcEnd);
            free(down7->iface);
            log_debug_1("Closing multicast receiver memory");
            status = pthread_mutex_destroy(&down7->mutex);
            if (status) {
                log_add_errno(status, "Couldn't destroy LDM7 mutex");
                status = LDM7_LOGIC;
            }
            status = pthread_cond_destroy(&down7->cond);
            if (status) {
                log_add_errno(status, "Couldn't destroy condition-variable");
                status = LDM7_LOGIC;
            }
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
 * @param[in] down7   Pointer to the downstream LDM7.
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
    log_debug_1("Entered: iProd=%lu", (unsigned long)iProd);
    (void)mrm_addMissedFile(down7->mrm, iProd);
}

/**
 * Tracks the last data-product to be successfully received by the multicast
 * LDM receiver associated with a downstream LDM7. This function is called by
 * the multicast LDM receiver; therefore, it must return immediately so that the
 * multicast LDM receiver can continue.
 *
 * The first time this function is called for a given downstream LDM7, it
 * starts a detached thread that requests the backlog of data-products that
 * were missed due to the passage of time from the end of the previous session
 * to the reception of the first multicast data-product.
 *
 * @param[in] down7  Pointer to the downstream LDM7.
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
            log_errno_q(status, "Couldn't start backlog-requesting task");
        }
        else {
            pthread_detach(thread);
        }
    }
}

/**
 * Processes a missed data-product from a remote LDM7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM7 because it was missed by the
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
 * Accepts notification from the upstream LDM7 that a requested data-product
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
        log_add("Product %lu is unexpected", (unsigned long)*missingIprod);
    }
    else {
        // The queue can't be empty
        (void)mrm_removeRequestedFileNoWait(down7->mrm, &iProd);

        log_warning_q("Requested product %lu doesn't exist",
                (unsigned long)*missingIprod);
    }

    return NULL ; /* don't reply */
}

/**
 * Processes a backlog data-product from a remote LDM7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM7 because it was missed during the
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
 * Accepts notification that the downstream LDM7 associated with the current
 * thread has received all backlog data-products from its upstream LDM7. From
 * now on, the current process may be terminated for a time period that is less
 * than the minimum residence time of the upstream LDM7's product-queue without
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

    log_notice_q("All backlog data-products received: feedtype=%s, server=%s",
            s_feedtypet(down7->feedtype),
            sa_snprint(down7->servAddr, saStr, sizeof(saStr)));

    return NULL; // causes RPC dispatcher to not reply
}
