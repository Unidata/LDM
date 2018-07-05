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
#include "Completer.h"
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
#include "StopFlag.h"
#include "Thread.h"
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
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <time.h>
#include <unistd.h>

#ifndef MAX
    #define MAX(a,b) ((a) >= (b) ? (a) : (b))
#endif

/******************************************************************************
 * Thread-dependent context-pointer for RPC service functions
 ******************************************************************************/

/**
 * Key for getting the pointer to a downstream LDM7 that's associated with a
 * thread:
 */
static pthread_key_t  down7Key;

/**
 * Lockout for creating `down7Key`:
 */
static pthread_once_t down7KeyControl = PTHREAD_ONCE_INIT;

/******************************************************************************
 * Proxy for an upstream LDM7
 ******************************************************************************/

typedef uint64_t     Up7ProxyMagic;
static Up7ProxyMagic UP7PROXY_MAGIC = 0x9b25d0c5400ee17c;

/**
 * Data-structure of a thread-safe proxy for an upstream LDM7 associated with a
 * downstream LDM7.
 */
typedef struct up7Proxy {
    char*                 remoteId; ///< Socket address of upstream LDM7
    CLIENT*               clnt;     ///< Client-side RPC handle
    pthread_mutex_t       mutex;    ///< Because accessed by multiple threads
    Up7ProxyMagic         magic;    ///< For detecting invalid instances
} Up7Proxy;

inline static void
up7Proxy_assertValid(const Up7Proxy* const proxy)
{
    log_assert(proxy->magic == UP7PROXY_MAGIC);
}

/**
 * Locks an upstream LDM7 proxy for exclusive access.
 *
 * @pre                   `proxy->clnt != NULL`
 * @param[in] proxy       Pointer to the upstream LDM7 proxy to be locked.
 */
static void
up7Proxy_lock(
    Up7Proxy* const proxy)
{
    log_debug_1("Entered");
    int status = pthread_mutex_lock(&proxy->mutex);
    log_assert(status == 0);
}

/**
 * Unlocks an upstream LDM7 proxy.
 *
 * @param[in] proxy       Pointer to the upstream LDM7 proxy to be unlocked.
 */
static void
up7Proxy_unlock(
    Up7Proxy* const proxy)
{
    log_debug_1("Entered");
    int status = pthread_mutex_unlock(&proxy->mutex);
    log_assert(status == 0);
}

// Forward declaration
static Ldm7Status
downlet_testConnection(Downlet* const downlet);

static int
up7Proxy_init(
        Up7Proxy* const restrict  proxy,
        const int                 socket,
        struct sockaddr_in* const sockAddr)
{
    int status;

    if (proxy == NULL || socket <= 0 || sockAddr == NULL) {
        status = LDM7_INVAL;
    }
    else {
        status = mutex_init(&proxy->mutex, PTHREAD_MUTEX_ERRORCHECK, true);

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
                // `log_assert(status == 0)`

                int sock = socket;
                proxy->clnt = clnttcp_create(sockAddr, LDMPROG, SEVEN, &sock, 0,
                        0);

                if (proxy->clnt == NULL) {
                    log_add_syserr("Couldn't create RPC client for %s: %s",
                            proxy->remoteId);
                    status = LDM7_RPC;
                }
                else {
                    proxy->magic = UP7PROXY_MAGIC;
                }

                if (status)
                    free(proxy->remoteId);
            } // `proxy->remoteId` allocated

            if (status)
                pthread_mutex_destroy(&proxy->mutex);
        } // `proxy->mutex` initialized
    } // Non-NULL input arguments

    return status;
}

static void
up7Proxy_destroy(Up7Proxy* const proxy)
{
    up7Proxy_lock(proxy);
        proxy->magic = ~UP7PROXY_MAGIC;
        // Destroys *and* frees `clnt`. Won't close externally-created socket.
        clnt_destroy(proxy->clnt);
        proxy->clnt = NULL;
        free(proxy->remoteId);
    up7Proxy_unlock(proxy);

    (void)pthread_mutex_destroy(&proxy->mutex);
}

/**
 * Returns a new proxy for an upstream LDM7.
 *
 * @param[out] up7Proxy     Pointer to new proxy
 * @param[in]  socket       Socket to use
 * @param[in]  sockAddr     Address of upstream LDM7 server
 * @retval     0            Success
 * @retval     LDM7_INVAL   Invalid argument
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static int up7Proxy_new(
        Up7Proxy** const restrict up7Proxy,
        const int                 socket,
        struct sockaddr_in* const sockAddr)
{
    int status;

    if (up7Proxy == NULL) {
        status = LDM7_INVAL;
    }
    else {
        Up7Proxy* const proxy = log_malloc(sizeof(Up7Proxy),
                "upstream LDM7 proxy");

        if (proxy == NULL) {
            status = LDM7_SYSTEM;
        }
        else {
            status = up7Proxy_init(proxy, socket, sockAddr);

            if (status) {
                log_add("Couldn't initialize proxy for upstream LDM7");
                free(proxy);
            }
            else {
                *up7Proxy = proxy;
            }
        }
    }

    return status;
}

/**
 * Deletes a proxy for an upstream LDM7.
 * @param[in] proxy
 */
static void
up7Proxy_free(
        Up7Proxy* const proxy)
{
    log_debug_1("Entered");

    if (proxy) {
        up7Proxy_assertValid(proxy);
        up7Proxy_destroy(proxy);
        free(proxy);
    }
}

/**
 * Subscribes to an upstream LDM7 server.
 *
 * @param[in]  proxy          Proxy for the upstream LDM7.
 * @param[in]  feed           Feed specification.
 * @param[in]  vcEnd          Local virtual-circuit endpoint
 * @param[out] mcastInfo      Information on the multicast group corresponding
 *                            to `feed`.
 * @param[out] ifaceAddr      IP address of VLAN virtual interface
 * @retval     0              If and only if success. `*mcastInfo` is set. The
 *                            caller should call `mi_free(*mcastInfo)` when
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
up7Proxy_subscribe(
        Up7Proxy* const restrict         proxy,
        feedtypet                        feed,
        const VcEndPoint* const restrict vcEnd,
        McastInfo** const restrict       mcastInfo,
        in_addr_t* const restrict        ifaceAddr)
{
    int status;

    up7Proxy_assertValid(proxy);

    up7Proxy_lock(proxy);

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
        log_add("subscribe_7() returned NULL: %s", clnt_errmsg(clnt));
        status = clntStatusToLdm7Status(clnt);
    }
    else {
        status = reply->status;

        if (status == LDM7_UNAUTH) {
            log_add("Subscription request was denied");
        }
        else if (status == LDM7_NOENT) {
            log_add("Upstream LDM7 doesn't multicast any part of requested "
                    "feed");
        }
        else if (status) {
            log_add("subscribe_7() failure: status=%d", status);
        }
        else {
            *mcastInfo = mi_clone(&reply->SubscriptionReply_u.info.mcastInfo);
            *ifaceAddr = cidrAddr_getAddr(
                    &reply->SubscriptionReply_u.info.fmtpAddr);
        }
        xdr_free(xdr_SubscriptionReply, (char*)reply);
    }

    up7Proxy_unlock(proxy);

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
up7Proxy_requestBacklog(
    Up7Proxy* const restrict    proxy,
    BacklogSpec* const restrict spec)
{
    up7Proxy_assertValid(proxy);

    up7Proxy_lock(proxy);
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
            status = LDM7_RPC;
        }
    up7Proxy_unlock(proxy);

    return status;
}

/**
 * Requests a data-product that was missed by the multicast LDM receiver.
 *
 * @param[in] proxy       Pointer to the upstream LDM7 proxy.
 * @param[in] iProd       FMTP product-ID of missed data-product.
 * @retval    0           Success. A data-product was requested.
 * @retval    LDM7_RPC    RPC error. `log_add()` called.
 */
