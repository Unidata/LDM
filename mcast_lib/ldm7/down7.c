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
#include "StopFlag.h"
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
#include "Task.h"
#include "timestamp.h"
#include "VirtualCircuit.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sys/select.h>
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

/******************************************************************************
 * Requester of Data-Product Missed by the FMTP Layer:
 ******************************************************************************/

typedef struct backstop {
    McastReceiverMemory*  mrm;       ///< Persistent multicast receiver memory
    Up7Proxy*             up7Proxy;  ///< Proxy for upstream LDM7
    struct downlet*       downlet;   ///< Parent one-time downstream LDM7
    signaturet            prevLastMcast;    ///< Previous session's Last prod
    bool                  prevLastMcastSet; ///< `prevLastMcast` set?
} Backstop;

typedef struct ucastRcvr {
    SVCXPRT*              xprt;
    char*                 remoteStr;
    struct downlet*       downlet;
    StopFlag              stopFlag;
} UcastRcvr;

typedef struct mcastRcvr {
    Mlr*            mlr;     ///< Multicast LDM receiver
    struct downlet* downlet; ///< Parent one-time downstream LDM7
} McastRcvr;

typedef struct backlogger {
    /// Signature of first product received via multicast
    signaturet            before;
    struct downlet*       downlet; ///< Parent one-time downstream LDM7
    StopFlag              stopFlag;
} Backlogger;

typedef struct downlet {
    struct down7*        down7;      ///< Parent downstream LDM7
    Up7Proxy*            up7proxy;   ///< Proxy for upstream LDM7
    Backstop*            backstop;   ///< Requests products FMTP layer missed
    UcastRcvr*           ucastRcvr;  ///< Receiver of unicast products
    Backlogger*          backlogger; ///< Requests backlog of products
    Task*                backstopTask;
    Task*                ucastRcvrTask;
    Task*                backloggerTask;
    Task*                mcastRcvrTask;
    Up7Proxy*            up7Proxy;   ///< Proxy for upstream LDM7
    pqueue*              pq;         ///< pointer to the product-queue
    const ServiceAddr*   servAddr;   ///< socket address of remote LDM7
    McastInfo*           mcastInfo;  ///< information on multicast group
    /**
     * IP address of interface to use for receiving multicast and unicast
     * packets
     */
    const char*          iface;
    McastRcvr*           mcastRcvr;
    char*                upId;          ///< ID of upstream LDM7
    char*                feedId;        ///< Desired feed specification
    /// Server-side transport for missing products
    SVCXPRT*             xprt;
    ///< Persistent multicast receiver memory
    McastReceiverMemory* mrm;
    const VcEndPoint*    vcEnd;         ///< Local virtual-circuit endpoint
    pthread_t            thread;
    pthread_mutex_t      mutex;         ///< Mutex for state changes
    pthread_cond_t       cond;          ///< Condition-variable for status
    feedtypet            feedtype;      ///< Feed of multicast group
    Ldm7Status           status;        ///< Downstream LDM7 status
    int                  sock;          ///< Socket with remote LDM7
    volatile bool        mcastWorking;  ///< Product received via multicast?
} Downlet;

/**
 * The data structure of a downstream LDM7:
 */
struct down7 {
    pqueue*               pq;            ///< pointer to the product-queue
    ServiceAddr*          servAddr;      ///< socket address of remote LDM7
    McastInfo*            mcastInfo;     ///< information on multicast group
    /**
     * IP address of interface to use for receiving multicast and unicast
     * packets
     */
    char*                 iface;
    /**
     * Signature of the first data-product received by the associated multicast
     * LDM receiver during the current session.
     */
    signaturet            firstMcast;
    ///< Persistent multicast receiver memory
    McastReceiverMemory*  mrm;
    /**
     * Signature of the last data-product received by the associated multicast
     * LDM receiver during the previous session.
     */
    signaturet            prevLastMcast;
    pthread_mutex_t       mutex;            ///< Mutex for state changes
    pthread_cond_t        cond;             ///< Condition-variable for status
    pthread_mutex_t       numProdMutex;     ///< Mutex for number of products
    uint64_t              numProds;         ///< Number of inserted products
    feedtypet             feedtype;         ///< Feed of multicast group
    VcEndPoint            vcEnd;            ///< Local virtual-circuit endpoint
    Ldm7Status            status;           ///< Downstream LDM7 status
    volatile bool         mcastWorking;     ///< Product received via multicast?
    bool                  prevLastMcastSet; ///< `prevLastMcast` set?
    StopFlag              stopFlag;
};

// Type for obtaining integer status from `pthread_create()` start-function.
typedef union {
    void* ptr;
    int   val;
} PtrInt;

static Ldm7Status
downlet_testConnection(Downlet* const downlet);

