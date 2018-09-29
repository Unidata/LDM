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
 *
 * Requirements:
 *     - SIGTERM must terminate the process in which this runs
 */

#include "config.h"

#include "AtomicInt.h"
#include "ChildCommand.h"
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
 * Proxy for an upstream LDM7
 ******************************************************************************/

/**
 * Data-structure of a thread-safe proxy for an upstream LDM7 associated with a
 * downstream LDM7.
 */
typedef struct {
    char*                 remoteId; ///< Socket address of upstream LDM7
    CLIENT*               clnt;     ///< Client-side RPC handle
    pthread_mutex_t       mutex;    ///< Because accessed by multiple threads
} Up7Proxy;
static Up7Proxy up7Proxy;

/**
 * Locks the upstream LDM7 proxy for exclusive access.
 *
 * @pre                   `proxy->clnt != NULL`
 */
static void
up7Proxy_lock()
{
    log_debug("up7Proxy_lock() entered");
    int status = pthread_mutex_lock(&up7Proxy.mutex);
    log_assert(status == 0);
}

/**
 * Unlocks the upstream LDM7 proxy.
 */
static void
up7Proxy_unlock()
{
    log_debug("up7Proxy_unlock() entered");
    int status = pthread_mutex_unlock(&up7Proxy.mutex);
    log_assert(status == 0);
}

// Forward declaration
static Ldm7Status
downlet_testConnection();

static int
up7Proxy_init(const int           socket,
        struct sockaddr_in* const sockAddr)
{
    int status;

    if (socket < 0 || sockAddr == NULL || sockAddr->sin_family != AF_INET) {
        status = LDM7_INVAL;
    }
    else {
        status = mutex_init(&up7Proxy.mutex, PTHREAD_MUTEX_ERRORCHECK, true);

        if (status) {
            log_add_errno(status, "Couldn't initialize mutex");
            status = LDM7_SYSTEM;
        }
        else {
            up7Proxy.remoteId = sockAddrIn_format(sockAddr);

            if (up7Proxy.remoteId == NULL) {
                log_add("Couldn't format socket address of upstream LDM7");
                status = LDM7_SYSTEM;
            }
            else {
                // `log_assert(status == 0)`

                int sock = socket;
                up7Proxy.clnt = clnttcp_create(sockAddr, LDMPROG, SEVEN, &sock, 0,
                        0);

                if (up7Proxy.clnt == NULL) {
                    log_add_syserr("Couldn't create RPC client for %s: %s",
                            up7Proxy.remoteId);
                    status = LDM7_RPC;
                    free(up7Proxy.remoteId);
                }
            } // `up7Proxy.remoteId` allocated

            if (status)
                pthread_mutex_destroy(&up7Proxy.mutex);
        } // `up7Proxy.mutex` initialized
    } // Non-NULL input arguments

    return status;
}

static void
up7Proxy_destroy(void)
{
    up7Proxy_lock();
        /*
         * Destroys *and* frees `up7Proxy.clnt`. Won't close externally-created
         * socket.
         */
        clnt_destroy(up7Proxy.clnt);
        up7Proxy.clnt = NULL;
        free(up7Proxy.remoteId);
    up7Proxy_unlock();

    (void)pthread_mutex_destroy(&up7Proxy.mutex);
}

/**
 * Subscribes to an upstream LDM7 server. This is a potentially length
 * operation.
 *
 * @param[in]  feed           Feed specification.
 * @param[in]  vcEnd          Local virtual-circuit endpoint
 * @param[out] mcastInfo      Information on the multicast group corresponding
 *                            to `feed`.
 * @param[out] ifaceAddr      IP address of VLAN virtual interface
 * @retval     0              If and only if success. `*mcastInfo` is set. The
 *                            caller should call `mi_free(*mcastInfo)` when
 *                            it's no longer needed.
 * @retval     LDM7_SHUTDOWN  Shutdown requested
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
        feedtypet                        feed,
        const VcEndPoint* const restrict vcEnd,
        McastInfo** const restrict       mcastInfo,
        in_addr_t* const restrict        ifaceAddr)
{
    int status;

    up7Proxy_lock();
        CLIENT* const clnt = up7Proxy.clnt;

        McastSubReq   request;
        request.feed = feed;
        request.vcEnd = *vcEnd;

        /*
         * WARNING: If a standard RPC implementation is used, then it is likely
         * that `subscribe_7()` won't return when `SIGTERM` is received because
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
                *mcastInfo = mi_clone(
                        &reply->SubscriptionReply_u.info.mcastInfo);
                *ifaceAddr = cidrAddr_getAddr(
                        &reply->SubscriptionReply_u.info.fmtpAddr);
            }
            xdr_free(xdr_SubscriptionReply, (char*)reply);
        }
    up7Proxy_unlock();

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
 * @param[in] spec      Specification of backlog
 * @retval    0         Success.
 * @retval    LDM7_RPC  Error in RPC layer. `log_add()` called.
 */
static int
up7Proxy_requestBacklog(
    BacklogSpec* const restrict spec)
{
    int status;

    up7Proxy_lock();
        CLIENT* const clnt = up7Proxy.clnt;

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
    up7Proxy_unlock();

    return status;
}

/**
 * Requests a data-product that was missed by the multicast LDM receiver.
 *
 * @param[in] iProd       FMTP product-ID of missed data-product.
 * @retval    0           Success. A data-product was requested.
 * @retval    LDM7_RPC    RPC error. `log_add()` called.
 */
static int
up7Proxy_requestProduct(const FmtpProdIndex iProd)
{
    int status;

    up7Proxy_lock();
        CLIENT* clnt = up7Proxy.clnt;

        log_debug("iProd=%lu", (unsigned long)iProd);

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
    up7Proxy_unlock();

    return status;
}

/**
 * Tests the connection to an upstream LDM7 by sending a no-op/no-reply message
 * to it. Doesn't block.
 *
 * @retval    0         Success. The connection is still good.
 * @retval    LDM7_RPC  RPC error. `log_add()` called.
 */
static int
up7Proxy_testConnection()
{
    int status;

    up7Proxy_lock();

    test_connection_7(NULL, up7Proxy.clnt);

    if (clnt_stat(up7Proxy.clnt) == RPC_TIMEDOUT) {
        /*
         * "test_connection_7()" uses asynchronous message-passing, so the
         * status will always be RPC_TIMEDOUT unless an error occurs.
         */
        status = 0;
    }
    else {
	log_add("test_connection_7() failure: %s", clnt_errmsg(up7Proxy.clnt));
        status = LDM7_RPC;
    }

    up7Proxy_unlock();

    return status;
}

/******************************************************************************
 * Forward declaration
 ******************************************************************************/

static void
downlet_taskTerminated(const Ldm7Status status);

/******************************************************************************
 * Requester of Data-Products Missed by the FMTP Layer:
 ******************************************************************************/