static int
up7Proxy_requestProduct(
    Up7Proxy* const     proxy,
    const FmtpProdIndex iProd)
{
    int     status;

    up7Proxy_assertValid(proxy);

    up7Proxy_lock(proxy);
        CLIENT* clnt = proxy->clnt;

        log_debug_1("iProd=%lu", (unsigned long)iProd);

        // Asynchronous send => no reply
        McastProdIndex index = iProd;
        (void)request_product_7(&index, clnt);

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
            status = LDM7_RPC;
        }
    up7Proxy_unlock(proxy);

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
up7Proxy_testConnection(
    Up7Proxy* const proxy)
{
    int status;

    up7Proxy_assertValid(proxy);

    up7Proxy_lock(proxy);

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

    up7Proxy_unlock(proxy);

    return status;
}

/******************************************************************************
 * Requester of Data-Products Missed by the FMTP Layer:
 ******************************************************************************/

typedef struct backstop {
    McastReceiverMemory*  mrm;       ///< Persistent multicast receiver memory
    Up7Proxy*             up7Proxy;  ///< Proxy for upstream LDM7
    struct downlet*       downlet;   ///< Parent one-time, downstream LDM7
    signaturet            prevLastMcast;    ///< Previous session's Last prod
    bool                  prevLastMcastSet; ///< `prevLastMcast` set?
} Backstop;

/**
 * Initializes a one-time, LDM7 missed-product backstop.
 * @param[out]    backstop     Requester of missed products
 * @param[in]     up7Proxy     Proxy for upstream LDM7
 * @param[in]     mrm          Multicast receiver memory. Must exist until
 *                             `backstop_deinit()` returns.
 * @param[in,out] downlet      Parent one-time, downstream LDM7
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
            "one-time, LDM7 missed-product backstop");

    if (backstop != NULL)
        backstop_init(backstop, up7Proxy, mrm, downlet);

    return backstop;
}

static void
backstop_free(Backstop* const backstop)
{
    log_debug_1("Entered");

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
 * @retval    0              `backstop_stop()` was called
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

                status = up7Proxy_requestProduct(backstop->up7Proxy, iProd);

                if (status) {
                    log_add("Couldn't request product");
                    break;
                }
            } // product-index added to requested-but-not-received queue
        } // have peeked-at product-index from missed-but-not-requested queue
    }

    return status;
}

static int
backstop_halt(
        Backstop* const backstop,
        const pthread_t thread)
{
    log_debug_1("Entered");
    mrm_shutDownMissedFiles(backstop->mrm);

    return 0;
}

/******************************************************************************
 * Receiver of unicast products from an upstream LDM7
 ******************************************************************************/

typedef struct ucastRcvr {
    SVCXPRT*              xprt;
    char*                 remoteStr;
    struct downlet*       downlet;
    StopFlag              stopFlag;
} UcastRcvr;

/**
 * Runs the RPC-based server of a downstream LDM7. *Might* destroy and
 * unregister the service transport. Doesn't return until `ucastRcvr_stop()` is
 * called or an error occurs
 *
 * @param[in]     ucastRcvr      Unicast receiver
 * @retval        0              `ucastRcvr_halt()` was called.
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

    while (!stopFlag_isSet(&ucastRcvr->stopFlag)) {
        // log_debug_1("Calling poll(): socket=%d", sock); // Excessive output
        status = poll(&pfd, 1, timeout);

        if (0 == status) {
            // Timeout
            if (!stopFlag_isSet(&ucastRcvr->stopFlag)) {
                status = downlet_testConnection(ucastRcvr->downlet);

                if (status) {
                    log_add("Connection to %s is broken", ucastRcvr->remoteStr);
                    break;
                }
            }
            continue;
        }

        if (status < 0) {
            if (errno == EINTR) {
                log_add("poll() on socket %d to upstream LDM7 %s was "
                        "interrupted", sock, ucastRcvr->remoteStr);
                status = LDM7_INTR;
            }
            else {
                log_add_syserr("poll() failed on socket %d to upstream LDM7 "
                        "%s", sock, ucastRcvr->remoteStr);
                status = LDM7_SYSTEM;
            }
            break;
        }

        if (pfd.revents & POLLERR) {
            log_add("Error on socket %d to upstream LDM7 %s", sock,
                    ucastRcvr->remoteStr);
            status = LDM7_SYSTEM;
            break;
        }

        if (pfd.revents & POLLHUP) {
            log_add_syserr("Socket %d to upstream LDM7 %s was closed", sock,
                    ucastRcvr->remoteStr);
            status = LDM7_SYSTEM;
            break;
        }

        if (pfd.revents & (POLLIN | POLLRDNORM)) {
            /*
             * Processes RPC message. Calls `ldmprog_7()`. Calls
             * `svc_destroy(ucastRcvr->xprt)` on error.
             */
            svc_getreqsock(sock);

            if (!FD_ISSET(sock, &svc_fdset)) {
                // `svc_getreqsock()` destroyed `ucastRcvr->xprt`
                log_add("Connection to upstream LDM7 %s was closed by RPC "
                        "layer", ucastRcvr->remoteStr);
                ucastRcvr->xprt = NULL; // To inform others
                status = LDM7_RPC;
                break;
            }
            else {
                status = 0;
            }
        }
    } // `poll()` loop

    // Eclipse IDE wants to see a return
    return stopFlag_isSet(&ucastRcvr->stopFlag) ? 0 : status;
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
     * one-time, downstream LDM7; therefore, a thread-specific pointer to the
     * one-time, downstream LDM7 is set to provide context to those that need it.
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

        if (status)
            log_add("Error in unicast product receiver");
    } // thread-specific pointer to downstream LDM7 is set

    return status;
}