/**
 * Initializes a mutex.
 * @param[out] mutex        Mutex to be initialized
 * @retval     0            Success
 * @retval     LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
mutex_init(pthread_mutex_t* const mutex)
{
    pthread_mutexattr_t mutexAttr;
    int                 status = pthread_mutexattr_init(&mutexAttr);

    if (status) {
        log_add_errno(status, "Couldn't initialize attributes of mutex");
        status = LDM7_SYSTEM;
    }
    else {
        (void)pthread_mutexattr_setprotocol(&mutexAttr, PTHREAD_PRIO_INHERIT);
        (void)pthread_mutexattr_settype(&mutexAttr, PTHREAD_MUTEX_ERRORCHECK );

        status = pthread_mutex_init(mutex, &mutexAttr);

        if (status) {
            log_add_errno(status, "Couldn't initialize mutex");
            status = LDM7_SYSTEM;
        }

        (void)pthread_mutexattr_destroy(&mutexAttr);
    } // `mutexAttr` initialized

    return status;
}

inline static void
mutex_deinit(pthread_mutex_t* const mutex)
{
    int status = pthread_mutex_destroy(mutex);
    log_assert(status == 0);
}


static void
signalCond(pthread_cond_t* const cond)
{
    int status = pthread_cond_signal(cond);
    log_assert(status == 0);
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
        log_abort("Mutex was unlocked");
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
static void up7proxy_delete(
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
 *                            caller should call `mi_delete(*mcastInfo)` when
 *                            it's no longer needed.
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

    /*
     * WARNING: If a standard RPC implementation is used, then it is likely that
     * `subscribe_7()` won't return when `SIGTERM` is received because
     * `readtcp()` in `clnt_tcp.c` nominally ignores `EINTR`. The RPC
     * implementation included with the LDM package has been modified to not
     * have this problem. -- Steve Emmerson 2018-03-26
     */
    SubscriptionReply* reply = subscribe_7(&request, clnt);

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
up7proxy_requestBacklog(
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
    (void)request_product_7((FmtpProdIndex*)&iProd, clnt); // Safe cast

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
 * to it. Doesn't block.
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

/******************************************************************************
 * One-time downstream LDM7 callback functions
 ******************************************************************************/

inline static void
downlet_assertLocked(Downlet* const downlet)
{
    assertLocked(&downlet->mutex);
}

inline static void
downlet_assertUnlocked(Downlet* const downlet)
{
    assertUnlocked(&downlet->mutex);
}

/**
 * Locks the state of a one-time downstream LDM7.
 *
 * @pre              One-time downstream LDM7 is unlocked
 * @param[in] down7  One-time downstream LDM7 to have its state locked
 * @post             One-time downstream LDM7 is locked
 */
static void
downlet_lock(Downlet* const downlet)
{
    log_debug_1("Locking state");
    int status = pthread_mutex_lock(&downlet->mutex);
    log_assert(status == 0);
}

/**
 * Unlocks the state of a one-time downstream LDM7.
 *
 * @param[in] down7  One-time downstream LDM7 to have its state unlocked.
 */
static void
downlet_unlock(Downlet* const downlet)
{
    log_debug_1("Unlocking state");
    int status = pthread_mutex_unlock(&downlet->mutex);
    log_assert(status == 0);
}

/**
 * Compares and sets the status of a one-time downstream LDM7.
 * @param[in,out] downlet    one-time downstream LDM7
 * @param[in]     expect     Expected status
 * @param[in]     newStatus  New status iff `downlet->status == expect`
 * @return                   Current status
 */
static Ldm7Status
downlet_casStatus(
        Downlet* const   downlet,
        const Ldm7Status expect,
        const Ldm7Status newStatus)
{
    downlet_assertLocked(downlet);

    if (downlet->status == expect) {
        downlet->status = newStatus;

        // Not `pthread_cond_broadcast()` because only the main-thread waits
        signalCond(&downlet->cond);
    }

    return downlet->status;
}

/**
 * Waits for a change in the status of a one-time downstream LDM7.
 * @param[in] down7  One-time downstream LDM7
 * @return           Status of one-time downstream LDM7
 */
static Ldm7Status
downlet_waitForStatusChange(Downlet* const downlet)
{
    downlet_lock(downlet);
        while (downlet->status == LDM7_OK) {
            int status = pthread_cond_wait(&downlet->cond, &downlet->mutex);
            log_assert(status == 0);
        }
    downlet_unlock(downlet);

    return downlet->status;
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
downlet_setStatusIfOk(
        Downlet* const     downlet,
        const Ldm7Status newStatus)
{
    downlet_lock(downlet);
        int status = downlet_casStatus(downlet, LDM7_OK, newStatus);
    downlet_unlock(downlet);

    return status;
}

/**
 * Called by `backlogger_run()`.
 *
 * @param[in] downlet   One-time downstream LDM7
 * @param[in] before    Signature of first product received via multicast
 * @retval    0         Success
 * @retval    LDM7_RPC  Error in RPC layer. `log_add()` called.
 */
static Ldm7Status
downlet_requestBacklog(
        Downlet* const   downlet,
        const signaturet before)
{
    BacklogSpec  spec;

    spec.afterIsSet = mrm_getLastMcastProd(downlet->mrm, spec.after);
    if (!spec.afterIsSet)
        (void)memset(spec.after, 0, sizeof(signaturet));
    (void)memcpy(spec.before, before, sizeof(signaturet));
    spec.timeOffset = getTimeOffset();

    int status = up7proxy_requestBacklog(downlet->up7proxy, &spec);

    if (status)
        log_add("Couldn't request session backlog");

    return status;
}

/******************************************************************************
 * Requester of the backlog of data products
 ******************************************************************************/

/**
 *
 * @param[out] backlogger  Backlog requester
 * @param[in]  before      Signature of first product received via multicast
 * @param[in]  downlet     One-time downstream LDM7. Must exist until
 *                         `backlogger_destroy()` is called.
 */
static void
backlogger_init(
        Backlogger* const backlogger,
        const signaturet  before,
        Downlet* const    downlet)
{
    backlogger->downlet = downlet;

    int status = stopFlag_init(&backlogger->stopFlag);
    log_assert(status == 0);

    (void)memcpy(backlogger->before, before, sizeof(signaturet));
}

static Backlogger*
backlogger_new(
        const signaturet before,
        Downlet* const   downlet)
{
    Backlogger* backlogger = log_malloc(sizeof(Backlogger),
            "missed-product backlog requester");

    if (backlogger)
        backlogger_init(backlogger, before, downlet);

    return backlogger;
}

inline static void
backlogger_delete(Backlogger* const backlogger)
{
    free(backlogger);
}

static int
backlogger_run(Backlogger* const backlogger)
{
    int status;

    if (stopFlag_isSet(&backlogger->stopFlag)) {
        status = 0;
    }
    else {
        status = downlet_requestBacklog(backlogger->downlet,
                backlogger->before);
    }

    return status;
}

static void
backlogger_halt(
        Backlogger* const backlogger,
        const pthread_t   thread)
{
    stopFlag_set(&backlogger->stopFlag);
    pthread_kill(thread, SIGTERM);
}

/******************************************************************************
 * Requester of data-products missed by the FMTP layer
 ******************************************************************************/

/**
 * Initializes a one-time LDM7 missed-product backstop.
 * @param[out]    backstop     Requester of missed products
 * @param[in]     up7Proxy     Proxy for upstream LDM7
 * @param[in]     mrm          Multicast receiver memory. Must exist until
 *                             `backstop_deinit()` returns.
 * @param[in,out] downlet      Parent one-time downstream LDM7
 * @retval        0            Success
 * @see `backstop_deinit()`
 */
static void
backstop_init(
        Backstop* const restrict            backstop,
        Up7Proxy* const restrict            up7Proxy,
        McastReceiverMemory* const restrict mrm,
        Downlet* const restrict             downlet)
{
    backstop->mrm = mrm;
    backstop->prevLastMcastSet = mrm_getLastMcastProd(backstop->mrm,
            backstop->prevLastMcast);
    backstop->up7Proxy = up7Proxy;
    backstop->downlet = downlet;
}

static Backstop*
backstop_new(
        Up7Proxy* const restrict            up7Proxy,
        McastReceiverMemory* const restrict mrm,
        Downlet* const restrict             downlet)
{
    Backstop* backstop = log_malloc(sizeof(Backstop),
            "one-time LDM7 missed-product backstop");

    if (backstop != NULL)
        backstop_init(backstop, up7Proxy, mrm, downlet);

    return backstop;
}

static void
backstop_delete(Backstop* const backstop)
{
    free(backstop);
}

/**
 * Requests data-products that were missed by the multicast LDM receiver.
 * Entries from the missed-but-not-requested queue are removed and converted
 * into requests for missed data-products, which are asynchronously sent to the
 * remote LDM7. Doesn't return until `backstop_stop()` is called or an
 * unrecoverable error occurs.
 *
 * Called by `pthread_create()`.
 *
 * Attempts to set the downstream LDM7 status on completion.
 *
 * @param[in] arg            Requester of data-products missed by FMTP layer
 * @retval    LDM7_SHUTDOWN  `backstop_stop()` was called
 * @retval    LDM7_RPC       Error in RPC layer. `log_add()` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 * @see `stopMissedProdRequester()`
 */
static int
backstop_run(Backstop* const backstop)
{
    int status;

    for (;;) {
        /*
         * The semantics and order of the following actions are necessary to
         * preserve the meaning of the two queues and to ensure that all missed
         * data-products are received following a restart.
         */
        FmtpProdIndex iProd;

        if (!mrm_peekMissedFileWait(backstop->mrm, &iProd)) {
            log_debug_1("The queue of missed data-products has been shutdown");
            status = 0;
            break;
        }
        else {
            if (!mrm_addRequestedFile(backstop->mrm, iProd)) {
                log_add("Couldn't add FMTP product-index to requested-queue");
                status = LDM7_SYSTEM;
                break;
            }
            else {
                /* The queue can't be empty */
                (void)mrm_removeMissedFileNoWait(backstop->mrm, &iProd);
                status = up7proxy_requestProduct(backstop->up7Proxy, iProd);

                if (status) {
                    log_add("Couldn't request product");
                    break;
                }
            } // product-index added to requested-but-not-received queue
        } // have peeked-at product-index from missed-but-not-requested queue
    }

    return status;
}

static void
backstop_halt(Backstop* const backstop)
{
    mrm_shutDownMissedFiles(backstop->mrm);
}

/******************************************************************************
 * LDM7 receiver of unicast data-products.
 ******************************************************************************/

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
ucastRcvr_createXprt(
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
 * Runs the RPC-based server of a downstream LDM7. *Might* destroy and
 * unregister the service transport. Doesn't return until `ucastRcvr_stop()` is
 * called or an error occurs
 *
 * @param[in]     ucastRcvr      Unicast receiver
 * @retval        0              `stopUcastRcvr()` was called.
 * @retval        LDM7_RPC       Error in RPC layer. `log_add()` called.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 */
static int
ucastRcvr_runSvc(UcastRcvr* const ucastRcvr)
{
    int           status = 0;
    const int     sock = ucastRcvr->xprt->xp_sock;
    struct pollfd pfd;
    const int     timeout = interval * 1000; // Probably 30 seconds

    pfd.fd = sock;
    pfd.events = POLLIN;

    log_info_1("Starting unicast receiver: sock=%d, timeout=%d ms", sock,
            timeout);

    while (!stopFlag_isSet(&ucastRcvr->stopFlag) && status == 0) {
        log_debug_1("Calling poll(): socket=%d", sock);
        status = poll(&pfd, 1, timeout);

        if (0 == status) {
            // Timeout
            if (!stopFlag_isSet(&ucastRcvr->stopFlag)) {
                status = downlet_testConnection(ucastRcvr->downlet);
                if (status)
                    break;
            }
            continue;
        }

        if (status < 0) {
            if (errno == EINTR) {
                status = LDM7_INTR;
            }
            else {
                log_add_syserr("poll() failure on socket %d with upstream LDM7, "
                        "%s", sock, ucastRcvr->remoteStr);
                status = LDM7_SYSTEM;
            }
            break;
        }

        if (!(pfd.revents & POLLRDNORM)) {
            log_add_syserr("Error on socket %d with upstream LDM7, %s", sock,
                    ucastRcvr->remoteStr);
            status = LDM7_SYSTEM;
            break;
        }

        // Processes RPC message. Calls ldmprog_7(). Will call
        // `svc_destroy(ucastRcvr->xprt)` on error.
        svc_getreqsock(sock);

        if (!FD_ISSET(sock, &svc_fdset)) {
            // `svc_getreqsock()` destroyed `ucastRcvr->xprt`
            log_add("Connection with upstream LDM7, %s, closed by RPC layer",
                    ucastRcvr->remoteStr);
            ucastRcvr->xprt = NULL; // To inform others
            status = LDM7_RPC;
            break;
        }
    } // `poll()` loop

    return status; // Eclipse IDE wants to see a return
}

/**
 * Receives unicast data-products from the associated upstream LDM7 -- either
 * because they were missed by the multicast LDM receiver or because they are
 * part of the backlog. Doesn't return until `ucastRcvr_halt()` is called
 * or an error occurs. On return
 *   - `down7_setStatusIfOk()` will have been called; and
 *   - The TCP socket will be closed.
 *
 * @param[in] arg          One-time LDM7 unicast receiver
 * @retval    0            Success
 * @retval    LDM7_RPC     Error in RPC layer. `log_add()` called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `ucastRcvr_halt()`
 */
static int
ucastRcvr_run(UcastRcvr* const ucastRcvr)
{
    /*
     * The downstream LDM7 RPC service-functions don't know their associated
     * downstream LDM7; therefore, a thread-specific pointer to the downstream
     * LDM7 is set to provide context to those that need it.
     */
    int status = pthread_setspecific(down7Key, ucastRcvr->downlet);

    if (status) {
        log_add_errno(status,
                "Couldn't set thread-specific pointer to downstream LDM7");
        status = LDM7_SYSTEM;
    }
    else {
        /*
         * The following executes until an error occurs or termination is
         * externally requested. It *might* destroy the service transport --
         * which would unregister it.
         */
        status = ucastRcvr_runSvc(ucastRcvr);
    } // thread-specific pointer to downstream LDM7 is set

    return status;
}

static void
ucastRcvr_halt(
        UcastRcvr* const ucastRcvr,
        const pthread_t  thread)
{
    stopFlag_set(&ucastRcvr->stopFlag);
    pthread_kill(thread, SIGTERM);
}

static Ldm7Status
ucastRcvr_init(
        UcastRcvr* const restrict ucastRcvr,
        const int                 sock,
        Downlet* const restrict   downlet)
{
    SVCXPRT* xprt;
    int      status = ucastRcvr_createXprt(sock, &ucastRcvr->xprt);

    if (status) {
        log_add("Couldn't create server-side transport on socket %d", sock);
    }
    else {
        char* remoteStr = ipv4Sock_getPeerString(sock);

        // Last argument == 0 => don't register with portmapper
        if (!svc_register(xprt, LDMPROG, SEVEN, ldmprog_7, 0)) {
            log_add("Couldn't register one-time RPC server for receiving "
                    "data-products from \"%s\"",  remoteStr);
            status = LDM7_RPC;
        }
        else {
            ucastRcvr->xprt = xprt;
            ucastRcvr->remoteStr = remoteStr;
            ucastRcvr->downlet = downlet;

            status = stopFlag_init(&ucastRcvr->stopFlag);
            log_assert(status == 0);

            status = LDM7_OK;
        } // LDM7 service registered

        if (status)
            svc_destroy(xprt);
    } // `xprt` initialized

    return status;
}

static void
ucastRcvr_deinit(UcastRcvr* const ucastRcvr)
{
    svc_unregister(LDMPROG, SEVEN);

    free(ucastRcvr->remoteStr);

    if (ucastRcvr->xprt) {
        svc_destroy(ucastRcvr->xprt);
        ucastRcvr->xprt = NULL;
    }

    ucastRcvr->downlet = NULL;
}

/**
 * Allocates and initializes a new one-time LDM7 unicast receiver.
 * @param[in]     sock         Unicast socket with upstream LDM7
 * @param[in]     downlet      Parent one-time downstream LDM7
 * @retval        NULL         Failure. `log_add()` called.
 * @return                     Allocated and initialized one-time LDM7 unicast
 *                             receiver
 * @see          `ucastRcvr_delete()`
 */
static UcastRcvr*
ucastRcvr_new(
        const int                 sock,
        Downlet* const restrict   downlet)
{
    UcastRcvr* ucastRcvr = log_malloc(sizeof(UcastRcvr),
            "one-time LDM7 unicast receiver");

    if (ucastRcvr != NULL) {
        if (ucastRcvr_init(ucastRcvr, sock, downlet)) {
            log_add("Couldn't initialize one-time LDM7 unicast receiver");
            free(ucastRcvr);
            ucastRcvr = NULL;
        }
    }

    return ucastRcvr;
}

static void
ucastRcvr_delete(UcastRcvr* const ucastRcvr)
{
    ucastRcvr_deinit(ucastRcvr);
    free(ucastRcvr);
}

/******************************************************************************
 * Multicast receiver
 ******************************************************************************/

static int
mcastRcvr_init(
        McastRcvr* const restrict  mcastRcvr,
        McastInfo* const restrict  mcastInfo,
        const char* const restrict iface,
        pqueue* const restrict     pq,
        Downlet* const restrict    downlet)
{
    int  status;
    Mlr* mlr = mlr_new(mcastInfo, iface, pq, downlet);

    if (mlr == NULL) {
        log_add("Couldn't construct multicast LDM receiver");
        status = LDM7_SYSTEM;
    }
    else {
        mcastRcvr->mlr = mlr;
        mcastRcvr->downlet = downlet;
        status = 0;
    }

    return status;
}

inline static void
mcastRcvr_deinit(McastRcvr* const mcastRcvr)
{
    mlr_delete(mcastRcvr->mlr);
}

static McastRcvr*
mcastRcvr_new(
        McastInfo* const restrict  mcastInfo,
        const char* const restrict iface,
        pqueue* const restrict     pq,
        Downlet* const restrict    downlet)
{
    McastRcvr* mcastRcvr = log_malloc(sizeof(McastRcvr),
            "a one-time LDM7 multicast receiver");

    if (mcastRcvr) {
        if (mcastRcvr_init(mcastRcvr, mcastInfo, iface, pq, downlet)) {
            free(mcastRcvr);
            mcastRcvr = NULL;
        }
    }

    return mcastRcvr;
}

inline static void
mcastRcvr_delete(McastRcvr* const mcastRcvr)
{
    mcastRcvr_deinit(mcastRcvr);
    free(mcastRcvr);
}

/**
 * Receives data-products via multicast. Doesn't return until
 * an error occurs.
 *
 * Attempts to set the downstream LDM7 status on completion.
 *
 * @param[in] arg  LDM7 multicast receiver
 * @retval    0              Success
 * @retval    LDM7_INVAL     Invalid argument. `log_add()` called.
 * @retval    LDM7_MCAST     Multicast error. `log_add()` called.
 * @see `mcastRcvr_stop()`
 */
static int
mcastRcvr_run(McastRcvr* const mcastRcvr)
{
    log_debug_1("Entered");

    return mlr_run(mcastRcvr->mlr); // Blocks
}

inline static void
mcastRcvr_halt(McastRcvr* const mcastRcvr)
{
    log_debug_1("Stopping multicast receiver");
    mlr_halt(mcastRcvr->mlr);
}

/******************************************************************************
 * One-time downstream LDM7
 ******************************************************************************/

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
downlet_getSock(
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
downlet_getSocket(
    const ServiceAddr* const restrict       servAddr,
    int* const restrict                     sock,
    struct sockaddr_storage* const restrict sockAddr)
{
    struct sockaddr_storage addr;
    socklen_t               sockLen;
    int                     fd;
    int                     status = downlet_getSock(servAddr, AF_UNSPEC, &fd,
            &addr);

    if (status == LDM7_IPV6 || status == LDM7_REFUSED ||
            status == LDM7_TIMEDOUT) {
        log_clear();
        status = downlet_getSock(servAddr, AF_INET, &fd, &addr);
    }

    if (status == 0) {
        *sock = fd;
        *sockAddr = addr;
    }

    return status;
}

/**
 * Initializes the connection to an upstream LDM7.
 *
 * @param[in]  downlet        Pointer to one-time downstream LDM7.
 * @retval     0              Success. `downlet->up7proxy` and `downlet->sock`
 *                            are set.
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
downlet_initConn(Downlet* const downlet)
{
    int                     sock;
    struct sockaddr_storage sockAddr;
    int                     status = downlet_getSocket(downlet->servAddr, &sock,
            &sockAddr);

    if (status == LDM7_OK) {
        status = up7proxy_new(&downlet->up7proxy, sock,
                (struct sockaddr_in*)&sockAddr);
        if (status) {
            (void)close(sock);
        }
        else {
            downlet->sock = sock;
        }
    } // `sock` is open

    return status;
}

/**
 * Frees the resources of the subscription client.
 * @param[in] arg  Downstream LDM7
 */
static void
downlet_deinitConn(
        Downlet* downlet)
{
    up7proxy_delete(downlet->up7proxy); // won't close externally-created socket
    downlet->up7proxy = NULL;

    (void)close(downlet->sock);
    downlet->sock = -1;
}

/**
 * Initializes a one-time downstream LDM7.
 *
 * @param[out] downlet        One-time downstream LDM7 to be initialized
 * @param[in]  down7          Parent downstream LDM7. Must exist until
 *                            `downlet_deinit()` returns.
 * @param[in]  servAddr       Pointer to the address of the server from which to
 *                            obtain multicast information, backlog products,
 *                            and products missed by the FMTP layer. Must exist
 *                            until `downlet_deinit()` returns.
 * @param[in]  feed           Feed of multicast group to be received.
 * @param[in]  mcastIface     IP address of interface to use for receiving
 *                            multicast packets. Must exist until
 *                            `downlet_deinit()` returns.
 * @param[in]  vcEnd          Local virtual-circuit endpoint. Must exist until
 *                            `downlet_deinit()` returns.
 * @param[in]  pq             The product-queue. Must be thread-safe (i.e.,
 *                            `pq_getFlags(pq) | PQ_THREADSAFE` must be true).
 *                            Must exist until `downlet_deinit()` returns.
 * @param[in]  mrm            Multicast receiver session memory. Must exist
 *                            until `downlet_deinit()` returns.
 * @retval     0              Success
 * @retval     LDM7_INTR      Signal caught
 * @retval     LDM7_INVAL     Invalid port number or host identifier.
 *                            `log_add()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection (server likely
 *                            isn't running). `log_add()` called.
 * @retval     LDM7_RPC       RPC error. `log_add()` called.
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. `log_add()`
 *                            called.
 * @retval     LDM7_UNAUTH    Not authorized. `log_add()` called.
 * @see `downlet_deinit()`
 */
static Ldm7Status
downlet_init(
        Downlet* const restrict             downlet,
        Down7* const restrict               down7,
        const ServiceAddr* const restrict   servAddr,
        const feedtypet                     feed,
        const char* const restrict          mcastIface,
        const VcEndPoint* const restrict    vcEnd,
        pqueue* const restrict              pq,
        McastReceiverMemory* const restrict mrm)
{
    downlet->down7 = down7;
    downlet->servAddr = servAddr;
    downlet->iface = mcastIface;
    downlet->vcEnd = vcEnd;
    downlet->pq = pq;
    downlet->feedtype = feed;
    downlet->up7proxy = NULL;
    downlet->sock = -1;
    downlet->mcastInfo = NULL;
    downlet->mcastWorking = false;
    downlet->status = LDM7_OK;
    downlet->ucastRcvr = NULL;
    downlet->backlogger = NULL;
    downlet->mrm = mrm;
    downlet->backloggerTask = NULL;
    downlet->backstopTask = NULL;
    downlet->ucastRcvrTask = NULL;

    int status = pthread_cond_init(&downlet->cond, NULL);

    if (status) {
        log_add_errno(status, "Couldn't initialize condition-variable");
        status = LDM7_SYSTEM;
    }
    else {
        status = mutex_init(&downlet->mutex);

        if (status == 0) {
            downlet->upId = sa_format(servAddr);

            if (downlet->upId == NULL) {
                log_add("Couldn't format socket address of upstream LDM7");
                status = LDM7_SYSTEM;
            }
            else {
                downlet->feedId = feedtypet_format(feed);

                if (downlet->feedId == NULL) {
                    log_add("Couldn't format desired feed specification");
                    status = LDM7_SYSTEM;
                }
                else {
                    // Initializes `downlet->up7Proxy` and `downlet->sock`
                    status = downlet_initConn(downlet);

                    if (status) {
                        log_add("Couldn't establish connection to upstream "
                                "LDM7 server %s", downlet->upId);
                        free(downlet->feedId);
                    }
                } // `downlet->feedId` initialized

                if (status)
                    free(downlet->upId);
            } // `downlet->upId` initialized

            if (status)
                mutex_deinit(&downlet->mutex);
        } // `downlet->mutex` initialized

        if (status)
            (void)pthread_cond_destroy(&downlet->cond);
    } // `downlet->cond` initialized

    return status;
}

/**
 * @param[in,out] downlet      One-time downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
downlet_deinit(Downlet* const downlet)
{
    downlet_deinitConn(downlet);
    free(downlet->feedId);
    free(downlet->upId);

    mutex_deinit(&downlet->mutex);

    int status = pthread_cond_destroy(&downlet->cond);
    if (status) {
        log_add_errno(status, "Couldn't destroy condition-variable");
        status = LDM7_SYSTEM;
    }

    return status;
}

/**
 * Receives unicast data-products from the associated upstream LDM7 -- either
 * because they were missed by the multicast LDM receiver or because they are
 * part of the backlog. Doesn't return until `downlet_haltUcastRcvr()` is called
 * or an error occurs. On return
 *   - `down7_setStatusIfOk()` will have been called; and
 *   - The TCP socket will be closed.
 *
 * @param[in] arg   One-time LDM7 unicast receiver
 * @retval    NULL  Always
 * @see            `downlet_haltUcastRcvr()`
 */
static void*
downlet_runUcastRcvr(void* const arg)
{
    Downlet* const   downlet = (Downlet*)arg;
    int              status = ucastRcvr_run(downlet->ucastRcvr);

    (void)downlet_setStatusIfOk(downlet, status);

    // Because end of thread
    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);

    return NULL;
}

static int
downlet_haltUcastRcvr(
        void* const arg,
        pthread_t   thread)
{
    Downlet* const   downlet = (Downlet*)arg;
    UcastRcvr* const ucastRcvr = downlet->ucastRcvr;

    ucastRcvr_halt(ucastRcvr, thread);

    return 0;
}

/**
 * Creates an asynchronous subtask of a one-time downstream LDM7. Executes the
 * subtask on a separate thread.
 *
 * @param[in]     downlet      One-time downstream LDM7
 * @param[in]     id           Identifer of the subtask for log messages
 * @param[out]    task         One-time downstream LDM7 subtask
 * @param[in]     runFunc      Task run function
 * @param[in]     haltFunc     Task halt function
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `downlet_destroyTask()`
 */
static int
downlet_createTask(
        Downlet* const restrict    downlet,
        const char* const restrict id,
        Task** const restrict      task,
        void*                    (*runFunc)(void *),
        int                      (*haltFunc)(void*, pthread_t))
{
    int status;

    /*
     * '*task` must be assigned before `runFunc` is called because the latter
     * references the former
     */
    *task = NULL; // task_new(downlet, runFunc, haltFunc);

    if (*task == NULL) {
        log_add("Couldn't create asynchronous task for %s", id);
        status = LDM7_SYSTEM;
    }
    else {
        //status = task_start(*task);

        if (status) {
            log_add("Couldn't start task for %s", id);

            // task_delete(*task);
            *task = NULL;

            status = LDM7_SYSTEM;
        }
    } // `*task` constructed

    return status;
}

static int
downlet_destroyTask(
        Task* const restrict       task,
        const char* const restrict id)
{
    int status;

    if (task) {
        status = 0; // task_stop(task, NULL);

        if (status) {
            log_add("Couldn't stop task for %s");
        }
        else {
            // task_delete(task); // TODO
        }
    }

    return status;
}

/**
 * Creates a receiver of unicast data-products from the associated upstream
 * LDM7. Executes the receiver on a separate thread. The products were either
 * missed by the FMTP layer or they are part of the backlog. Blocks until the
 * receiver has started.
 * @param[in]     downlet      Parent one-time downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `downlet_destroyUcastRcvr()`
 */
static int
downlet_createUcastRcvr(
        Downlet* const downlet)
{
    static char id[] = "one-time LDM7 unicast receiver";
    int         status;

    downlet->ucastRcvr = ucastRcvr_new(downlet->sock, downlet);

    if (downlet->ucastRcvr == NULL) {
        log_add("Couldn't construct %s", id);
        status = LDM7_SYSTEM;
    }
    else {
        status = downlet_createTask(downlet, id, &downlet->ucastRcvrTask,
                downlet_runUcastRcvr, downlet_haltUcastRcvr);

        if (status) {
            ucastRcvr_delete(downlet->ucastRcvr);
            downlet->ucastRcvr = NULL;
        }
    } // `downlet->ucastRcvr` constructed

    return status;
}

static void
downlet_destroyUcastRcvr(
        Downlet* const downlet)
{
    if (downlet_destroyTask(downlet->ucastRcvrTask,
            "one-time LDM7 unicast receiver")) {
        log_flush_error();
    }
    else {
        downlet->ucastRcvrTask = NULL;

        ucastRcvr_delete(downlet->ucastRcvr);
        downlet->ucastRcvr = NULL;
    }
}

static void*
downlet_runBackstop(void* const arg)
{
    Downlet* const  downlet = (Downlet*)arg;
    Backstop* const backstop = downlet->backstop;
    int             status = backstop_run(backstop);

    (void)downlet_setStatusIfOk(downlet, status);

    // Because end of thread
    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);

    return NULL;
}

static int
downlet_haltBackstop(
        void* const     arg,
        const pthread_t thread)
{
    Downlet* const  downlet = (Downlet*)arg;
    Backstop* const backstop = downlet->backstop;

    backstop_halt(backstop);

    return 0;
}

static int
downlet_createBackstop(Downlet* const downlet)
{
    static char id[] = "one-time LDM7 backstop";
    int         status;

    downlet->backstop = backstop_new(downlet->up7Proxy, downlet->mrm, downlet);

    if (downlet->backstop == NULL) {
        log_add("Couldn't construct %s", id);
        status = LDM7_SYSTEM;
    }
    else {
        status = downlet_createTask(downlet, id, &downlet->backstopTask,
                downlet_runBackstop, downlet_haltBackstop);

        if (status) {
            backstop_delete(downlet->backstop);
            downlet->backstop = NULL;
        }
    } // `downlet->backstop` constructed

    return status;
}

static void
downlet_destroyBackstop(
        Downlet* const downlet)
{
    if (downlet_destroyTask(downlet->backstopTask,
            "one-time LDM7 backstop")) {
        log_flush_error();
    }
    else {
        downlet->backstopTask = NULL;

        backstop_delete(downlet->backstop);
        downlet->backstop = NULL;
    }
}


static void*
downlet_runMcastRcvr(void* const arg)
{
    Downlet* const   downlet = (Downlet*)arg;
    McastRcvr* const mcastRcvr = downlet->mcastRcvr;
    int status = mcastRcvr_run(mcastRcvr);

    (void)downlet_setStatusIfOk(downlet, status);

    // Because end of thread
    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);

    return NULL;
}

static int
downlet_haltMcastRcvr(
        void* const     arg,
        const pthread_t thread)
{
    Downlet* const   downlet = (Downlet*)arg;
    McastRcvr* const mcastRcvr = downlet->mcastRcvr;

    mcastRcvr_halt(mcastRcvr);

    return 0;
}

/**
 * Creates an asynchronous task that receives multicast data-products from the
 * associated upstream LDM7. Executes the receiver on a separate thread.
 *
 * @param[in]     downlet      Parent one-time downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `downlet_destroyMcastRcvr()`
 */
static int
downlet_createMcastRcvr(
        Downlet* const restrict downlet)
{
    static char id[] = "one-time LDM7 multicast receiver";
    int         status;

    downlet->mcastRcvr = mcastRcvr_new(downlet->mcastInfo, downlet->iface,
            downlet->pq, downlet);

    if (downlet->mcastRcvr == NULL) {
        log_add("Couldn't construct %s", id);
        status = LDM7_SYSTEM;
    }
    else {
        status = downlet_createTask(downlet, id, &downlet->mcastRcvrTask,
                downlet_runMcastRcvr, downlet_haltMcastRcvr);

        if (status) {
            mcastRcvr_delete(downlet->mcastRcvr);
            downlet->mcastRcvr = NULL;
        }
    } // `downlet->mcastRcvr` constructed

    return status;
}

static void
downlet_destroyMcastRcvr(
        Downlet* const downlet)
{
    if (downlet_destroyTask(downlet->mcastRcvrTask,
            "one-time LDM7 multicast receiver")) {
        log_flush_error();
    }
    else {
        downlet->mcastRcvrTask = NULL;

        mcastRcvr_delete(downlet->mcastRcvr);
        downlet->mcastRcvr = NULL;
    }
}

static void*
downlet_runBacklogger(void* const arg)
{
    Downlet* const    downlet = (Downlet*)arg;
    Backlogger* const backlogger = downlet->backlogger;
    int               status = backlogger_run(backlogger);

    (void)downlet_setStatusIfOk(downlet, status);

    // Because end of thread
    log_flush(status ? LOG_LEVEL_ERROR : LOG_LEVEL_INFO);

    return NULL;
}

static int
downlet_haltBacklogger(
        void* const     arg,
        const pthread_t thread)
{
    Downlet* const    downlet = (Downlet*)arg;
    Backlogger* const backlogger = downlet->backlogger;

    backlogger_halt(backlogger, thread);

    return 0;
}

static int
downlet_createBacklogger(
        Downlet* const   downlet,
        const signaturet before)
{
    static char id[] = "one-time LDM7 backlog requester";
    int         status;

    downlet->backlogger = backlogger_new(before, downlet);

    if (downlet->backlogger == NULL) {
        log_add("Couldn't construct %s", id);
        status = LDM7_SYSTEM;
    }
    else {
        status = downlet_createTask(downlet, id, &downlet->backloggerTask,
                downlet_runBacklogger, downlet_haltBacklogger);

        if (status)
            backlogger_delete(downlet->backlogger);
    } // `backlogger` constructed

    return status;
}

static void
downlet_destroyBacklogger(
        Downlet* const downlet)
{
    if (downlet_destroyTask(downlet->backloggerTask,
            "one-time LDM7 backlog requester")) {
        log_flush_error();
    }
    else {
        downlet->backloggerTask = NULL;

        backlogger_delete(downlet->backlogger);
        downlet->backlogger = NULL;
    }
}

/**
 * Starts the asynchronous subtasks of a downstream LDM7 that collectively
 * receive data-products. Blocks until all subtasks have started.
 *
 * @pre                       Downstream LDM7 is locked
 * @param[in]  down7          Pointer to the downstream LDM7.
 * @retval     0              Success.
 * @retval     LDM7_SHUTDOWN  The LDM7 has been shut down. No task is running.
 * @retval     LDM7_SYSTEM    Error. `log_add()` called. No task is running.
 * @post                      Downstream LDM7 is locked
 */
static int
downlet_createTasks(Downlet* const downlet)
{
    int status = downlet_createUcastRcvr(downlet);

    if (status) {
        log_add("Couldn't create one-time LDM7 unicast receiver task");
    }
    else {
        status = downlet_createBackstop(downlet);

        if (status) {
            log_add("Couldn't create one-time LDM7 backstop task");
        }
        else {
            status = downlet_createMcastRcvr(downlet);

            if (status) {
                log_add("Couldn't create one-time LDM7 multicast receiver task");
                downlet_destroyBackstop(downlet);
            }
        } // One-time LDM7 backstop task created

        if (status)
            downlet_destroyUcastRcvr(downlet);
    } // One-time LDM7 unicast receiver task created

    return status;
}

/**
 * Destroys the asynchronous subtasks of a one-time downstream LDM7.
 *
 * @pre                    One-time downstream LDM7 is unlocked
 * @param[in,out] downlet  One-time downstream LDM7
 * @post                   One-time downstream LDM7 is unlocked
 */
static void
downlet_destroyTasks(Downlet* const downlet)
{
    downlet_assertUnlocked(downlet);

    (void)downlet_destroyBacklogger(downlet);
    (void)downlet_destroyMcastRcvr(downlet);
    (void)downlet_destroyBackstop(downlet);
    (void)downlet_destroyUcastRcvr(downlet);
}

/**
 * Executes a one-time downstream LDM7. Doesn't return until an error occurs.
 *
 * @param[in,out] downlet        One-time downstream LDM7
 * @retval        LDM7_INTR      Signal caught. `log_add()` called.
 * @retval        LDM7_INVAL     Invalid port number or host identifier.
 *                               `log_add()` called.
 * @retval        LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval        LDM7_RPC       RPC failure (including interrupt). `log_add()`
 *                               called.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 */
static Ldm7Status
downlet_run(Downlet* const downlet)
{
    log_notice_q("Downstream LDM7 starting up: remoteLDM7=%s, feed=%s, "
            "pq=\"%s\"", downlet->upId, s_feedtypet(downlet->feedtype),
            pq_getPathname(downlet->pq));

    // Sets `downlet->{up7Proxy,sock}`
    downlet->status = downlet_initConn(downlet);

    if (downlet->status) {
        char* feedSpec = feedtypet_format(downlet->feedtype);
        log_add("Couldn't create client for subscribing to feed %s from %s",
                downlet->feedId, downlet->upId);
        free(feedSpec);
    }
    else {
        // Blocks until error, reply, or timeout; sets `downlet->mcastInfo`
        downlet->status = up7proxy_subscribe(downlet->up7proxy,
                downlet->feedtype, downlet->vcEnd, &downlet->mcastInfo);

        if (downlet->status) {
            log_add("Couldn't subscribe to feed %s from %s", downlet->feedId,
                    downlet->upId);
        }
        else {
            downlet->status = downlet_createTasks(downlet);

            if (downlet->status) {
                log_add("Error starting subtasks for feed %s from %s",
                        downlet->feedId, downlet->upId);
            }
            else {
                downlet_waitForStatusChange(downlet); // Sets `downlet->status`
                downlet_destroyTasks(downlet);
            } // Subtasks started

            mi_delete(downlet->mcastInfo); // NULL safe
        } // `downlet->mcastInfo` set

        downlet_deinitConn(downlet);
    } // Subscription client allocated

    return downlet->status;
}

/**
 * Creates, executes, and destroys a one-time downstream LDM7. Doesn't return
 * until an error occurs.
 *
 * @param[in] down7          Parent downstream LDM7
 * @param[in] servAddr       Address of upstream LDM7
 * @param[in] feed           LDM feedtype
 * @param[in] mcastIface     IP address of interface to receive multicast
 *                           products
 * @param[in] vcEnd          Local virtual-circuit endpoint
 * @param[in] pq             Product queue
 * @param[in] mrm            Persistent multicast receiver memory
 * @retval    LDM7_INTR      Signal caught. `log_add()` called.
 * @retval    LDM7_INVAL     Invalid port number or host identifier. `log_add()`
 *                           called.
 * @retval    LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval    LDM7_RPC       RPC error. `log_add()` called.
 * @retval    LDM7_REFUSED   Remote host refused connection (server likely isn't
 *                           running). `log_add()` called.
 * @retval    LDM7_SYSTEM    System error. `log_add()` called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. `log_add()` called.
 * @retval    LDM7_UNAUTH    Not authorized. `log_add()` called.
 */
static int
downlet_try(
        Down7* const restrict             down7,
        const ServiceAddr* const restrict servAddr,
        const feedtypet                   feed,
        const char* const restrict        mcastIface,
        const VcEndPoint* const restrict  vcEnd,
        pqueue* const restrict            pq,
        McastReceiverMemory*              mrm)
{
    Downlet downlet;
    int     status = downlet_init(&downlet, down7, servAddr, feed, mcastIface,
            vcEnd, pq, mrm);

    if (status) {
        log_add("Couldn't initialize one-time downstream LDM7");
    }
    else {
        status = downlet_run(&downlet);

        downlet_deinit(&downlet);
    }

    return status;
}

static Ldm7Status
downlet_testConnection(Downlet* const downlet)
{
    return up7proxy_testConnection(downlet->up7proxy);
}

/**
 * Increments the number of data-products successfully inserted into the
 * product-queue of a downstream LDM7.
 *
 * @param[in] downlet  One-time downstream LDM7
 */
void
downlet_incNumProds(Downlet* const downlet)
{
    down7_incNumProds(downlet->down7);
}

/**
 * Adds a data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM7.
 *
 * @param[in] downlet      One-time downstream LDM7.
 * @param[in] prod         data-product.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static int
downlet_recvProd(
        Downlet* const restrict downlet,
        product* const restrict prod)
{
    // Products are also inserted on the multicast-receiver threads
    pqueue* const restrict  pq = downlet->pq;
    int                     status = pq_insert(pq, prod);

    if (status == 0) {
        if (log_is_enabled_info) {
            char buf[LDM_INFO_MAX];

            (void)s_prod_info(buf, sizeof(buf), &prod->info,
                    log_is_enabled_debug);
            log_info_q("Inserted: %s", buf);
        }
        down7_incNumProds(downlet->down7);
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
 * Queues a data-product for being requested by the LDM7 backstop mechanism.
 * This function is called by the multicast LDM receiver; therefore, it must
 * return immediately so that the multicast LDM receiver can continue.
 *
 * @param[in] downlet  One-time downstream LDM7
 * @param[in] iProd    Index of the missed FMTP product.
 */
void
downlet_missedProduct(
    Downlet* const      downlet,
    const FmtpProdIndex iProd)
{
    log_debug_1("Entered: iProd=%lu", (unsigned long)iProd);

    /*
     * Cancellation of the operation of the missed-but-not-requested queue is
     * ignored because nothing can be done about it at this point and no harm
     * should result.
     */
    (void)mrm_addMissedFile(downlet->mrm, iProd);
}

/**
 * Tracks the last data-product to be successfully received by the multicast
 * LDM receiver. This function is called by the multicast LDM receiver;
 * therefore, it must return immediately so that the multicast LDM receiver can
 * continue.
 *
 * The first time this function is called for a given one-time downstream LDM7,
 * it starts a subtask that requests the backlog of data-products that were
 * missed due to the passage of time from the end of the previous session to the
 * reception of the first multicast data-product.
 *
 * @param[in] downlet  Pointer to the one-time downstream LDM7.
 * @param[in] last     Pointer to the metadata of the last data-product to be
 *                     successfully received by the associated multicast
 *                     LDM receiver. Caller may free when it's no longer needed.
 */
void
downlet_lastReceived(
    Downlet* const restrict         downlet,
    const prod_info* const restrict last)
{
    mrm_setLastMcastProd(downlet->mrm, last->signature);

    if (!downlet->mcastWorking) {
        downlet->mcastWorking = true;

        int status = downlet_createBacklogger(downlet, last->signature);

        if (status) {
            log_add("Couldn't start task to request product backlog");
            log_flush_error();
            downlet_setStatusIfOk(downlet, LDM7_SYSTEM);
        }
    }
}

/******************************************************************************
 * Downstream LDM7
 ******************************************************************************/

/**
 * Creates the thread-specific data-key for the pointer to the one-time
 * downstream LDM.
 */
static void
down7_createThreadKey(void)
{
    int status = pthread_key_create(&down7Key, NULL);

    if (status)
        log_add_errno(status, "Couldn't create thread-specific data-key");
}

/**
 * Initializes a downstream LDM7.
 *
 * @param[out] down7        Downstream LDM7 to be initialized
 * @param[in]  servAddr     Pointer to the address of the server from which to
 *                          obtain multicast information, backlog products, and
 *                          products missed by the FMTP layer. Caller may free
 *                          upon return.
 * @param[in]  feed         Feed of multicast group to be received.
 * @param[in]  mcastIface   IP address of interface to use for receiving
 *                          multicast packets. Caller may free upon return.
 * @param[in]  pq           The product-queue. Must be thread-safe (i.e.,
 *                          `pq_getFlags(pq) | PQ_THREADSAFE` must be true).
 * @param[in]  mrm          Persistent multicast receiver memory. Must exist
 *                          until `down7_deinit()` returns.
 * @param[in]  vcEnd        Local virtual-circuit endpoint
 * @retval     0            Success
 * @retval     LDM7_INVAL   Product-queue isn't thread-safe. `log_add()` called.
 * @retval     LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
down7_init(
        Down7* const restrict               down7,
        const ServiceAddr* const restrict   servAddr,
        const feedtypet                     feed,
        const char* const restrict          mcastIface,
        const VcEndPoint* const restrict    vcEnd,
        pqueue* const restrict              pq,
        McastReceiverMemory* const restrict mrm)
{
    down7->pq = pq;
    down7->feedtype = feed;
    down7->mcastInfo = NULL;
    down7->mcastWorking = false;
    down7->numProds = 0;
    down7->status = LDM7_OK;
    down7->mrm = mrm;
    (void)memset(down7->firstMcast, 0, sizeof(signaturet));
    (void)memset(down7->prevLastMcast, 0, sizeof(signaturet));

    int status = LDM7_SYSTEM; // Default error

    /*
     * The product-queue must be thread-safe because this module accesses it on
     * these threads:
     *   - FMTP multicast receiver
     *   - FMTP unicast receiver
     *   - LDM7 data-product receiver.
     */
    if (!(pq_getFlags(pq) | PQ_THREADSAFE)) {
        log_add("Product-queue %s isn't thread-safe: %0x",
                pq_getPathname(pq), pq_getFlags(pq));
        status = LDM7_INVAL;
    }
    else {
        if ((down7->servAddr = sa_clone(servAddr)) == NULL) {
            char buf[256];

            (void)sa_snprint(servAddr, buf, sizeof(buf));
            log_add("Couldn't clone server address \"%s\"", buf);
            status = LDM7_SYSTEM;
        }
        else {
            if ((status = pthread_cond_init(&down7->cond, NULL)) != 0) {
                log_add_errno(status, "Couldn't initialize condition-variable");
                status = LDM7_SYSTEM;
            }
            else {
                status = mutex_init(&down7->mutex);

                if (status) {
                    down7->iface = strdup(mcastIface);

                    if (down7->iface == NULL) {
                        log_add("Couldn't copy multicast interface "
                                "specification");
                        status = LDM7_SYSTEM;
                    }
                    else {
                        if (!vcEndPoint_copy(&down7->vcEnd, vcEnd)) {
                            log_add("Couldn't copy receiver-side "
                                    "virtual-circuit endpoint");
                            status = LDM7_SYSTEM;
                        }
                        else {
                            status = mutex_init(&down7->numProdMutex);

                            if (status) {
                                log_syserr_q("Couldn't initialize "
                                        "number-of-products mutex");
                                status = LDM7_SYSTEM;
                            }
                            else {
                                if ((status = pthread_once(&down7KeyControl,
                                        down7_createThreadKey)) != 0) {
                                    log_add_errno(status,
                                            "Couldn't create downstream LDM7 "
                                            "thread-key");
                                    status = LDM7_SYSTEM;
                                }
                                else {
                                    status = 0; // Success
                                }

                                if (status)
                                    mutex_deinit(&down7->numProdMutex);
                            } // `down7->numProdMutex` initialized

                            if (status)
                                vcEndPoint_deinit(&down7->vcEnd);
                        } // `down7->vcEnd` initialized

                        if (status)
                            free(down7->iface);
                    } // `down7->iface` initialized

                    if (status)
                        mutex_deinit(&down7->mutex);
                } // `down7->mutex` initialized

                if (status)
                    (void)pthread_cond_destroy(&down7->cond);
            } // `down7->cond` initialized

            if (status)
                sa_delete(down7->servAddr);
        } // `down7->servAddr` initialized
    } // Product-queue is thread-safe

    return status;
}