typedef struct {
    pthread_mutex_t       mutex;     ///< Mutex
    pthread_t             thread;    ///< `backstop_run()` thread
    McastReceiverMemory*  mrm;       ///< Persistent multicast receiver memory
    signaturet            prevLastMcast;    ///< Previous session's Last prod
    bool                  prevLastMcastSet; ///< `prevLastMcast` set?
} Backstop;
static Backstop backstop;

/**
 * Initializes the backstop of the one-time, downstream LDM7 missed-product
 *
 * @param[in] mrm          Multicast receiver memory. Must exist until
 *                         `backstop_destroy()` returns.
 * @retval    0            Success
 * @see `backstop_destroy()`
 */
static int
backstop_init(McastReceiverMemory* const mrm)
{
    backstop.mrm = mrm;
    backstop.prevLastMcastSet = mrm_getLastMcastProd(backstop.mrm,
            backstop.prevLastMcast);

    return 0;
}

static void
backstop_destroy()
{
    (void)pthread_mutex_destroy(&backstop.mutex);
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
 * @param[in] arg            Ignored
 * @retval    NULL           Always
 * @see `backstop_stop()`
 */
static void*
backstop_run(void* const arg)
{
    int status;

    for (;;) {
        /*
         * The semantics and order of the following actions are necessary to
         * preserve the meaning of the two queues and to ensure that all missed
         * data-products are received following a restart.
         */
        FmtpProdIndex iProd;

        if (!mrm_peekMissedFileWait(backstop.mrm, &iProd)) {
            log_debug("The queue of missed data-products has been shutdown");
            status = LDM7_SHUTDOWN;
            break;
        }
        else {
            if (!mrm_addRequestedFile(backstop.mrm, iProd)) {
                log_add("Couldn't add FMTP product-index to requested-queue");
                status = LDM7_SYSTEM;
                break;
            }
            else {
                /* The queue can't be empty */
                (void)mrm_removeMissedFileNoWait(backstop.mrm, &iProd);

                status = up7Proxy_requestProduct(iProd);

                if (status) {
                    log_add("Couldn't request product");
                    break;
                }
            } // product-index added to requested-but-not-received queue
        } // have peeked-at product-index from missed-but-not-requested queue
    }

    log_flush_error(); // Just in case
    log_free();        // Because end of thread
    downlet_taskTerminated(status);

    return NULL;
}

/**
 * Creates a concurrent task that requests data-products that were missed by
 * the FMTP layer.
 *
 * @param[in,out] mrm   Persistent multicast receiver memory
 * @retval 0            Success
 * @retval LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `backstop_stop()`
 */
static Ldm7Status
backstop_start(McastReceiverMemory* const mrm)
{
    int status = backstop_init(mrm);

    if (status) {
        log_add("Couldn't initialize backstop");
    }
    else {
        status = pthread_create(&backstop.thread, NULL, backstop_run, NULL);

        if (status) {
            log_add("Couldn't create thread for backstop");
            backstop_destroy();
            status = LDM7_SYSTEM;
        }
    } // Backstop initialized

    return status;
}

/**
 * Stops the backstop concurrent task.
 *
 * @asyncsignalsafety  Unsafe
 */
static void
backstop_stop(void)
{
    mrm_shutDownMissedFiles(backstop.mrm);
    (void)pthread_join(backstop.thread, NULL);
}

/******************************************************************************
 * Receiver of unicast products from an upstream LDM7
 ******************************************************************************/

typedef struct {
    pthread_t             thread;
    pthread_mutex_t       mutex;
    SVCXPRT*              xprt;
    char*                 remoteStr;
} UcastRcvr;
static UcastRcvr ucastRcvr;

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
 * Initializes the concurrent unicast receiver task.
 *
 * @param[in] sock         Unicast socket with upstream LDM7
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 * @retval    LDM7_RPC     RPC error. `log_add()` called.
 * @see `ucastRcvr_stop()`
 */
static Ldm7Status
ucastRcvr_init(const int sock)
{
    ucastRcvr.xprt = NULL;
    ucastRcvr.remoteStr = NULL;

    int status = ucastRcvr_createXprt(sock, &ucastRcvr.xprt);

    if (status) {
        log_add("Couldn't create server-side transport on socket %d", sock);
    }
    else {
        ucastRcvr.remoteStr = ipv4Sock_getPeerString(sock);

        if (ucastRcvr.remoteStr == NULL) {
            log_add("Couldn't get ID of remote peer");
            status = LDM7_SYSTEM;
        }
        else {
            // Last argument == 0 => don't register with portmapper
            if (!svc_register(ucastRcvr.xprt, LDMPROG, SEVEN, ldmprog_7, 0)) {
                log_add("Couldn't register server for receiving data-products "
                        "from \"%s\"",  ucastRcvr.remoteStr);
                status = LDM7_RPC;
            }
            else {
                status = mutex_init(&ucastRcvr.mutex, PTHREAD_MUTEX_ERRORCHECK,
                        true);

                if (status) {
                    status = LDM7_SYSTEM;
                    svc_unregister(LDMPROG, SEVEN);
                }
            } // LDM7 service registered with RPC layer

            if (status) {
                free(ucastRcvr.remoteStr);
                ucastRcvr.remoteStr = NULL;
            }
        } // `ucastRcvr.remoteStr` set

        if (status) {
            svc_destroy(ucastRcvr.xprt);
            ucastRcvr.xprt = NULL;
        }
    } // `ucastRcvr.xprt` initialized

    return status;
}

/**
 * Destroys the unicast receiver concurrent task.
 *
 * @asyncsignalsafety  Unsafe
 * @see `ucastRcvr_init()`
 */
static void
ucastRcvr_destroy(void)
{
    mutex_destroy(&ucastRcvr.mutex);
    svc_unregister(LDMPROG, SEVEN);
    free(ucastRcvr.remoteStr);

    // Transport might have been destroyed by `ucastRcvr_run()`
    if (ucastRcvr.xprt)
        svc_destroy(ucastRcvr.xprt);
}

/**
 * Runs the RPC-based server of a downstream LDM7. *Might* destroy and
 * unregister the service transport. Doesn't return until `ucastRcvr_stop()` is
 * called or an error occurs
 *
 * Called by `pthread_create()`.
 *
 * @param[in]     arg            Ignored
 */
static void*
ucastRcvr_run(void* const restrict arg)
{
    int           status = 0;
    const int     sock = ucastRcvr.xprt->xp_sock;
    struct pollfd pfd;
    const int     timeout = interval * 1000; // Probably 30 seconds

    pfd.fd = sock;
    pfd.events = POLLIN;

    log_info("Starting unicast receiver: sock=%d, timeout=%d ms", sock,
            timeout);

    for (;;) {
        // log_debug_1("Calling poll(): socket=%d", sock); // Excessive output
        status = poll(&pfd, 1, timeout);
        if (0 == status) {
            // Timeout
            continue;
        }

        if (status < 0) {
            log_add_syserr("poll() failure on socket %d to upstream LDM7 "
                    "%s", sock, ucastRcvr.remoteStr);
            status = LDM7_SYSTEM;
            break;
        }

        if (pfd.revents & POLLERR) {
            log_add("Error on socket %d to upstream LDM7 %s", sock,
                    ucastRcvr.remoteStr);
            status = LDM7_SYSTEM;
            break;
        }

        if (pfd.revents & POLLHUP) {
            log_add_syserr("Socket %d to upstream LDM7 %s was closed", sock,
                    ucastRcvr.remoteStr);
            status = LDM7_SYSTEM;
            break;
        }

        if (pfd.revents & (POLLIN | POLLRDNORM)) {
            /*
             * Processes RPC message. Calls select(). Calls `ldmprog_7()`. Calls
             * `svc_destroy(ucastRcvr.xprt)` on error.
             */
            svc_getreqsock(sock);

            if (!FD_ISSET(sock, &svc_fdset)) {
                // `svc_getreqsock()` destroyed `ucastRcvr.xprt`
                log_add("Connection to upstream LDM7 %s was closed by RPC "
                        "layer", ucastRcvr.remoteStr);
                ucastRcvr.xprt = NULL; // To inform others
                status = LDM7_RPC;
                break;
            }
            else {
                status = 0;
            }
        } // Input is available
    } // `poll()` loop

    log_flush_error(); // Just in case
    log_free(); // Because end of thread
    downlet_taskTerminated(status);

    // Eclipse IDE wants to see a return
    return NULL;
}

/**
 * Starts the concurrent unicast receiver task. Doesn't return until
 * `ucastRcvr_stop()` is called or an error occurs.
 *
 * @param[in] sock         Unicast socket with upstream LDM7
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 * @retval    LDM7_RPC     RPC error. `log_add()` called.
 * @see `ucastRcvr_stop()`
 */
static Ldm7Status
ucastRcvr_start(const int sock)
{
    int status = ucastRcvr_init(sock);

    if (status) {
        log_add("Couldn't initialize unicast receiver");
    }
    else {
        mutex_lock(&ucastRcvr.mutex);
            status = pthread_create(&ucastRcvr.thread, NULL, ucastRcvr_run,
                    NULL);
        mutex_unlock(&ucastRcvr.mutex);

        if (status) {
            log_add("Couldn't create thread for unicast receiver");
            status = LDM7_SYSTEM;
        }
    }

    return status;
}

/**
 * Stops the unicast receiver concurrent task.
 *
 * @asyncsignalsafety  Unsafe
 * @see `ucastRcvr_start()`
 */
static void
ucastRcvr_stop(void)
{
    (void)pthread_kill(ucastRcvr.thread, SIGTERM); // Interrupts `poll()`
    (void)pthread_join(ucastRcvr.thread, NULL);
    ucastRcvr_destroy();
}

/******************************************************************************
 * Submodule for local VLAN interface
 ******************************************************************************/

static const char vlanUtil[] = "vlanUtil";

/**
 * Creates a local VLAN interface. Replaces the relevant routing, address, and
 * link information.
 *
 * @param[in] srvrAddrStr  Dotted-decimal IP address of sending FMTP server
 * @param[in] ifaceName    Name of virtual interface to be created (e.g.,
 *                         "eth0.0") or "dummy"
 * @param[in] ifaceAddr    IP address to be assigned to virtual interface
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @retval    LDM7_INVAL   Invalid argument. `log_add()` called.
 */
static int vlanIface_create(
        const char* const restrict srvrAddrStr,
        const char* const restrict ifaceName,
        const in_addr_t            ifaceAddr)
{
    int  status;

    if (strncmp(ifaceName, "dummy", 5) == 0) {
        log_notice("Ignoring call to create dummy VLAN interface");
        status = 0;
    }
    else {
        char ifaceAddrStr[INET_ADDRSTRLEN];

        // Can't fail
        (void)inet_ntop(AF_INET, &ifaceAddr, ifaceAddrStr,
                sizeof(ifaceAddrStr));

        const char* const cmdVec[] = {vlanUtil, "create", ifaceName,
                ifaceAddrStr, srvrAddrStr, NULL };

        int childStatus;

        status = sudo(cmdVec, &childStatus);

        if (status || childStatus) {
            log_add("Couldn't create local VLAN interface");
            status = LDM7_SYSTEM;
        }
    } // Not a dummy VLAN interface

    return status;
}

/**
 * Destroys a local VLAN interface.
 *
 * @param[in] srvrAddrStr  IP address of sending FMTP server in dotted-decimal
 *                         form
 * @param[in] ifaceName    Name of virtual interface to be destroyed (e.g.,
 *                         "eth0.0") or "dummy.
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System or command failure. `log_add()` called.
 */
static int
vlanIface_destroy(
        const char* const restrict srvrAddrStr,
        const char* const restrict ifaceName)
{
    int status;

    if (strncmp(ifaceName, "dummy", 5) == 0) {
        log_notice("Ignoring call to destroy dummy VLAN interface");
        status = 0;
    }
    else {
        int               childStatus;
        const char* const cmdVec[] = {vlanUtil, "destroy", ifaceName,
                srvrAddrStr, NULL};

        status = sudo(cmdVec, &childStatus);

        if (status || childStatus) {
            log_add("Couldn't destroy local VLAN interface");
            status = LDM7_SYSTEM;
        }
    } // Not a dummy VLAN interface

    return status;
}

/******************************************************************************
 * Receiver of multicast products from an upstream LDM7 (uses the FMTP layer)
 ******************************************************************************/

typedef struct {
    pthread_t       thread;       ///< `mcastRcvr_run()` thread
    Mlr*            mlr;          ///< Multicast LDM receiver
    /// Dotted decimal form of sending FMTP server
    char            fmtpSrvrAddr[INET_ADDRSTRLEN];
    const char*     ifaceName;    ///< VLAN interface to create
} McastRcvr;
static McastRcvr mcastRcvr;

/**
 * Initializes a multicast receiver.
 *
 * @param[in] mcastInfo        Information on multicast group
 * @param[in] ifaceName        Name of VLAN virtual interface to be created
 *                             (e.g., (eth0.0") or "dummy".  Must exist until
 *                             `mcastRcvr_destroy()` returns.
 * @param[in] ifaceAddr        Address to be assigned to VLAN interface
 * @param[in] pq               Product queue
 * @retval    0                Success or `ifaceName` starts with "dummy"
 * @retval    LDM7_INVAL       Invalid address of sending FMTP server.
 *                             `log_add()` called.
 * @retval    LDM7_SYSTEM      System failure. `log_add()` called.
 */
static int
mcastRcvr_init(
        McastInfo* const restrict  mcastInfo,
        const char* const restrict ifaceName,
        const in_addr_t            ifaceAddr,
        pqueue* const restrict     pq)
{
    mcastRcvr.thread = 0;
    mcastRcvr.mlr = NULL;

    const char* fmtpSrvrId = mcastInfo->server.inetId;
    int         status = getDottedDecimal(fmtpSrvrId, mcastRcvr.fmtpSrvrAddr);

    if (status) {
        log_add("Invalid address of sending FMTP server: \"%s\"", fmtpSrvrId);
        status = LDM7_INVAL;
    }
    else {
        status = vlanIface_create(mcastRcvr.fmtpSrvrAddr, ifaceName, ifaceAddr);

        if (status) {
            log_add("Couldn't create VLAN virtual interface");
        }
        else {
            char ifaceAddrStr[INET_ADDRSTRLEN];

            // Can't fail
            (void)inet_ntop(AF_INET, &ifaceAddr, ifaceAddrStr,
                    sizeof(ifaceAddrStr));

            Mlr* mlr = mlr_new(mcastInfo, ifaceAddrStr, pq);

            if (mlr == NULL) {
                log_add("Couldn't create multicast LDM receiver");
                status = LDM7_SYSTEM;
            }
            else {
                mcastRcvr.mlr = mlr;
                mcastRcvr.ifaceName = ifaceName;
            } // `mlr` allocated

            if (status)
                vlanIface_destroy(mcastRcvr.fmtpSrvrAddr, ifaceName);
        } // FMTP VLAN interface created
    } // `fmtpSrvrId` is invalid

    return status;
}

inline static void
mcastRcvr_destroy()
{
    mlr_free(mcastRcvr.mlr);
    mcastRcvr.mlr = NULL;

    if (vlanIface_destroy(mcastRcvr.fmtpSrvrAddr, mcastRcvr.ifaceName))
        log_notice("Couldn't destroy VLAN virtual interface");
}

/**
 * Receives data-products via multicast. Doesn't return until `mcastRcvr_stop()`
 * is called or an error occurs. Calls `downlet_taskTerminated()` on exit.
 *
 * @param[in] arg            Ignored
 * @retval    NULL           Always
 * @see `mcastRcvr_stop()`
 */
static void*
mcastRcvr_run(void* const arg)
{
    log_debug("Entered");

    int status = mlr_run(mcastRcvr.mlr);
    /*
     * LDM7_INVAL     Invalid argument. `log_add()` called.
     * LDM7_MCAST     Multicast error. `log_add()` called.
     * LDM7_SHUTDOWN  Shutdown requested
     */

    log_flush_error(); // Just in case
    log_free();        // Because end of thread
    downlet_taskTerminated(status);

    return NULL;
}

/**
 * Starts the multicast receiver concurrent task.
 *
 * @param[in] mcastInfo   Information on multicast feed
 * @param[in] ifaceName   Name of interface for receiving multicast packets
 *                        (e.g., "eth0.0") or "dummy". Must exist until
 *                        `mcastRcvr_destroy()` returns.
 * @param[in] ifaceAddr   IP address to use for receiving multicast packets
 * @param[in] pq          Output product queue
 * @retval    0           Success
 * @retval    LDM7_INVAL  Invalid address of sending FMTP server. `log_add()`
 *                        called.
 * @retval    LDM7_SYSTEM System failure. `log_add()` called.
 * @see `mcastRcvr_stop()`
 */
static Ldm7Status
mcastRcvr_start(
        McastInfo* const restrict  mcastInfo,
        const char* const restrict ifaceName,
        const in_addr_t            ifaceAddr,
        pqueue* const restrict     pq)
{
    int status = mcastRcvr_init(mcastInfo, ifaceName, ifaceAddr, pq);

    if (status) {
        log_add("Couldn't initialize multicast receiver");
    }
    else {
        status = pthread_create(&mcastRcvr.thread, NULL, mcastRcvr_run,
                NULL);

        if (status) {
            log_add("Couldn't create thread for multicast receiver");
            mcastRcvr_destroy();
            status = LDM7_SYSTEM;
        }
    } // Multicast receiver is initialized

    return status;
}

/**
 * Stops the multicast receiver concurrent task.
 *
 * @asyncsignalsafety  Unsafe
 */
static void
mcastRcvr_stop(void)
{
    log_debug("Entered");
    mlr_halt(mcastRcvr.mlr);
    (void)pthread_join(mcastRcvr.thread, NULL);
    mcastRcvr_destroy();
}

/******************************************************************************
 * Requester of the backlog of data products (i.e., products missed since the
 * end of the previous session).
 ******************************************************************************/

typedef struct backlogger {
    signaturet      before;     ///< Signature of first product received via
                                ///< multicast
    pthread_t       thread;     ///< `backlogger_run()` thread
    pthread_mutex_t mutex;      ///< Mutex
    bool            haveThread; ///< Executing on thread?
} Backlogger;
static Backlogger backlogger;

/**
 * @param[in]  before       Signature of first product received via multicast
 * @retval     0            Success
 * @retval     LDM7_SYSTEM  System failure. `log_add()` called.
 */
static int
backlogger_init(const signaturet before)
{
    int status = mutex_init(&backlogger.mutex, PTHREAD_MUTEX_ERRORCHECK, true);

    if (status) {
        status = LDM7_SYSTEM;
    }
    else {
        (void)memcpy(backlogger.before, before, sizeof(signaturet));

        backlogger.haveThread = false;
    }

    return status;
}

static void
backlogger_destroy()
{
    (void)mutex_destroy(&backlogger.mutex);
}

// Forward declaration
static Ldm7Status
downlet_requestBacklog(const signaturet before);

/**
 * Executes the concurrent task that requests the backlog of data-products
 * missed since the end of the previous session. Doesn't return until
 *   - The request has been successfully made;
 *   - An error occurs; or
 *   - `backlogger_stop()` is called
 * Calls `downlet_taskTerminate()` on exit.
 *
 * @param[in] arg   Ignored
 * @retval    NULL  Always
 * @see `backlogger_stop()`
 */
static void*
backlogger_run(void* const arg)
{
    int status = downlet_requestBacklog(backlogger.before);

    log_flush_error(); // Just in case
    log_free();        // Because end of thread

    // Notify only on error
    if (status)
        downlet_taskTerminated(status);

    return NULL;
}

/**
 * Starts the concurrent task that requests the backlog of missed data-products.
 *
 * @param[in]  before  Signature of first product received via multicast
 * @retval     0       Success
 */
static Ldm7Status
backlogger_start(const signaturet before)
{
    int status = backlogger_init(before);

    if (status) {
        log_add("Couldn't initialize backlogger");
    }
    else {
        mutex_lock(&backlogger.mutex);
            status = pthread_create(&backlogger.thread, NULL, backlogger_run,
                    NULL);
            backlogger.haveThread = status == 0;
        mutex_unlock(&backlogger.mutex);

        if (status) {
            log_add("Couldn't create thread for backlogger");
            backlogger_destroy();
        }
    }

    return status;
}

/**
 * Stops the unicast receiver concurrent task.
 *
 * @asyncsignalsafety  Unsafe
 * @see `ucastRcvr_start()`
 */
static void
backlogger_stop(void)
{
    (void)pthread_kill(backlogger.thread, SIGTERM); // Interrupts socket write
    (void)pthread_join(backlogger.thread, NULL);
}

/******************************************************************************
 * One-time downstream LDM7
 ******************************************************************************/

/**
 * The data structure of the downstream LDM7. Defined here so that it can be
 * accessed by the one-time, downstream LDM7.
 */
struct {
    pthread_t             runThread;     ///< `down7_run()` thread
    pqueue*               pq;            ///< pointer to the product-queue
    ServiceAddr*          servAddr;      ///< socket address of remote LDM7
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
    pthread_mutex_t       mutex;            ///< Downstream LDM7 mutex
    uint64_t              numProds;         ///< Number of inserted products
    feedtypet             feedtype;         ///< Feed of multicast group
    VcEndPoint            vcEnd;            ///< Local virtual-circuit endpoint
    Ldm7Status            status;           ///< Downstream LDM7 status
    sigset_t              cancelSigSet;     ///< Cancellation signal mask
    int                   cancelSig;        ///< Cancellation signal
    bool                  prevLastMcastSet; ///< `prevLastMcast` set?
    volatile sig_atomic_t terminate;        ///< Termination requested?
    bool                  initialized;      ///< Object is initialized?
} down7;

typedef enum {
    TASK_STOPPED,
    TASK_STARTED
} TaskStatus;

/**
 * Data structure of the one-time, downstream LDM7. It is initialized and
 * destroyed on every connection attempt.
 */
typedef struct downlet {
    pthread_mutex_t mutex;            ///< Mutex
    pthread_cond_t  cond;             ///< Condition variable
    McastInfo*      mcastInfo;        ///< information on multicast group
    /**
     * IP address of interface to use for receiving multicast and unicast
     * FMTP packets
     */
    char*           upId;             ///< ID of upstream LDM7
    char*           feedId;           ///< Desired feed specification
    /// Server-side transport for receiving products
    SVCXPRT*        xprt;
    AtomicInt*      ucastRcvrStatus;  ///< Unicast receiver status
    AtomicInt*      backstopStatus;   ///< Backstop status
    AtomicInt*      mcastRcvrStatus;  ///< Multicast receiver status
    AtomicInt*      backloggerStatus; ///< Backlogger status
    in_addr_t       ifaceAddr;        ///< VLAN virtual interface IP address
    int             sock;             ///< Socket with remote LDM7
    int             taskStatus;       ///< Concurrent task status
} Downlet;
static Downlet downlet;

static Ldm7Status
downlet_startUcastRcvr(void)
{
    int status = ucastRcvr_start(downlet.sock);

    if (status) {
        log_add("Couldn't start unicast receiver task");
    }
    else {
        status = atomicInt_set(downlet.ucastRcvrStatus, TASK_STARTED);
        log_assert(status == TASK_STOPPED);
        status = 0;
    }

    return status;
}

static void
downlet_stopUcastRcvr(void)
{
    if (atomicInt_set(downlet.ucastRcvrStatus, TASK_STOPPED) == TASK_STARTED)
        ucastRcvr_stop();
}

static Ldm7Status
downlet_startBackstop(void)
{
    int status = backstop_start(down7.mrm);

    if (status) {
        log_add("Couldn't start backstop task");
    }
    else {
        status = atomicInt_set(downlet.backstopStatus, TASK_STARTED);
        log_assert(status == TASK_STOPPED);
        status = 0;
    }

    return status;
}

static void
downlet_stopBackstop(void)
{
    if (atomicInt_set(downlet.backstopStatus, TASK_STOPPED) == TASK_STARTED)
        backstop_stop();
}

static Ldm7Status
downlet_startMcastRcvr(void)
{
    int status = mcastRcvr_start(downlet.mcastInfo, down7.iface,
                    downlet.ifaceAddr, down7.pq);

    if (status) {
        log_add("Couldn't start multicast receiver task");
    }
    else {
        status = atomicInt_set(downlet.mcastRcvrStatus, TASK_STARTED);
        log_assert(status == TASK_STOPPED);
        status = 0;
    }

    return status;
}

static void
downlet_stopMcastRcvr(void)
{
    if (atomicInt_set(downlet.mcastRcvrStatus, TASK_STOPPED) == TASK_STARTED)
        mcastRcvr_stop();
}

static Ldm7Status
downlet_startBacklogger(const signaturet before)
{
    int status = backlogger_start(before);

    if (status) {
        log_add("Couldn't start backlog-requesting task");
    }
    else {
        status = atomicInt_set(downlet.backloggerStatus, TASK_STARTED);
        log_assert(status == TASK_STOPPED);
        status = 0;
    }

    return status;
}

static void
downlet_stopBacklogger(void)
{
    if (atomicInt_set(downlet.backloggerStatus, TASK_STOPPED) == TASK_STARTED)
        backlogger_stop();
}

/**
 * Starts the concurrent subtasks of the one-time, downstream LDM7 to receive
 * data-products. Blocks until all subtasks have started. The tasks are:
 * - Multicast data-product receiver
 * - Missed data-product (i.e., "backstop") requester
 * - Unicast data-product receiver
 *
 * NB: The task to receive data-products missed since the end of the previous
 * session (i.e., the "backlogger") is created elsewhere
 *
 * @retval     0              Success.
 * @retval     LDM7_SHUTDOWN  The LDM7 has been shut down. No task is running.
 * @retval     LDM7_SYSTEM    Error. `log_add()` called. No task is running.
 */
static Ldm7Status
downlet_startTasks(void)
{
    int status = downlet_startUcastRcvr();

    if (status == 0) {
        status = downlet_startBackstop();

        if (status == 0) {
            status = downlet_startMcastRcvr();

            if (status)
                downlet_stopBackstop();
        } // Backstop started

        if (status)
            downlet_stopUcastRcvr();
    } // Unicast receiver started

    return status;
}

/**
 * Stops all concurrent tasks.
 *
 * @asyncsignalsafety  Unsafe
 */
static void
downlet_stopTasks(void)
{
    downlet_stopBacklogger();
    downlet_stopMcastRcvr();
    downlet_stopBackstop();
    downlet_stopUcastRcvr();
}

static int
downlet_wait(void)
{
#if 1
    sigset_t oldsigset;
    sigprocmask(SIG_BLOCK, &down7.cancelSigSet, &oldsigset);
    int sig;
    (void)sigwait(&down7.cancelSigSet, &sig);
    sigprocmask(SIG_SETMASK, &oldsigset, NULL);
#else
    mutex_lock(&downlet.mutex);
        while (downlet.taskStatus == 0)
            pthread_cond_wait(&downlet.cond, &downlet.mutex);
    mutex_unlock(&downlet.mutex);
#endif

    return downlet.taskStatus;
}

/**
 * Handles the termination of a concurrent task by saving the status of the
 * first such task and signaling the one-time, downstream LDM7 to stop. Called
 * by concurrent tasks when they terminate.
 *
 * @param[in] status   Status of terminated task
 * @asyncsignalsafety  Unsafe
 */
static void
downlet_taskTerminated(const Ldm7Status status)
{
    mutex_lock(&downlet.mutex);
        bool cancelDownlet;

        if (downlet.taskStatus) {
            cancelDownlet = false;
        }
        else {
            downlet.taskStatus = status;
            cancelDownlet = true;
        }
    mutex_unlock(&downlet.mutex);

    if (cancelDownlet)
#if 1
        down7_halt();
#else
        pthread_cond_signal(&downlet.cond);
#endif
}

/**
 * Called by `backlogger_run()`.
 *
 * @param[in] before    Signature of first product received via multicast
 * @retval    0         Success
 * @retval    LDM7_RPC  Error in RPC layer. `log_add()` called.
 */
static Ldm7Status
downlet_requestBacklog(const signaturet before)
{
    int         status;
    BacklogSpec spec;

    spec.afterIsSet = mrm_getLastMcastProd(down7.mrm, spec.after);
    if (!spec.afterIsSet)
        (void)memset(spec.after, 0, sizeof(signaturet));
    (void)memcpy(spec.before, before, sizeof(signaturet));
    spec.timeOffset = getTimeOffset();

    status = up7Proxy_requestBacklog(&spec);

    if (status)
        log_add("Couldn't request session backlog");

    return status;
}

/**
 * Returns a socket that's connected to an Internet server via TCP. This is a
 * potentially lengthy operation.
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
 * @retval     LDM7_REFUSED   Remote host refused connection (host is offline or
 *                            server isn't running). `log_add()` called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. `log_add()`
 *                            called.
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 */
static int
downlet_getSock(
    const ServiceAddr* const restrict servAddr,
    const int                         family,
    int* const restrict               sock,
    struct sockaddr* const restrict   sockAddr)
{
    struct sockaddr addr;
    socklen_t       sockLen;
    int             status = sa_getInetSockAddr(servAddr, family, false, &addr,
            &sockLen);

    if (status == 0) {
        const int         useIPv6 = addr.sa_family == AF_INET6;
        const char* const addrFamilyId = useIPv6 ? "IPv6" : "IPv4";
        const int         fd = socket(addr.sa_family, SOCK_STREAM, IPPROTO_TCP);

        if (fd == -1) {
            log_add_syserr("Couldn't create %s TCP socket", addrFamilyId);
            status = (useIPv6 && errno == EAFNOSUPPORT)
                    ? LDM7_IPV6
                    : LDM7_SYSTEM;
        }
        else {
            if (connect(fd, &addr, sockLen)) {
                char sockAddrStr[128] = {};

                (void)sockaddr_format(&addr, sockAddrStr, sizeof(sockAddrStr));
                log_add_syserr(NULL);

                status = (errno == ETIMEDOUT)
                        ? LDM7_TIMEDOUT
                        : (errno == ECONNREFUSED || errno == EHOSTUNREACH)
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
 * address family AF_UNSPEC first, then AF_INET. This is a potentially lengthy
 * operation.
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
    const ServiceAddr* const restrict servAddr,
    int* const restrict               sock,
    struct sockaddr* const restrict   sockAddr)
{
    struct sockaddr addr;
    socklen_t       sockLen;
    int             fd;
    int             status = downlet_getSock(servAddr, AF_UNSPEC, &fd, &addr);

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
 * Creates a client that's connected to an upstream LDM7 server. This is a
 * potentially lengthy operation.
 *
 * @retval     0              Success. `downlet.up7Proxy` and `downlet.sock`
 *                            are set.
 * @retval     LDM7_SHUTDOWN  Shutdown requested
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
downlet_initClient()
{
    int             status;
    int             sock;
    struct sockaddr sockAddr;

    status = downlet_getSocket(down7.servAddr, &sock,
            &sockAddr); // Potentially lengthy

    if (status == LDM7_OK) {
        status = up7Proxy_init(sock, (struct sockaddr_in*)&sockAddr);

        if (status) {
            log_add("Couldn't initialize proxy for upstream LDM7");
            (void)close(sock);
        }
        else {
            downlet.sock = sock;
        }
    } // `sock` is open

    return status;
}

/**
 * Frees the client-side resources of the one-time, downstream LDM7.
 */
static void
downlet_destroyClient()
{
    log_debug("downlet_destroyClient() entered");

    up7Proxy_destroy(); // Won't close externally-created socket
    (void)close(downlet.sock);

    downlet.sock = -1;

    free(downlet.mcastInfo);
}

/**
 * Initializes the one-time, downstream LDM7.
 *
 * @param[in]  servAddr       Pointer to the address of the server from which to
 *                            obtain multicast information, backlog products,
 *                            and products missed by the FMTP layer. Must exist
 *                            until `downlet_destroy()` returns.
 * @param[in]  feed           Feed of multicast group to be received.
 * @param[in]  mcastIface     Name of interface to use for receiving multicast
 *                            packets (e.g., "eth0.0") or "dummy". Must exist
 *                            until `downlet_destroy()` returns.
 * @param[in]  vcEnd          Local virtual-circuit endpoint. Must exist until
 *                            `downlet_destroy()` returns. If the switch or port
 *                            identifier starts with "dummy", then the VLAN
 *                            virtual interface will not be created.
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
downlet_init()
{
    int status;

    downlet.sock = -1;
    downlet.upId = sa_format(down7.servAddr);
    downlet.taskStatus = 0;
    downlet.ucastRcvrStatus = atomicInt_new(TASK_STOPPED);
    downlet.backstopStatus = atomicInt_new(TASK_STOPPED);
    downlet.mcastRcvrStatus = atomicInt_new(TASK_STOPPED);
    downlet.backloggerStatus = atomicInt_new(TASK_STOPPED);

    if (downlet.ucastRcvrStatus == NULL || downlet.backstopStatus == NULL ||
            downlet.mcastRcvrStatus == NULL ||
            downlet.backloggerStatus == NULL) {
        log_add("Couldn't allocate status integers for tasks");
        status = LDM7_SYSTEM;
    }
    else {
        if (downlet.upId == NULL) {
            log_add("Couldn't format socket address of upstream LDM7");
            status = LDM7_SYSTEM;
        }
        else {
            downlet.feedId = feedtypet_format(down7.feedtype);

            if (downlet.feedId == NULL) {
                log_add("Couldn't format desired feed specification");
                status = LDM7_SYSTEM;
            }
            else {
                status = mutex_init(&downlet.mutex, PTHREAD_MUTEX_ERRORCHECK,
                        true);

                if (status) {
                    log_add("Couldn't initialize one-time, downstream LDM7 "
                            "mutex");
                }
                else {
                    status = pthread_cond_init(&downlet.cond, NULL);

                    if (status) {
                        log_add("Couldn't initialize one-time, downstream LDM7 "
                                "condition-variable");
                        mutex_destroy(&downlet.mutex);
                    }
                } // Mutex initialized

                if (status) {
                    free(downlet.feedId);
                    downlet.feedId = NULL;
                }
            } // `downlet.feedId` created

            if (status) {
                free(downlet.upId);
                downlet.upId = NULL;
            }
        } // `downlet.upId` created

        if (status) {
            atomicInt_free(downlet.ucastRcvrStatus);
            atomicInt_free(downlet.backstopStatus);
            atomicInt_free(downlet.mcastRcvrStatus);
            atomicInt_free(downlet.backloggerStatus);
        }
    } // Status integers for tasks allocated

    return status;
}

/**
 * Destroys the one-time, downstream LDM7.
 *
 * @retval        0            Success
 */
static Ldm7Status
downlet_destroy()
{
    (void)pthread_cond_destroy(&downlet.cond);
    (void)mutex_destroy(&downlet.mutex);
    free(downlet.feedId);
    free(downlet.upId);
    atomicInt_free(downlet.ucastRcvrStatus);
    atomicInt_free(downlet.backstopStatus);
    atomicInt_free(downlet.mcastRcvrStatus);
    atomicInt_free(downlet.backloggerStatus);

    return 0;
}

/**
 * Executes the one-time, downstream LDM7. Doesn't return until an error occurs.
 *
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
 * @retval        LDM7_SHUTDOWN  Shutdown requested
 * @retval        LDM7_TIMEDOUT  Subscription request timed-out. `log_add()`
 *                               called.
 * @retval        LDM7_REFUSED   Upstream host refused connection (LDM7 not
 *                               running?). `log_add()` called.
 * @retval        LDM7_UNAUTH    Upstream LDM7 denied request. `log_add()` called.
 * @see `downlet_halt()`
 */
static Ldm7Status
downlet_run()
{
    int status;

    // Sets `downlet.up7Proxy` and `downlet.sock`
    status = downlet_initClient(downlet); // Potentially lengthy

    if (status) {
        log_add("Couldn't create client for feed %s from server %s",
                downlet.feedId, downlet.upId);
    }
    else {
        /*
         * Blocks until error, reply, or timeout. Sets `downlet.mcastInfo`
         * and `downlet.fmtpAddr`. Potentially lengthy.
         */
        status = up7Proxy_subscribe(down7.feedtype, &down7.vcEnd,
                &downlet.mcastInfo, &downlet.ifaceAddr);

        if (status) {
            log_add("Couldn't subscribe to feed %s from %s",
                    downlet.feedId, downlet.upId);
        }
        else {
            char* const miStr = mi_format(downlet.mcastInfo);
            char        ifaceAddrStr[INET_ADDRSTRLEN];

            (void)inet_ntop(AF_INET, &downlet.ifaceAddr, ifaceAddrStr,
                    sizeof(ifaceAddrStr));
            log_notice("Subscription reply from %s: mcastGroup=%s, "
                    "ifaceAddr=%s", downlet.upId, miStr, ifaceAddrStr);
            free(miStr);

            status = downlet_startTasks();

            if (status) {
                log_add("Couldn't create concurrent tasks for feed %s from %s",
                        downlet.feedId, downlet.upId);
            }
            else {
                // Returns when concurrent task terminates
                status = downlet_wait();

                log_debug("downlet_run(): Status changed");
                (void)downlet_stopTasks();
            } // Subtasks created

            mi_free(downlet.mcastInfo); // NULL safe
            downlet.mcastInfo = NULL;
        } // `downlet.mcastInfo` set

        downlet_destroyClient();
    } // Client created

    return status;
}

static Ldm7Status
downlet_testConnection()
{
    return up7Proxy_testConnection();
}

/**
 * Increments the number of data-products successfully inserted into the
 * product-queue. Called by the multicast LDM receiver.
 */
void
downlet_incNumProds()
{
    down7_incNumProds();
}

/**
 * Adds a data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM7.
 *
 * @param[in] prod         data-product.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static int
downlet_recvProd(product* const restrict prod)
{
    // Products are also inserted on the multicast-receiver threads
    pqueue* const restrict  pq = down7.pq;
    int                     status = pq_insert(pq, prod);

    if (status == 0) {
        if (log_is_enabled_info) {
            char buf[LDM_INFO_MAX];

            (void)s_prod_info(buf, sizeof(buf), &prod->info,
                    log_is_enabled_debug);
            log_info("Inserted: %s", buf);
        }
        down7_incNumProds();
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
 * Queues a data-product for being requested by the LDM7 backstop mechanism.
 * This function is called by the multicast LDM receiver; therefore, it must
 * return immediately so that the multicast LDM receiver can continue.
 *
 * @param[in] iProd    Index of the missed FMTP product.
 */
void
downlet_missedProduct(const FmtpProdIndex iProd)
{
    log_debug("Entered: iProd=%lu", (unsigned long)iProd);

    /*
     * Cancellation of the operation of the missed-but-not-requested queue is
     * ignored because nothing can be done about it at this point and no harm
     * should result.
     */
    (void)mrm_addMissedFile(down7.mrm, iProd);
}

/**
 * Tracks the last data-product to be successfully received by the multicast
 * LDM receiver. This function is called by the multicast LDM receiver;
 * therefore, it must return immediately so that the multicast LDM receiver can
 * continue.
 *
 * The first time this function is called for a newly-initialized, one-time,
 * downstream LDM7, it starts a subtask that requests the backlog of
 * data-products that were missed due to the passage of time from the end of the
 * previous session to the reception of the first multicast data-product.
 *
 * @param[in] last     Pointer to the metadata of the last data-product to be
 *                     successfully received by the associated multicast
 *                     LDM receiver. Caller may free when it's no longer needed.
 */
void
downlet_lastReceived(const prod_info* const restrict last)
{
    mrm_setLastMcastProd(down7.mrm, last->signature);

    if (atomicInt_get(downlet.backloggerStatus) == TASK_STOPPED &&
            downlet_startBacklogger(last->signature))
        (void)downlet_stopTasks();
}

/******************************************************************************
 * Downstream LDM7
 ******************************************************************************/

/**
 * Initializes this module.
 *
 * @param[in]  servAddr     Pointer to the address of the server from which to
 *                          obtain multicast information, backlog products, and
 *                          products missed by the FMTP layer. Caller may free
 *                          upon return.
 * @param[in]  feed         Feed of multicast group to be received.
 * @param[in]  mcastIface   Name of interface to use for receiving multicast
 *                          packets or "dummy". Caller may free.
 * @param[in]  pq           The product-queue. Must be thread-safe (i.e.,
 *                          `pq_getFlags(pq) | PQ_THREADSAFE` must be true).
 * @param[in]  mrm          Persistent multicast receiver memory. Must exist
 *                          until `down7_destroy()` returns.
 * @param[in]  vcEnd        Local virtual-circuit endpoint. Caller may free.
 *                          If the switch or port identifier starts with
 *                          "dummy", then the VLAN virtual interface will not be
 *                          created.
 * @retval     0            Success
 * @retval     LDM7_INVAL   Product-queue isn't thread-safe. `log_add()` called.
 * @retval     LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
down7_init(
        const ServiceAddr* const restrict   servAddr,
        const feedtypet                     feed,
        const char* const restrict          mcastIface,
        const VcEndPoint* const restrict    vcEnd,
        pqueue* const restrict              pq,
        McastReceiverMemory* const restrict mrm)
{
    int status = mutex_init(&down7.mutex, PTHREAD_MUTEX_ERRORCHECK, true);

    if (status) {
        status = LDM7_SYSTEM;
    }
    else {
        down7.pq = pq;
        down7.feedtype = feed;
        down7.numProds = 0;
        down7.status = LDM7_OK;
        down7.mrm = mrm;
        down7.terminate = 0;
        down7.cancelSig = SIGTERM;
        sigemptyset(&down7.cancelSigSet);
        sigaddset(&down7.cancelSigSet, down7.cancelSig);
        (void)memset(down7.firstMcast, 0, sizeof(signaturet));
        (void)memset(down7.prevLastMcast, 0, sizeof(signaturet));

        /*
         * The product-queue must be thread-safe because this module accesses it
         * on these threads:
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
            if ((down7.servAddr = sa_clone(servAddr)) == NULL) {
                char buf[256];

                (void)sa_snprint(servAddr, buf, sizeof(buf));
                log_add("Couldn't clone server address \"%s\"", buf);
                status = LDM7_SYSTEM;
            }
            else {
                down7.iface = strdup(mcastIface);

                if (down7.iface == NULL) {
                    log_add("Couldn't copy multicast interface name");
                    status = LDM7_SYSTEM;
                }
                else {
                    if (!vcEndPoint_copy(&down7.vcEnd, vcEnd)) {
                        log_add("Couldn't copy receiver-side virtual-circuit "
                                "endpoint");
                        status = LDM7_SYSTEM;
                    }
                    else {
                        down7.initialized = true;
                    } // `down7.vcEnd` initialized

                    if (status)
                        free(down7.iface);
                } // `down7.iface` initialized

                if (status) {
                    sa_free(down7.servAddr);
                    down7.servAddr = NULL;
                }
            } // `down7.servAddr` initialized
        } // Product-queue is thread-safe

        if (status)
            mutex_destroy(&down7.mutex);
    } // Downstream LDM7 mutex initialized

    return status;
}

/**
 * Destroys the downstream LDM7 module.
 */
void
down7_destroy(void)
{
    if (down7.initialized) {
        vcEndPoint_destroy(&down7.vcEnd);
        free(down7.iface);
        sa_free(down7.servAddr);
        down7.servAddr = NULL;
        mutex_destroy(&down7.mutex);
        down7.initialized = false;
    }
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
down7_run()
{
    int status = 0;

    char* upId = sa_format(down7.servAddr);
    char* feedId = feedtypet_format(down7.feedtype);
    char* vcEndId = vcEndPoint_format(&down7.vcEnd);
    log_notice("Downstream LDM7 starting up: remoteLDM7=%s, feed=%s, "
            "FmtpIface=%s, vcEndPoint=%s, pq=\"%s\"", upId, feedId,
            down7.iface, vcEndId, pq_getPathname(down7.pq));
    free(vcEndId);
    free(feedId);
    free(upId);

    down7.runThread = pthread_self();

    for (;;) {
        mutex_lock(&down7.mutex);
            if (down7.terminate) {
                mutex_unlock(&down7.mutex);
                status = 0; // `down7_halt()` was called
                break;
            }
        mutex_unlock(&down7.mutex);

        status = downlet_init();

        if (status != 0) {
            log_add("Couldn't initialize one-time downstream LDM7");
            break;
        }

        status = downlet_run(); // Indefinite execution

        downlet_destroy();

        mutex_lock(&down7.mutex);
            if (down7.terminate) {
                mutex_unlock(&down7.mutex);
                status = 0; // `down7_halt()` called
                break;
            }
        mutex_unlock(&down7.mutex);

        if (status == LDM7_SHUTDOWN) {
            log_flush_notice(); // Just in case
            break;
        }

        if (status == LDM7_TIMEDOUT) {
            log_flush_notice();
            continue;
        }

        if (status == LDM7_NOENT || status == LDM7_REFUSED ||
                status == LDM7_UNAUTH) {
            log_flush_warning();
        }
        else {
            log_add("Error executing one-time, downstream LDM7");
            log_flush_error();
        }

        sleep(interval); // Problem might be temporary
    } // One-time, downstream LDM7 execution loop

    return status;
}

/**
 * Halts the downstream LDM7 module that's executing on another thread.
 *
 * @see `down7_run()`
 * @asyncsignalsafety  Safe
 */
void
down7_halt(void)
{
    down7.terminate = 1;

    (void)pthread_kill(down7.runThread, down7.cancelSig);
}

/**
 * Queues a request for a product.
 *
 * @param[in] iProd  Index of product to be requested
 */
void
down7_requestProduct(const FmtpProdIndex iProd)
{
    log_debug("Entered: iProd=%lu", (unsigned long)iProd);

    /*
     * Cancellation of the operation of the missed-but-not-requested queue is
     * ignored because nothing can be done about it at this point and no harm
     * should result.
     */
    (void)mrm_addMissedFile(down7.mrm, iProd);
}

/**
 * Increments the number of data-products successfully inserted into the
 * product-queue by the downstream LDM7. Called by the multicast LDM receiver.
 */
void
down7_incNumProds(void)
{
    (void)mutex_lock(&down7.mutex);
        down7.numProds++;
    (void)mutex_unlock(&down7.mutex);
}

/**
 * Returns the number of data-products successfully inserted into the
 * product-queue by a downstream LDM7.
 *
 * @return  Number of products
 */
uint64_t
down7_getNumProds(void)
{
    (void)mutex_lock(&down7.mutex);
        uint64_t num = down7.numProds;
    (void)mutex_unlock(&down7.mutex);

    return num;
}

/**
 * Returns the number of slots reserved in the product-queue for
 * not-yet-received data-products.
 *
 * @return  Number of reserved slots
 */
long
down7_getPqeCount(void)
{
    return pqe_get_count(down7.pq);
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
    log_debug("Entered");

    prod_info* const info = &missedProd->prod.info;
    FmtpProdIndex    iProd;

    if (!mrm_peekRequestedFileNoWait(down7.mrm, &iProd) ||
            iProd != missedProd->iProd) {
        char  buf[LDM_INFO_MAX];
        char* rmtStr = sockAddrIn_format(svc_getcaller(rqstp->rq_xprt));

        log_add("Unexpected product received from %s: %s", rmtStr,
                s_prod_info(buf, sizeof(buf), info, log_is_enabled_debug));
        free(rmtStr);
    }
    else {
        // The queue can't be empty
        (void)mrm_removeRequestedFileNoWait(down7.mrm, &iProd);

        if (downlet_recvProd(&missedProd->prod) != 0) {
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
    FmtpProdIndex* const restrict  missingIprod,
    struct svc_req* const restrict rqstp)
{
    FmtpProdIndex  iProd;

    if (!mrm_peekRequestedFileNoWait(down7.mrm, &iProd) ||
        iProd != *missingIprod) {
        log_add("Product %lu is unexpected", (unsigned long)*missingIprod);
    }
    else {
        // The queue can't be empty
        (void)mrm_removeRequestedFileNoWait(down7.mrm, &iProd);

        log_warning("Requested product %lu doesn't exist",
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
    int status = downlet_recvProd(prod);

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

    log_notice("All backlog data-products received: feed=%s, server=%s",
            s_feedtypet(down7.feedtype),
            sa_snprint(down7.servAddr, saStr, sizeof(saStr)));

    return NULL; // causes RPC dispatcher to not reply
}