static void
ucastRcvr_halt(
        UcastRcvr* const ucastRcvr,
        const pthread_t  thread)
{
    log_debug_1("Entered");
    stopFlag_set(&ucastRcvr->stopFlag);
    pthread_kill(thread, SIGTERM);
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

static Ldm7Status
ucastRcvr_init(
        UcastRcvr* const restrict ucastRcvr,
        const int                 sock,
        Downlet* const restrict   downlet)
{
    SVCXPRT* xprt;
    int      status = ucastRcvr_createXprt(sock, &xprt);

    if (status) {
        log_add("Couldn't create server-side transport on socket %d", sock);
    }
    else {
        char* remoteStr = ipv4Sock_getPeerString(sock);

        // Last argument == 0 => don't register with portmapper
        if (!svc_register(ucastRcvr->xprt, LDMPROG, SEVEN, ldmprog_7, 0)) {
            log_add("Couldn't register one-time, RPC server for receiving "
                    "data-products from \"%s\"",  remoteStr);
            status = LDM7_RPC;
        }
        else {
            status = stopFlag_init(&ucastRcvr->stopFlag);
            log_assert(status == 0);

            ucastRcvr->downlet = downlet;
            ucastRcvr->remoteStr = remoteStr;
            ucastRcvr->xprt = xprt;

            status = LDM7_OK;
        } // LDM7 service registered

        if (status)
            svc_destroy(xprt);
    } // `ucastRcvr->xprt` initialized

    return status;
}

static void
ucastRcvr_destroy(UcastRcvr* const ucastRcvr)
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
 * Allocates and initializes a new one-time, LDM7 unicast receiver.
 * @param[in]     sock         Unicast socket with upstream LDM7
 * @param[in]     downlet      Parent one-time, downstream LDM7
 * @retval        NULL         Failure. `log_add()` called.
 * @return                     Allocated and initialized one-time, LDM7 unicast
 *                             receiver
 * @see          `ucastRcvr_free()`
 */
static UcastRcvr*
ucastRcvr_new(
        const int                 sock,
        Downlet* const restrict   downlet)
{
    UcastRcvr* ucastRcvr = log_malloc(sizeof(UcastRcvr),
            "one-time, LDM7 unicast receiver");

    if (ucastRcvr != NULL) {
        if (ucastRcvr_init(ucastRcvr, sock, downlet)) {
            log_add("Couldn't initialize one-time, LDM7 unicast receiver");
            free(ucastRcvr);
            ucastRcvr = NULL;
        }
    }

    return ucastRcvr;
}

static void
ucastRcvr_free(UcastRcvr* const ucastRcvr)
{
    log_debug_1("Entered");

    if (ucastRcvr) {
        ucastRcvr_destroy(ucastRcvr);
        free(ucastRcvr);
    }
}

/******************************************************************************
 * Virtual interface for a VLAN
 ******************************************************************************/

/**
 * Executes a command. Logs the command's standard output and standard error
 * streams. Waits for the command to terminate.
 *
 * @param[in] file         Filename of process image
 * @param[in] cmd          Command to execute
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System or command failure. `log_add()` called.
 */
static int command(
        char* const restrict file,
        char* const restrict cmd)
{
    int   fds[2];
    int   status = pipe(fds);

    if (status) {
        log_add_syserr("pipe() failure");
        status = LDM7_SYSTEM;
    }
    else {
        const pid_t pid = fork();

        if (pid == -1) {
            log_add_syserr("fork() failure");
            status = LDM7_SYSTEM;
        }
        else if (pid == 0) {
            /* Child process */
            (void)close(fds[0]); // Read end of pipe unneeded
            (void)dup2(fds[1], STDOUT_FILENO);
            (void)dup2(fds[1], STDERR_FILENO);

            (void)execlp(file, cmd, NULL);

            log_add_syserr("execlp() failure");
            log_flush_error();
            exit(1);
        }
        else {
            /* Parent process */
            (void)close(fds[1]); // Write end of pipe unneeded
            fds[1] = -1;

            FILE* input = fdopen(fds[0], "r");

            if (input == NULL) {
                log_add_syserr("fdopen() failure");
                status = LDM7_SYSTEM;
            }
            else {
                char line[_POSIX_MAX_INPUT+1];

                while ((fgets(input, line, sizeof(line))) != NULL)
                    log_notice_1(line);

                (void)fclose(input);
                fds[0] = -1;
            } // `input` open

            int exitStatus;
            status = waitpid(pid, &exitStatus, 0);

            if (status == -1) {
                log_add_syserr("waitpid() failure for process %d", pid);
                status = LDM7_SYSTEM;
            }
            else {
                status = WEXITSTATUS(exitStatus);

                if (status) {
                    log_add("Command exited with status %d", status);
                    status = LDM7_SYSTEM;
                }
            }
        } // Parent process. `pid` set.

        if (fds[0] != -1)
            (void)close(fds[0]);
        if (fds[1] != -1)
            (void)close(fds[1]);
    } // Pipe opened in `fds`

    return status;
}

static const char vlanUtil[] = "vlanUtil";

/**
 * Creates an FMTP VLAN. Destroys any previously-existing VLAN.
 *
 * @param[in] srvrAddrStr  Address of sending FMTP server in dotted-decimal form
 * @param[in] ifaceName    Name of virtual interface to be created
 * @param[in] ifaceAddr    IP address of virtual interface
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @retval    LDM7_INVAL   Invalid argument. `log_add()` called.
 */
static int vlanUtil_create(
        const char* const restrict srvrAddrStr,
        const char* const restrict ifaceName,
        const in_addr_t            ifaceAddr)
{
    int  status;
    char ifaceAddrStr[INET_ADDRSTRLEN];

    // Can't fail
    (void)inet_ntop(AF_INET, &ifaceAddr, ifaceAddrStr, sizeof(ifaceAddrStr));

    char* const cmd = ldm_format(128, "%s create %s %s %s", vlanUtil,
            srvrAddrStr, ifaceName, ifaceAddrStr);

    if (cmd == NULL) {
        log_add("Couldn't construct command to create FMTP VLAN");
        status = LDM7_SYSTEM;
    }
    else {
        status = command(vlanUtil, cmd);

        if (status)
            log_add("Couldn't create FMTP VLAN via command \"%s\"", cmd);

        free(cmd);
    } // `cmd` allocated

    return status;
}

/**
 * Destroys an FMTP VLAN.
 *
 * @param[in] srvrAddrStr  IP address of sending FMTP server in dotted-decimal
 *                         form
 * @param[in] ifaceName    Name of virtual interface to be destroyed
 */
static void vlanUtil_destroy(
        const char* const restrict srvrAddrStr,
        const char* const restrict ifaceName)
{
    int         status;
    char* const cmd = ldm_format(128, "%s destroy %s %s", vlanUtil, srvrAddrStr,
            ifaceName);

    if (cmd == NULL) {
        log_add("Couldn't construct command to destroy FMTP VLAN");
        status = LDM7_SYSTEM;
    }
    else {
        status = command(vlanUtil, cmd);

        if (status)
            log_add("Couldn't destroy FMTP VLAN via command \"%s\"", cmd);

        free(cmd);
    } // `cmd` allocated
}

/******************************************************************************
 * Receiver of multicast products from an upstream LDM7 (uses the FMTP layer)
 ******************************************************************************/

typedef struct mcastRcvr {
    Mlr*            mlr;          ///< Multicast LDM receiver
    /// Dotted decimal form of sending FMTP server
    const char      fmtpSrvrAddr[INET_ADDRSTRLEN];
    const char*     ifaceName;    ///< VLAN interface to create
    struct downlet* downlet;      ///< Parent one-time, downstream LDM7
} McastRcvr;

/**
 * Initializes a multicast receiver.
 *
 * @param[in] mcastRcvr    Multicast receiver to be initialized
 * @param[in] mcastInfo    Information on multicast group
 * @param[in] ifaceName    Name of VLAN virtual interface to be created. Must
 *                         exist for duration of initialized instance.
 * @param[in] ifaceAddr    Address to be assigned to VLAN interface
 * @param[in] pq           Product queue
 * @param[in] downlet      Parent, one-time, downstream LDM7
 * @retval    0            Success
 * @retval    LDM7_INVAL   Invalid address of sending FMTP server. `log_add()`
 *                         called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
static int
mcastRcvr_init(
        McastRcvr* const restrict  mcastRcvr,
        McastInfo* const restrict  mcastInfo,
        const char* const restrict ifaceName,
        const in_addr_t            ifaceAddr,
        pqueue* const restrict     pq,
        Downlet* const restrict    downlet)
{
    const char* fmtpSrvrId = mcastInfo->server.inetId;
    int         status = getDottedDecimal(fmtpSrvrId, mcastRcvr->fmtpSrvrAddr);

    if (status) {
        log_add("Invalid address of sending FMTP server: \"%s\"", fmtpSrvrId);
        status = LDM7_INVAL;
    }
    else {
        status = vlanUtil_create(mcastRcvr->fmtpSrvrAddr, ifaceName, ifaceAddr);

        if (status) {
            log_add("Couldn't create FMTP VLAN");
        }
        else {
            Mlr* mlr = mlr_new(mcastInfo, ifaceName, pq, downlet);

            if (mlr == NULL) {
                log_add("Couldn't create multicast LDM receiver");
                status = LDM7_SYSTEM;
            }
            else {
                mcastRcvr->mlr = mlr;
                mcastRcvr->ifaceName = ifaceName;
                mcastRcvr->downlet = downlet;
                status = 0;
            } // `mlr` allocated

            if (status)
                vlanUtil_destroy(mcastRcvr->fmtpSrvrAddr, ifaceName);
        } // FMTP VLAN interface created
    }

    return status;
}

inline static void
mcastRcvr_destroy(McastRcvr* const mcastRcvr)
{
    mlr_free(mcastRcvr->mlr);
    vlanUtil_destroy(mcastRcvr->fmtpSrvrAddr, mcastRcvr->ifaceName);
}

static McastRcvr*
mcastRcvr_new(
        McastInfo* const restrict      mcastInfo,
        const char* const restrict     ifaceName,
        const in_addr_t                ifaceAddr,
        pqueue* const restrict         pq,
        Downlet* const restrict        downlet)
{
    McastRcvr* mcastRcvr = log_malloc(sizeof(McastRcvr),
            "a one-time, LDM7 multicast receiver");

    if (mcastRcvr) {
        if (mcastRcvr_init(mcastRcvr, mcastInfo, ifaceName, ifaceAddr, pq,
                downlet))
        {
            free(mcastRcvr);
            mcastRcvr = NULL;
        }
    }

    return mcastRcvr;
}

inline static void
mcastRcvr_free(McastRcvr* const mcastRcvr)
{
    log_debug_1("Entered");

    if (mcastRcvr) {
        mcastRcvr_destroy(mcastRcvr);
        free(mcastRcvr);
    }
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

    int status = mlr_run(mcastRcvr->mlr); // Blocks

    if (status)
        log_add("Error in multicast receiver");

    return status;
}

inline static int
mcastRcvr_halt(
        McastRcvr* const mcastRcvr,
        const pthread_t  thread)
{
    log_debug_1("Entered");
    mlr_halt(mcastRcvr->mlr);

    return 0;
}

/******************************************************************************
 * Requester of the backlog of data products
 ******************************************************************************/

typedef struct backlogger {
    /// Signature of first product received via multicast
    signaturet            before;
    struct downlet*       downlet; ///< Parent one-time, downstream LDM7
    StopFlag              stopFlag;
} Backlogger;

/**
 * @param[out] backlogger  Backlog requester
 * @param[in]  before      Signature of first product received via multicast
 * @param[in]  downlet     One-time downstream LDM7. Must exist until
 *                         `backlogger_destroy()` is called.
 * @retval     0           Success
 * @retval     EAGAIN      Out of resources. `log_add()` called.
 * @retval     ENOMEM      Out of memory. `log_add()` called.
 */
static int
backlogger_init(
        Backlogger* const backlogger,
        const signaturet  before,
        Downlet* const    downlet)
{
    int status = stopFlag_init(&backlogger->stopFlag);

    if (status) {
        log_add("Couldn't initialize stop flag");
    }
    else {
        backlogger->downlet = downlet;

        (void)memcpy(backlogger->before, before, sizeof(signaturet));
    }

    return status;
}

static void
backlogger_destroy(Backlogger* const backlogger)
{
    stopFlag_destroy(&backlogger->stopFlag);
}

static Backlogger*
backlogger_new(
        const signaturet before,
        Downlet* const   downlet)
{
    Backlogger* backlogger = log_malloc(sizeof(Backlogger),
            "missed-product backlog requester");

    if (backlogger) {
        if (backlogger_init(backlogger, before, downlet)) {
            log_add("Couldn't initialize missed-product backlog requester");
            free(backlogger);
            backlogger = NULL;
        }
    }

    return backlogger;
}

inline static void
backlogger_free(Backlogger* const backlogger)
{
    log_debug_1("Entered");

    if (backlogger) {
        backlogger_destroy(backlogger);
        free(backlogger);
    }
}

// Forward declaration
static Ldm7Status
downlet_requestBacklog(
        Downlet* const   downlet,
        const signaturet before);

/**
 * Executes a task that requests the backlog of data-products from the end of
 * the previous session.
 *
 * @param[in,out] backlogger  Requester of backlog
 * @retval        0           Success or `backlogger_halt()` was called
 * @retval        LDM7_RPC    Error in RPC layer. `log_add()` called.
 */
static int
backlogger_run(Backlogger* const backlogger)
{
    int status = downlet_requestBacklog(backlogger->downlet,
                backlogger->before);

    if (stopFlag_isSet(&backlogger->stopFlag)) {
        status = 0;
    }
    else if (status) {
        log_add("Error in backlog-requesting task");
    }

    return status;
}

static int
backlogger_halt(
        Backlogger* const backlogger,
        const pthread_t   thread)
{
    log_debug_1("Entered");
    stopFlag_set(&backlogger->stopFlag);
    pthread_kill(thread, SIGTERM);

    return 0;
}

/******************************************************************************
 * One-time downstream LDM7
 ******************************************************************************/

/**
 * Data structure of a one-time, downstream LDM7. It is executed only once per
 * connection attempt.
 */
typedef struct downlet {
    struct down7*        down7;      ///< Parent downstream LDM7
    Backstop*            backstop;   ///< Requests products FMTP layer missed
    UcastRcvr*           ucastRcvr;  ///< Receiver of unicast products
    Backlogger*          backlogger; ///< Requests backlog of products
    Future*              backloggerFuture;
    Up7Proxy*            up7Proxy;   ///< Proxy for upstream LDM7
    pqueue*              pq;         ///< pointer to the product-queue
    const ServiceAddr*   servAddr;   ///< socket address of remote LDM7
    McastInfo*           mcastInfo;  ///< information on multicast group
    in_addr_t            ifaceAddr;  ///< IP address of VLAN virtual interface
    /**
     * IP address of interface to use for receiving multicast and unicast
     * packets
     */
    const char*          ifaceName;
    McastRcvr*           mcastRcvr;
    char*                upId;          ///< ID of upstream LDM7
    char*                feedId;        ///< Desired feed specification
    /// Server-side transport for missing products
    SVCXPRT*             xprt;
    ///< Persistent multicast receiver memory
    McastReceiverMemory* mrm;
    const VcEndPoint*    vcEnd;         ///< Local virtual-circuit endpoint
    Completer*           completer;     ///< Async.-task completion service
    pthread_t            thread;
    feedtypet            feedtype;      ///< Feed of multicast group
    int                  sock;          ///< Socket with remote LDM7
    volatile bool        mcastWorking;  ///< Product received via multicast?
} Downlet;

/**
 * Waits for a change in the status of a one-time, downstream LDM7.
 *
 * @param[in] down7        One-time downstream LDM7
 * @retval    0            Completion service was shut down
 * @retval    LDM7_LOGIC   Completion service is empty. `log_add()` called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @return                 Status of first failed task or status of first
 *                         completed task that isn't backlogger
 */
static Ldm7Status
downlet_waitForStatusChange(Downlet* const downlet)
{
    int status = 0;

    for (;;) {
        Future* future = completer_take(downlet->completer);

        if (future == NULL) {
            log_add("Completion service is empty");
            status = LDM7_LOGIC;
        }
        else {
            status = future_getResult(future, NULL); // No result object

            if (status == ECANCELED) {
                status = 0;
            }
            else if (status == EPERM) {
                status = future_runFuncStatus(future);
            }
            else if (status) {
                log_add("Couldn't get result of task");
                status = LDM7_SYSTEM;
            }
            else {
                // log_assert(status == 0);
                // Successful completion of backlogger task is ignored
                if (future == downlet->backloggerFuture)
                    continue;
            }

            // Backlogger future is freed elsewhere
            if (future != downlet->backloggerFuture)
                (void)future_free(future); // Can't be being executed
        } // `future` is set

        break;
    }

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
    int         status;
    BacklogSpec spec;

    spec.afterIsSet = mrm_getLastMcastProd(downlet->mrm, spec.after);
    if (!spec.afterIsSet)
        (void)memset(spec.after, 0, sizeof(signaturet));
    (void)memcpy(spec.before, before, sizeof(signaturet));
    spec.timeOffset = getTimeOffset();

    status = up7Proxy_requestBacklog(downlet->up7Proxy, &spec);

    if (status)
        log_add("Couldn't request session backlog");

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
 * Creates a client that's connected to an upstream LDM7 server.
 *
 * @param[in]  downlet        Pointer to one-time, downstream LDM7.
 * @retval     0              Success. `downlet->up7Proxy` and `downlet->sock`
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
downlet_initClient(Downlet* const downlet)
{
    int                     sock;
    struct sockaddr_storage sockAddr;
    int                     status = downlet_getSocket(downlet->servAddr, &sock,
            &sockAddr);

    if (status == LDM7_OK) {
        status = up7Proxy_new(&downlet->up7Proxy, sock,
                (struct sockaddr_in*)&sockAddr);
        if (status) {
            log_add("Couldn't create new proxy for upstream LDM7");
            (void)close(sock);
        }
        else {
            downlet->sock = sock;
        }
    } // `sock` is open

    return status;
}

/**
 * Frees the client-side resources of a one-time, downstream LDM7.
 *
 * @param[in] arg  One-time downstream LDM7
 */
static void
downlet_destroyClient(Downlet* const downlet)
{
    up7Proxy_free(downlet->up7Proxy); // Won't close externally-created socket
    downlet->up7Proxy = NULL;

    (void)close(downlet->sock);
    downlet->sock = -1;
}

/**
 * Initializes a one-time, downstream LDM7.
 *
 * @param[out] downlet        One-time downstream LDM7 to be initialized
 * @param[in]  down7          Parent downstream LDM7. Must exist until
 *                            `downlet_destroy()` returns.
 * @param[in]  servAddr       Pointer to the address of the server from which to
 *                            obtain multicast information, backlog products,
 *                            and products missed by the FMTP layer. Must exist
 *                            until `downlet_destroy()` returns.
 * @param[in]  feed           Feed of multicast group to be received.
 * @param[in]  mcastIface     IP address of interface to use for receiving
 *                            multicast packets. Must exist until
 *                            `downlet_destroy()` returns.
 * @param[in]  vcEnd          Local virtual-circuit endpoint. Must exist until
 *                            `downlet_destroy()` returns.
 * @param[in]  pq             The product-queue. Must be thread-safe (i.e.,
 *                            `pq_getFlags(pq) | PQ_THREADSAFE` must be true).
 *                            Must exist until `downlet_destroy()` returns.
 * @param[in]  mrm            Multicast receiver session memory. Must exist
 *                            until `downlet_destroy()` returns.
 * @retval     0              Success
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 * @see `downlet_destroy()`
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
    int status;

    (void)memset(downlet, 0, sizeof(downlet));

    downlet->down7 = down7;
    downlet->servAddr = servAddr;
    downlet->ifaceName = mcastIface;
    downlet->vcEnd = vcEnd;
    downlet->pq = pq;
    downlet->feedtype = feed;
    downlet->sock = -1;
    downlet->mrm = mrm;
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
            downlet->completer = completer_new();

            if (downlet->completer == NULL) {
                log_add("Couldn't create new completion service");
                status = LDM7_SYSTEM;
                free(downlet->feedId);
            }
            else {
                status = 0;
            }
        } // `downlet->feedId` created

        if (status)
            free(downlet->upId);
    } // `downlet->upId` created

    return status;
}

/**
 * @param[in,out] downlet      One-time downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
downlet_destroy(Downlet* const downlet)
{
    // The backlogger and its future are destroyed here, if necessary
    int status = future_free(downlet->backloggerFuture);
    backlogger_free(downlet->backlogger);

    completer_free(downlet->completer);
    free(downlet->feedId);
    free(downlet->upId);

    return status;
}

/**
 * Receives unicast data-products from the associated upstream LDM7 -- either
 * because they were missed by the multicast LDM receiver or because they are
 * part of the backlog. Doesn't return until `downlet_haltUcastRcvr()` is called
 * or an error occurs. On return the TCP socket will be closed.
 *
 * @param[in] arg      One-time LDM7 unicast receiver
 * @param[out] result  Result object. Ignored.
 * @retval    0        Success
 * @return             Error code. `log_add()` called.
 * @see               `downlet_haltUcastRcvr()`
 */
inline static int
downlet_runUcastRcvr(
        void* const restrict  arg,
        void** const restrict result)
{
    int status = ucastRcvr_run(((Downlet*)arg)->ucastRcvr);

    if (status) {
        log_add("Unicast receiving task completed unsuccessfully");
        log_flush_error();
    }
    else {
        log_clear();
    }

    return status;
}

static int
downlet_haltUcastRcvr(
        void* const arg,
        pthread_t   thread)
{
    ucastRcvr_halt(((Downlet*)arg)->ucastRcvr, thread);

    return 0;
}

/**
 * Creates a receiver of unicast data-products from the associated upstream
 * LDM7. Executes the receiver on a separate thread. The products were either
 * missed by the FMTP layer or they are part of the backlog. Blocks until the
 * receiver has started.
 * @param[in]     downlet      Parent one-time, downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `downlet_destroyUcastRcvr()`
 */
static int
downlet_createUcastRcvr(Downlet* const downlet)
{
    int         status;
    static char id[] = "one-time, LDM7 unicast receiver";

    UcastRcvr* const ucastRcvr = ucastRcvr_new(downlet->sock, downlet);

    if (ucastRcvr == NULL) {
        log_add("Couldn't create %s", id);
        status = LDM7_SYSTEM;
    }
    else {
        downlet->ucastRcvr = ucastRcvr;

        if (completer_submit(downlet->completer, downlet, downlet_runUcastRcvr,
                downlet_haltUcastRcvr) == NULL) {
            log_add("Couldn't submit %s for execution", id);
            ucastRcvr_free(ucastRcvr);
            status = LDM7_SYSTEM;
        }
        else {
            status = 0; // Success
        }
    } // `ucastRcvr` created

    return status;
}

static int
downlet_runBackstop(
        void* const restrict  arg,
        void** const restrict result)
{
    int status = backstop_run(((Downlet*)arg)->backstop);

    if (status) {
        log_add("Backstop task completed unsuccessfully");
        log_flush_error();
    }
    else {
        log_clear();
    }

    return status;
}

static int
downlet_haltBackstop(
        void* const     arg,
        const pthread_t thread)
{
    return backstop_halt(((Downlet*)arg)->backstop, thread);
}

static int
downlet_createBackstop(Downlet* const downlet)
{
    int         status;
    static char id[] = "one-time, LDM7 backstop";

    Backstop* const backstop = backstop_new(downlet->up7Proxy, downlet->mrm,
            downlet);

    if (backstop == NULL) {
        log_add("Couldn't create %s", id);
        status = LDM7_SYSTEM;
    }
    else {
        downlet->backstop = backstop;

        if (completer_submit(downlet->completer, downlet, downlet_runBackstop,
                downlet_haltBackstop) == NULL) {
            log_add("Couldn't submit %s for execution", id);
            backstop_free(backstop);
            status = LDM7_SYSTEM;
        }
        else {
            status = 0; // Success
        }
    } // `backstop` created

    return status;
}

static int
downlet_runMcastRcvr(
        void* const restrict  arg,
        void** const restrict result)
{
    int status = mcastRcvr_run(((Downlet*)arg)->mcastRcvr);

    if (status) {
        log_add("Multicast receiving task completed unsuccessfully");
        log_flush_error();
    }
    else {
        log_clear();
    }

    return status;
}

static int
downlet_haltMcastRcvr(
        void* const     arg,
        const pthread_t thread)
{
    return mcastRcvr_halt(((Downlet*)arg)->mcastRcvr, thread);
}

/**
 * Creates an asynchronous task that receives multicast data-products from the
 * associated upstream LDM7. Executes the receiver on a separate thread.
 *
 * @param[in]     downlet      Parent one-time, downstream LDM7
 * @retval        0            Success
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `downlet_destroyMcastRcvr()`
 */
static int
downlet_createMcastRcvr(
        Downlet* const restrict downlet)
{
    static char id[] = "one-time, LDM7 multicast receiver";
    int         status;

    McastRcvr* const mcastRcvr = mcastRcvr_new(downlet->mcastInfo,
            downlet->ifaceName, downlet->ifaceAddr, downlet->pq, downlet);

    if (mcastRcvr == NULL) {
        log_add("Couldn't create %s", id);
        status = LDM7_SYSTEM;
    }
    else {
        downlet->mcastRcvr = mcastRcvr;

        if (completer_submit(downlet->completer, downlet, downlet_runMcastRcvr,
                downlet_haltMcastRcvr) == NULL) {
            log_add("Couldn't submit %s for execution", id);
            status = LDM7_SYSTEM;
        }
        else {
            status = 0;
        }

        if (status)
            mcastRcvr_free(mcastRcvr);
    } // `mcastRcvr_new()` success

    return status;
}

static int
downlet_runBacklogger(
        void* const restrict  arg,
        void** const restrict result)
{
    int status = backlogger_run(((Downlet*)arg)->backlogger);

    if (status) {
        log_add("Backlog requesting task completed unsuccessfully");
        log_flush_error();
    }
    else {
        log_clear();
    }

    return status;
}

static int
downlet_haltBacklogger(
        void* const     arg,
        const pthread_t thread)
{
    return backlogger_halt(((Downlet*)arg)->backlogger, thread);
}

static int
downlet_createBacklogger(
        Downlet* const   downlet,
        const signaturet before)
{
    int         status;
    static char id[] = "one-time, downstream LDM7 backlog requester";

    downlet->backlogger = backlogger_new(before, downlet);

    if (downlet->backlogger == NULL) {
        log_add("Couldn't create %s", id);
        status = LDM7_SYSTEM;
    }
    else {
        downlet->backloggerFuture = completer_submit(downlet->completer,
                downlet, downlet_runBacklogger, downlet_haltBacklogger);

        if (downlet->backloggerFuture == NULL) {
            log_add("Couldn't submit %s for execution", id);
            backlogger_free(downlet->backlogger);
            downlet->backlogger = NULL;
            status = LDM7_SYSTEM;
        }
        else {
            status = 0; // Success
        }
    } // `downlet->backlogger` created

    return status;
}

/**
 * Destroys the asynchronous subtasks of a one-time, downstream LDM7.
 *
 * @param[in,out] downlet  One-time downstream LDM7
 * @retval        0        Success
 * @retval        ENOMEM   Out of memory
 */
static int
downlet_destroyTasks(Downlet* const downlet)
{
    int status = completer_shutdown(downlet->completer, true);

    if (status) {
        log_add("Couldn't shut down completion service");
    }
    else {
        for (Future* future; (future = completer_take(downlet->completer)); ) {
            // The backogger future is freed elsewhere
            if (future != downlet->backloggerFuture) {
                /*
                 * The task didn't allocate a result object; therefore, no
                 * memory leak can occur. Also, the task was canceled;
                 * therefore, it has completed and its future may be safely
                 * freed.
                 */
                status = future_free(future);
                log_assert(status == 0);
            }
        }

        mcastRcvr_free(downlet->mcastRcvr);
        backstop_free(downlet->backstop);
        ucastRcvr_free(downlet->ucastRcvr);
    }

    return status;
}

/**
 * Starts the asynchronous subtasks of a downstream LDM7 that collectively
 * receive data-products. Blocks until all subtasks have started. The tasks are:
 * - Multicast data-product receiver
 * - Missed data-product (i.e., "backstop") requester
 * - Unicast data-product receiver
 *
 * NB: The task to received data-products missed since the end of the previous
 * session (i.e., "backlogger") is created elsewhere
 *
 * @param[in]  down7          Pointer to the downstream LDM7.
 * @retval     0              Success.
 * @retval     LDM7_SHUTDOWN  The LDM7 has been shut down. No task is running.
 * @retval     LDM7_SYSTEM    Error. `log_add()` called. No task is running.
 */
static int
downlet_createTasks(Downlet* const downlet)
{
    int status = downlet_createUcastRcvr(downlet);

    if (status == 0)
        status = downlet_createBackstop(downlet);

    if (status == 0)
        status = downlet_createMcastRcvr(downlet);

    if (status)
        (void)downlet_destroyTasks(downlet);

    return status;
}

/**
 * Executes a one-time, downstream LDM7. Doesn't return until an error occurs.
 *
 * @param[in,out] downlet        One-time downstream LDM7
 * @retval        0              `downlet_halt()` called.
 * @retval        LDM7_INTR      Signal caught. `log_add()` called.
 * @retval        LDM7_INVAL     Invalid port number or host identifier.
 *                               `log_add()` called.
 * @retval        LDM7_LOGIC     Logic error. `log_add()` called.
 * @retval        LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval        LDM7_RPC       RPC failure (including interrupt). `log_add()`
 *                               called.
 * @retval        LDM7_SYSTEM    System error. `log_add()` called.
 * @retval        LDM7_NOENT     The upstream LDM7 doesn't multicast `feed`.
 *                               `log_add()` called.
 * @retval        LDM7_TIMEDOUT  Subscription request timed-out. `log_add()`
 *                               called.
 * @retval        LDM7_REFUSED   Upstream host refused connection (LDM7 not
 *                               running?). `log_add()` called.
 * @retval        LDM7_UNAUTH    Upstream LDM7 denied request. `log_add()` called.
 */
static Ldm7Status
downlet_run(Downlet* const downlet)
{
    int status;

    log_notice_q("Downstream LDM7 starting up: remoteLDM7=%s, feed=%s, "
            "pq=\"%s\"", downlet->upId, downlet->feedId,
            pq_getPathname(downlet->pq));

    // Sets `downlet->up7Proxy` and `downlet->sock`
    status = downlet_initClient(downlet);

    if (status) {
        log_add("Couldn't create client for feed %s from server %s",
                downlet->feedId, downlet->upId);
    }
    else {
        /*
         * Blocks until error, reply, or timeout. Sets `downlet->mcastInfo` and
         * `downlet->fmtpAddr`.
         */
        status = up7Proxy_subscribe(downlet->up7Proxy, downlet->feedtype,
                downlet->vcEnd, &downlet->mcastInfo, &downlet->ifaceAddr);

        if (status) {
            log_add("Couldn't subscribe to feed %s from %s", downlet->feedId,
                    downlet->upId);
        }
        else {
            char* const miStr = mi_format(downlet->mcastInfo);
            const char  ifaceAddrStr[INET_ADDRSTRLEN];

            (void)inet_ntop(AF_INET, &downlet->ifaceAddr, ifaceAddrStr,
                    sizeof(ifaceAddrStr));
            log_notice_q("Subscription reply from %s: mcastGroup=%s, "
                    "ifaceAddr=%s", downlet->upId, miStr, ifaceAddrStr);
            free(miStr);

            status = downlet_createTasks(downlet);

            if (status) {
                log_add("Error starting subtasks for feed %s from %s",
                        downlet->feedId, downlet->upId);
            }
            else {
                status = downlet_waitForStatusChange(downlet);

                (void)downlet_destroyTasks(downlet);
            } // Subtasks started

            mi_free(downlet->mcastInfo); // NULL safe
        } // `downlet->mcastInfo` set

        downlet_destroyClient(downlet);
    } // Client created

    return status;
}

/**
 * Halts an executing, one-time, downstream LDM7.
 *
 * @param[in,out] downlet  One-time downstream LDM7
 * @retval        0        Success
 * @retval        ENOMEM   Out of memory
 * @threadsafety           Safe
 */
static int
downlet_halt(Downlet* const downlet)
{
    return completer_shutdown(downlet->completer, true);
}

static Ldm7Status
downlet_testConnection(Downlet* const downlet)
{
    return up7Proxy_testConnection(downlet->up7Proxy);
}

/**
 * Increments the number of data-products successfully inserted into the
 * product-queue by a downstream LDM7. Called by the multicast LDM receiver.
 *
 * @param[in] downlet  One-time, downstream LDM7
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
 * The first time this function is called for a given one-time, downstream LDM7,
 * it starts a subtask that requests the backlog of data-products that were
 * missed due to the passage of time from the end of the previous session to the
 * reception of the first multicast data-product.
 *
 * @param[in] downlet  Pointer to the one-time, downstream LDM7.
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
            (void)downlet_destroyTasks(downlet);
        }
    }
}

/******************************************************************************
 * Downstream LDM7
 ******************************************************************************/

/**
 * The data structure of a downstream LDM7:
 */
struct down7 {
    Downlet               downlet;       ///< One-time, downstream LDM7
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
    pthread_mutex_t       numProdMutex;     ///< Mutex for number of products
    Executor*             executor;         ///< One-time LDM7 execution-service
    uint64_t              numProds;         ///< Number of inserted products
    feedtypet             feedtype;         ///< Feed of multicast group
    VcEndPoint            vcEnd;            ///< Local virtual-circuit endpoint
    Ldm7Status            status;           ///< Downstream LDM7 status
    volatile bool         mcastWorking;     ///< Product received via multicast?
    bool                  prevLastMcastSet; ///< `prevLastMcast` set?
    StopFlag              stopFlag;
};

/**
 * Creates the thread-specific data-key for the pointer to the one-time,
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
 *                          until `down7_destroy()` returns.
 * @param[in]  vcEnd        Local virtual-circuit endpoint. Caller may free.
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
    if (!(pq_getFlags(pq) & PQ_THREADSAFE)) {
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
            down7->iface = strdup(mcastIface);

            if (down7->iface == NULL) {
                log_add("Couldn't copy multicast interface specification");
                status = LDM7_SYSTEM;
            }
            else {
                if (!vcEndPoint_copy(&down7->vcEnd, vcEnd)) {
                    log_add("Couldn't copy receiver-side virtual-circuit "
                            "endpoint");
                    status = LDM7_SYSTEM;
                }
                else {
                    status = mutex_init(&down7->numProdMutex,
                            PTHREAD_MUTEX_ERRORCHECK, true);

                    if (status) {
                        log_add_errno(status,
                                "Couldn't initialize number-of-products mutex");
                        status = LDM7_SYSTEM;
                    }
                    else {
                        if ((status = pthread_once(&down7KeyControl,
                                down7_createThreadKey)) != 0) {
                            log_add_errno(status, "Couldn't create downstream "
                                    "LDM7 thread-key");
                            status = LDM7_SYSTEM;
                        }
                        else {
                            status = stopFlag_init(&down7->stopFlag);

                            if (status) {
                                log_add("Couldn't initialize stop flag");
                            }
                            else {
                                down7->executor = executor_new();

                                if (down7->executor == NULL) {
                                    log_add("Couldn't create execution-service"
                                            "for one-time, downstream LDM7");
                                    status = LDM7_SYSTEM;
                                    stopFlag_destroy(&down7->stopFlag);
                                }
                                else {
                                    status = 0; // Success
                                }
                            } // Stop flag initialized

                            /*
                             * The Downstream LDM7 thread-key isn't deleted
                             * because it might have been created by a previous
                             * call of this function.
                             */
                        } // Downstream LDM7 thread-key created

                        if (status)
                            pthread_mutex_destroy(&down7->numProdMutex);
                    } // `down7->numProdMutex` initialized

                    if (status)
                        vcEndPoint_destroy(&down7->vcEnd);
                } // `down7->vcEnd` initialized

                if (status)
                    free(down7->iface);
            } // `down7->iface` initialized

            if (status)
                sa_free(down7->servAddr);
        } // `down7->servAddr` initialized
    } // Product-queue is thread-safe

    return status;
}