static Ldm7Status
down7_deinit(Down7* const down7)
{
    // Can't destroy `down7Key` because it's static

    mutex_deinit(&down7->numProdMutex);
    vcEndPoint_deinit(&down7->vcEnd);
    free(down7->iface);
    mutex_deinit(&down7->mutex);

    int status = pthread_cond_destroy(&down7->cond);
    if (status) {
        log_add_errno(status, "Couldn't destroy condition-variable");
        status = LDM7_SYSTEM;
    }

    sa_delete(down7->servAddr);

    return status;
}

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
 * @param[in] vcEnd       Local virtual-circuit endpoint
 * @param[in] pq          The product-queue. Must be thread-safe (i.e.,
 *                        `pq_getFlags(pq) | PQ_THREADSAFE` must be true).
 * @param[in] mrm         Persistent multicast receiver memory. Must exist until
 *                        `down7_delete()` returns.
 * @retval    NULL        Failure. `log_add()` called.
 * @return                Pointer to the new downstream LDM7. Caller should call
 *                        `down7_delete()` when it's no longer needed.
 * @see `down7_start()`
 * @see `down7_delete()`
 */
Down7*
down7_new(
    const ServiceAddr* const restrict   servAddr,
    const feedtypet                     feedtype,
    const char* const restrict          mcastIface,
    const VcEndPoint* const restrict    vcEnd,
    pqueue* const restrict              pq,
    McastReceiverMemory* const restrict mrm)
{
    Down7* down7 = log_malloc(sizeof(Down7), "downstream LDM7");

    if (down7 != NULL) {
        if (down7_init(down7, servAddr, feedtype, mcastIface, vcEnd, pq, mrm)) {
            log_add("Couldn't initialize downstream LDM7");
            free(down7);
            down7 = NULL;
        }
    }

    return down7;
}