static void
down7_destroy(Down7* const down7)
{
    executor_free(down7->executor);
    stopFlag_destroy(&down7->stopFlag);
    /*
     * `down7Key` is not deleted because it might not have been created during
     * the creation of `down7`.
     */
    pthread_mutex_destroy(&down7->numProdMutex);
    vcEndPoint_destroy(&down7->vcEnd);
    free(down7->iface);
    sa_free(down7->servAddr);
}

/**
 * Executes the one-time, downstream LDM7 of a downstream LDM7.
 *
 * @param[in]  arg            Downstream LDM7
 * @param[out] result         Result object. Ignored.
 * @retval     0              Success
 * @retval     LDM7_INTR      Signal caught. `log_add()` called.
 * @retval     LDM7_INVAL     Invalid port number or host identifier.
 *                            `log_add()` called.
 * @retval     LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval     LDM7_NOENT     The upstream LDM7 doesn't multicast `feed`.
 *                            `log_add()` called.
 * @retval     LDM7_RPC       RPC failure (including interrupt). `log_add()`
 *                            called.
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 * @retval     LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval     LDM7_REFUSED   Remote host refused connection (server likely
 *                            isn't running). `log_add()` called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. `log_add()` called.
 * @retval     LDM7_UNAUTH    Not authorized. `log_add()` called.
 */
static int
down7_runDownlet(
        void* const restrict  arg,
        void** const restrict result)
{
    Down7* const down7 = (Down7*)arg;
    int          status = downlet_init(&down7->downlet, down7, down7->servAddr,
            down7->feedtype, down7->iface, &down7->vcEnd, down7->pq,
            down7->mrm);

    if (status) {
        log_add("Couldn't initialize one-time, downstream LDM7");
    }
    else {
        status = downlet_run(&down7->downlet);

        downlet_destroy(&down7->downlet);
    } // `down7->downlet` initialized

    /**
     * Flush all log messages because this is the end of an asynchronous task.
     */
    if (status == LDM7_TIMEDOUT) {
        log_flush_info();
    }
    else if (status == LDM7_INTR) {
        log_add("One-time, downstream LDM7 was interrupted");
        log_flush_info();
    }
    else if (status == LDM7_NOENT || status == LDM7_REFUSED ||
            status == LDM7_UNAUTH) {
        log_flush_warning();
    }
    else if (status) {
        log_add("Error executing one-time, downstream LDM7: status=%d", status);
        log_flush_error();
    }
    else {
        log_clear();
    }

    return status;
}

/**
 * Halts the one-time, downstream LDM7 of a downstream LDM7. The execution
 * service should only call this function if it has already called
 * `down7_runDownlet()`; consequently, we can depend on `down7->downlet` being
 * initialized.
 *
 * @param[in] arg      Downstream LDM7
 * @retval    0        Success
 * @retval    ENOMEM   Out of memory
 */