/**
 * Deletes a downstream LDM7 -- releasing its resources. Inverse of
 * `down7_new()`. Undefined behavior results if the downstream LDM7 is still
 * executing.
 *
 * @param[in] down7        Downstream LDM7
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `down7_new()`
 */
int
down7_delete(Down7* const down7)
{
    int status = down7_deinit(down7);
    free(down7);
    return status;
}

/**
 * Returns the product-queue associated with a downstream LDM7.
 *
 * @param[in] downlet  one-time downstream LDM7.
 * @return             Associated product-queue.
 */
pqueue* down7_getPq(
        Downlet* const downlet)
{
    return downlet->pq;
}

static void
down7_lock(Down7* const down7)
{
    int status = pthread_mutex_lock(&down7->mutex);
    log_assert(status == 0);
}

static void
down7_unlock(Down7* const down7)
{
    int status = pthread_mutex_unlock(&down7->mutex);
    log_assert(status == 0);
}

/**
 * Executes a downstream LDM7. Doesn't return unless an error occurs.
 *
 * @param[in,out] arg          Downstream LDM7
 * @retval        LDM7_INTR    Interrupted by signal
 * @retval        LDM7_INVAL   Invalid port number or host identifier.
 *                             `log_add()` called.
 * @retval        LDM7_RPC     RPC error. `log_add()` called.
 * @retval        LDM7_SYSTEM  System error. `log_add()` called.
 * @see `down7_halt()`
 */
Ldm7Status
down7_run(Down7* const down7)
{
    int status = 0;

    while (!stopFlag_isSet(&down7->stopFlag) && status == 0) {
        status = downlet_try(down7, down7->servAddr, down7->feedtype,
                down7->iface, &down7->vcEnd, down7->pq, down7->mrm);

        if (status == LDM7_TIMEDOUT) {
            log_flush_info();
            status = LDM7_OK; // Try again
        }
        else if (status == LDM7_REFUSED || status == LDM7_NOENT ||
                status == LDM7_UNAUTH) {
            log_flush_warning();

            // Problem might be temporary
            struct timespec when;
            when.tv_sec = time(NULL) + interval;
            when.tv_nsec = 0;
            stopFlag_timedWait(&down7->stopFlag, &when);
        }
        // Else fatal error
    }

    log_flush_error();

    return status;
}

/**
 * Halts an executing downstream LDM7.
 *
 * @param[in] down7   Downstream LDM7
 * @param[in] thread  Thread on which the downstream LDM7 is executing
 */
void
down7_halt(
        Down7* const    down7,
        const pthread_t thread)
{
    stopFlag_set(&down7->stopFlag);
    int status = pthread_kill(thread, SIGTERM);
    log_assert(status == 0);
}

static int
down7_haltTask(
        void* const     arg,
        const pthread_t thread)
{
    down7_halt((Down7*)arg, thread);
    return 0;
}