static int
down7_haltDownlet(
        void* const restrict  arg,
        pthread_t             thread)
{
    Down7* const down7 = (Down7*)arg;
    int          status = downlet_halt(&down7->downlet);

    if (status)
        log_add("Couldn't halt one-time, downstream LDM7");

    return status;
}

/**
 * Returns a new downstream LDM7. The instance doesn't receive anything until
 * `down7_run()` is called.
 *
 * @param[in] servAddr    Address of the server from which to obtain multicast
 *                        information, backlog products, and products missed by
 *                        the FMTP layer. Caller may free.
 * @param[in] feedtype    Feedtype of multicast group to receive.
 * @param[in] mcastIface  IP address of interface to use for receiving multicast
 *                        packets. Caller may free.
 * @param[in] vcEnd       Local virtual-circuit endpoint. Caller may free.
 * @param[in] pq          The product-queue. Must be thread-safe (i.e.,
 *                        `pq_getFlags(pq) | PQ_THREADSAFE` must be true).
 * @param[in] mrm         Persistent multicast receiver memory. Must exist until
 *                        `down7_free()` returns.
 * @retval    NULL        Failure. `log_add()` called.
 * @return                Pointer to the new downstream LDM7. Caller should call
 *                        `down7_free()` when it's no longer needed.
 * @see `down7_run()`
 * @see `down7_free()`
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
void
down7_free(Down7* const down7)
{
    log_debug_1("Entered");

    if (down7) {
        down7_destroy(down7);
        free(down7);
    }
}

/**
 * Returns the product-queue associated with a downstream LDM7.
 *
 * @param[in] downlet  One-time, downstream LDM7.
 * @return             Associated product-queue.
 */