static void*
down7_runTask(void* const arg)
{
    Down7* const down7 = (Down7*)arg;

    down7_run(down7);

    return &down7->status;
}

/**
 * Starts a downstream LDM7.
 *
 * @param[in,out] down7        Downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
down7_start(Down7* const down7)
{
    /*
     * The mechanism for stopping a downstream LDM7 must work even if the thread
     * is currently in `poll()`). Possible mechanisms include
     * using
     *   - A second "close requested" file-descriptor: Definitely possible at
     *     the cost of one file-descriptor.
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
    // Uses `pthread_kill()`
    Task* task = NULL; // task_create(down7, down7_runTask, down7_haltTask);
    int   status;

    if (task == NULL) {
        log_add_errno(status, "Couldn't create downstream LDM7 task");
        status = LDM7_SYSTEM;
    }
    else {
        //down7->task = task; TODO
        status = 0;
    }

    return status;
}

inline static Ldm7Status
down7_stop(Down7* const down7)
{
#if 0
    int status = task_stop(down7->task, NULL);

    (void)task_delete(down7->task);

    return status ? LDM7_SYSTEM : 0;
#endif // TODO
}

/**
 * Creates an asynchronous downstream LDM7.
 *
 * @param[in]  servAddr    Pointer to the address of the server from which to
 *                         obtain multicast information, backlog products, and
 *                         products missed by the FMTP layer. Caller may free
 *                         upon return.
 * @param[in]  feed        Feed of multicast group to be received
 * @param[in]  mcastIface  IP address of interface to use for receiving
 *                         multicast packets. Caller may free upon return.
 * @param[in]  pq          The product-queue. Must be thread-safe (i.e.,
 *                         `pq_getFlags(pq) | PQ_THREADSAFE` must be true).
 * @param[in]  mrm         Persistent multicast receiver memory. Must exist
 *                         until `down7_destroy()` returns.
 * @param[in]  vcEnd       Local virtual-circuit endpoint
 * @retval     NULL        Failure. `log_add()` called.
 * @return                 Pointer to the executing downstream LDM7. Caller
 *                         should call `down7_destroy()` when it's no longe
 *                         needed.
 * @see `down7_destroy()`
 */
Down7*
down7_create(
        const ServiceAddr* const restrict   servAddr,
        const feedtypet                     feed,
        const char* const restrict          mcastIface,
        const VcEndPoint* const restrict    vcEnd,
        pqueue* const restrict              pq,
        McastReceiverMemory* const restrict mrm)
{
    Down7* down7 = down7_new(servAddr, feed, mcastIface, vcEnd, pq, mrm);

    if (down7 == NULL) {
        log_add("Couldn't construct downstream LDM7");
    }
    else {
        if (down7_start(down7)) {
            log_add("Couldn't start downstream LDM7");
            down7_delete(down7);
            down7 = NULL;
        }
    }

    return down7;
}

/**
 * Destroys a downstream LDM7 by stopping it and then deleting it. Blocks until
 * that's done.
 * @param[in,out] down7        Downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
down7_destroy(Down7* const down7)
{
    int status = down7_stop(down7);

    if (status == 0)
        status = down7_delete(down7);

    return status;
}

/**
 * Queues a data-product for being requested via the LDM7 backstop mechanism.
 * The multicast LDM receiver uses `downlet_missedProduct()`, instead.
 *
 * @param[in] down7  Downstream LDM7
 * @param[in] iProd  Index of the missed FMTP product.
 */
void
down7_missedProduct(
    Down7* const        down7,
    const FmtpProdIndex iProd)
{
    log_debug_1("Entered: iProd=%lu", (unsigned long)iProd);

    /*
     * Cancellation of the operation of the missed-but-not-requested queue is
     * ignored because nothing can be done about it at this point and no harm
     * should result.
     */
    (void)mrm_addMissedFile(down7->mrm, iProd);
}