pqueue* down7_getPq(
        Downlet* const downlet)
{
    return downlet->pq;
}

/**
 * Executes a downstream LDM7. Doesn't return unless a severe error occurs or
 * `down7_halt()` is called.
 *
 * @param[in,out] arg          Downstream LDM7
 * @retval        0            `down7_halt()` called
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
        Future* const future = executor_submit(down7->executor, down7,
            down7_runDownlet, down7_haltDownlet);

        if (future == NULL) {
            log_add("Can't execute downstream LDM7 once");
            status = LDM7_SYSTEM;
        }
        else {
            status = future_getResult(future, NULL); // No result object

            if (status == ECANCELED) {
                status = 0; // `down7_halt()` was called; `down7->stopFlag` set
            }
            else if (status == EPERM) {
                // One-time, downstream LDM7 returned non-zero status
                status = future_runFuncStatus(future);

                if (status == LDM7_TIMEDOUT) {
                    status = 0; // Try again immediately
                }
                else if (status == LDM7_REFUSED || status == LDM7_NOENT ||
                        status == LDM7_UNAUTH) {
                    // Problem might be temporary
                    struct timespec when;
                    when.tv_sec = time(NULL) + interval;
                    when.tv_nsec = 0;
                    stopFlag_timedWait(&down7->stopFlag, &when);
                    status = 0; // Try again
                }
                else {
                    log_add("Downstream LDM7 failed: status=%d", status);
                }
            } // One-time, downstream LDM7 returned non-zero status
            else if (status) {
                log_add("Couldn't get result of executing downstream LDM7 "
                        "once");
                status = LDM7_SYSTEM;
            }
            else {
                stopFlag_set(&down7->stopFlag); // Just in case
            }

            (void)future_free(future); // Can't be being executed
        } // `future` created
    } // One-time, downstream LDM7 execution loop

    return status;
}

/**
 * Halts a downstream LDM7 that's executing on another thread.
 *
 * @param[in] down7   Downstream LDM7
 * @param[in] thread  Thread on which downstream LDM7 is executing
 */