/**
 * Increments the number of data-products successfully inserted into the
 * product-queue of a downstream LDM7.
 *
 * @param[in] down7  Downstream LDM7.
 */
void
down7_incNumProds(
        Down7* const down7)
{
    int status = pthread_mutex_lock(&down7->numProdMutex);
        log_assert(status == 0);
        down7->numProds++;
    (void)pthread_mutex_unlock(&down7->numProdMutex);
}

/**
 * Returns the number of data-products successfully inserted into the product-
 * queue of a downstream LDM7.
 *
 * @param[in] downlet  The downstream LDM7.
 * @return           The number of successfully inserted data-products.
 */
uint64_t
down7_getNumProds(
        Down7* const down7)
{
    int status = pthread_mutex_lock(&down7->numProdMutex);
        log_assert(status == 0);
        uint64_t num = down7->numProds;
    (void)pthread_mutex_unlock(&down7->numProdMutex);

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
 * Processes a missed data-product from a remote LDM7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM7 because it was missed by the
 * multicast LDM receiver. Destroys the server-side RPC transport if the
 * data-product can't be inserted into the product-queue. Does not reply. Called
 * by the RPC dispatcher `ldmprog_7()`.
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
    Downlet*         downlet = pthread_getspecific(down7Key);
    FmtpProdIndex    iProd;

    if (!mrm_peekRequestedFileNoWait(downlet->mrm, &iProd) ||
            iProd != missedProd->iProd) {
        char  buf[LDM_INFO_MAX];
        char* rmtStr = sockAddrIn_format(svc_getcaller(rqstp->rq_xprt));

        log_add("Unexpected product received from %s: %s", rmtStr,
                s_prod_info(buf, sizeof(buf), info, log_is_enabled_debug));
        free(rmtStr);
    }
    else {
        // The queue can't be empty
        (void)mrm_removeRequestedFileNoWait(downlet->mrm, &iProd);

        if (downlet_recvProd(downlet, &missedProd->prod) != 0) {
            char  buf[LDM_INFO_MAX];
            char* rmtStr = sockAddrIn_format(svc_getcaller(rqstp->rq_xprt));

            log_add("Couldn't insert missed product from %s: %s", rmtStr,
                    s_prod_info(buf, sizeof(buf), info, log_is_enabled_debug));
            free(rmtStr);

            svc_destroy(rqstp->rq_xprt);
        }
    }

    return NULL; // Causes RPC dispatcher to not reply
}

/**
 * Asynchronously accepts notification from the upstream LDM7 that a requested
 * data-product doesn't exist. Called by the RPC dispatch routine `ldmprog_7()`.
 *
 * @param[in] iProd   Index of the data-product.
 * @param[in] rqstp   Pointer to the RPC service-request.
 */
void*
no_such_product_7_svc(
    FmtpProdIndex* const missingIprod,
    struct svc_req* const rqstp)
{
    Downlet*         downlet = pthread_getspecific(down7Key);
    FmtpProdIndex iProd;

    if (!mrm_peekRequestedFileNoWait(downlet->mrm, &iProd) ||
        iProd != *missingIprod) {
        log_add("Product %lu is unexpected", (unsigned long)*missingIprod);
    }
    else {
        // The queue can't be empty
        (void)mrm_removeRequestedFileNoWait(downlet->mrm, &iProd);

        log_warning_q("Requested product %lu doesn't exist",
                (unsigned long)*missingIprod);
    }

    return NULL ; /* don't reply */
}

/**
 * Asynchronously processes a backlog data-product from a remote LDM7 by
 * attempting to add the data-product to the product-queue. The data-product
 * should have been previously requested from the remote LDM7 because it was
 * missed during the previous session. Destroys the server-side RPC transport if
 * the data-product can't be inserted into the product-queue. Does not reply.
 * Called by the RPC dispatcher `ldmprog_7()`.
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
    Downlet* downlet = pthread_getspecific(down7Key);
    int      status = downlet_recvProd(downlet, prod);

    log_assert(status == 0);

    return NULL; // causes RPC dispatcher to not reply
}

/**
 * Asynchronously accepts notification that the downstream LDM7 associated with
 * the current thread has received all backlog data-products from its upstream
 * LDM7. From now on, the current process may be terminated for a time period
 * that is less than the minimum residence time of the upstream LDM7's
 * product-queue without loss of data. Called by the RPC dispatcher
 * `ldmprog_7()`.
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