void
down7_halt(
        Down7* const    down7,
        const pthread_t thread)
{
    int status = executor_shutdown(down7->executor, true);
    log_assert(status == 0);

    stopFlag_set(&down7->stopFlag);
}

/**
 * Queues a data-product for being requested via the LDM7 backstop mechanism.
 * Called by `up7_down7_test.c`. The multicast LDM receiver uses
 * `downlet_missedProduct()`, instead.
 *
 * @param[in] down7  Downstream LDM7
 * @param[in] iProd  Index of the missed FMTP product.
 */
void
down7_requestProduct(
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
    (void)mutex_lock(&down7->numProdMutex);
        down7->numProds++;
    (void)mutex_unlock(&down7->numProdMutex);
}

/**
 * Returns the number of data-products successfully inserted into the product-
 * queue by a downstream LDM7.
 *
 * @param[in] down7  The downstream LDM7.
 * @return           The number of successfully inserted data-products.
 */
uint64_t
down7_getNumProds(Down7* const down7)
{
    (void)mutex_lock(&down7->numProdMutex);
        uint64_t num = down7->numProds;
    (void)mutex_unlock(&down7->numProdMutex);

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

/******************************************************************************
 * Downstream LDM7 RPC server functions
 ******************************************************************************/

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
    log_debug_1("Entered");

    prod_info* const info = &missedProd->prod.info;
    Downlet* const   downlet = pthread_getspecific(down7Key);
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
    Downlet* const downlet = pthread_getspecific(down7Key);
    FmtpProdIndex  iProd;

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
    char           saStr[512];
    Downlet* const downlet = pthread_getspecific(down7Key);

    log_notice_q("All backlog data-products received: feed=%s, server=%s",
            s_feedtypet(downlet->feedtype),
            sa_snprint(downlet->servAddr, saStr, sizeof(saStr)));

    return NULL; // causes RPC dispatcher to not reply
}
