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
#include "ChildCmd.h"
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
#include "MldmRcvr.h"
#include "MldmRcvrMemory.h"
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
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef MAX
    #define MAX(a,b) ((a) >= (b) ? (a) : (b))
#endif

/******************************************************************************
 * Signals:
 ******************************************************************************/

static sigset_t termMask;

/**
 * Does something with the signal mask.
 *
 * @param[in] how   What to do. One of `SIG_BLOCK` or `SIG_UNBLOCK`.
 * @param[in] sigs  Signals
 */
static void
doSigs(
        const int how,
        va_list  sigs)
{
    int      status;
    sigset_t sigset;

    sigemptyset(&sigset);

    for (int sig; (sig = va_arg(sigs, int)); ) {
        status = sigaddset(&sigset, sig);
        log_assert(status == 0);
    }

    status = pthread_sigmask(how, &sigset, NULL);
    log_assert(status == 0);
}

/**
 * Blocks signals.
 *
 * @param[in] sig  Signals to be blocked
 */
static void
blockSigs(
        const int sig,
        ...)
{
    va_list  sigs;

    va_start(sigs, sig);
    doSigs(SIG_BLOCK, sigs);
    va_end(sigs);
}

/**
 * Unblocks signals.
 *
 * @param[in] sig  Signals to be unblocked
 */
static void
unblockSigs(
        const int sig,
        ...)
{
    va_list  sigs;

    va_start(sigs, sig);
    doSigs(SIG_UNBLOCK, sigs);
    va_end(sigs);
}

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
    log_debug("Entered");
    int status = pthread_mutex_lock(&up7Proxy.mutex);
    log_assert(status == 0);
}

/**
 * Unlocks the upstream LDM7 proxy.
 */
static void
up7Proxy_unlock()
{
    log_debug("Entered");
    int status = pthread_mutex_unlock(&up7Proxy.mutex);
    log_assert(status == 0);
}

/**
 * Initializes the proxy for the upstream LDM7.
 *
 * @param[in] socket       Socket with upstream LDM7
 * @param[in] sockAddr     Remote address of socket
 * @retval    0            Success
 * @retval    LDM7_INVAL   `socket < 0`
 * @retval    LDM7_INVAL   `sockAddr == NULL`
 * @retval    LDM7_INVAL   `sockAddr->sin_family != AF_INET`
 * @retval    LDM7_RPC     Error in RPC layer. `log_add()` called.
 * @retval    LDM7_SYSTEM  Couldn't initialize mutex. `log_add()` called.
 * @retval    LDM7_SYSTEM  Couldn't obtain string representation of remote end
 *                         of socket. `log_add()` called.
 */
static int
up7Proxy_init(const int           socket,
        struct sockaddr_in* const sockAddr)
{
    int status;

    (void)memset(&up7Proxy, 0, sizeof(up7Proxy));

    if (socket < 0 || sockAddr == NULL || sockAddr->sin_family != AF_INET) {
        status = LDM7_INVAL;
    }
    else {
        status = mutex_init(&up7Proxy.mutex, PTHREAD_MUTEX_ERRORCHECK, true);

        if (status) {
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

/**
 * Destroys the proxy for the upstream LDM7.
 */
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
 * @param[out] ifaceCidr      CIDR address of VLAN virtual interface
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
        SepMcastInfo** const restrict    mcastInfo,
        CidrAddr* const restrict         ifaceCidr)
{
    int status;

    up7Proxy_lock();
        CLIENT* const clnt = up7Proxy.clnt;

        McastSubReq   request;
        request.feed = feed;
        request.vcEnd = *vcEnd;

        /*
         * The RPC timeout is increased because adding a node to an AL2S
         * multipoint VLAN can take longer than the default RPC timeout.
         *
         * NB: The RPC-timeout is set by the first `clnt_call()`
         */
        struct timeval timeout = {};
        timeout.tv_sec = 600;

        log_debug("Setting LDM7 subscription-timeout to %ld s",
                (long)timeout.tv_sec);
        (void)clnt_control(clnt, CLSET_TIMEOUT, &timeout); // Can't fail

        /*
         * WARNING: If a standard RPC implementation is used, then it is likely
         * that `subscribe_7()` won't return when a signal is received because
         * `readtcp()` in `clnt_tcp.c` nominally ignores `EINTR`. The RPC
         * implementation included with the LDM package has been modified to not
         * have this problem. -- Steve Emmerson 2018-03-26
         */
        SubscriptionReply* reply = subscribe_7(&request, clnt);

        timeout.tv_sec = 60; // Keep consonant with TIMEOUT in `fix_clnt.pl`
        log_debug("Resetting LDM7 RPC-timeout to %ld s", (long)timeout.tv_sec);
        (void)clnt_control(clnt, CLSET_TIMEOUT, &timeout);

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
                *mcastInfo = smi_newFromStr(
                        reply->SubscriptionReply_u.info.mcastInfo.feed,
                        reply->SubscriptionReply_u.info.mcastInfo.group,
                        reply->SubscriptionReply_u.info.mcastInfo.server);

                if (cidrAddr_copy(ifaceCidr,
                        &reply->SubscriptionReply_u.info.fmtpAddr) == NULL) {
                    log_add("Subscription reply contains invalid CIDR address "
                            "for virtual interface");
                    status = LDM7_INVAL;
                }
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
    const BacklogSpec* const restrict spec)
{
    int status;

    up7Proxy_lock();
        CLIENT* const clnt = up7Proxy.clnt;

        // asynchronous => no reply
        (void)request_backlog_7((BacklogSpec*)spec, clnt); // Safe cast
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
up7Proxy_reqProd(const FmtpProdIndex iProd)
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

#if 0
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
#endif

/******************************************************************************
 * Miscellaneous concurrent task stuff
 ******************************************************************************/

typedef enum {
    TASK_UNINITIALIZED = 0, ///< Task is uninitialized. `static` value
    TASK_INITIALIZED,       ///< Task is initialized
    TASK_STARTED,           ///< Task has started
    TASK_STOPPED            ///< Task has completed
} TaskState;

/******************************************************************************
 * Forward declaration
 ******************************************************************************/

static void
downlet_taskCompleted(const Ldm7Status status);

/******************************************************************************
 * Requester of data-products missed by the FMTP Layer:
 ******************************************************************************/

typedef struct {
    pthread_mutex_t       mutex;     ///< Mutex
    pthread_t             thread;    ///< `backstop_run()` thread
    McastReceiverMemory*  mrm;       ///< Persistent multicast receiver memory
    signaturet            prevLastMcast;    ///< Previous session's Last prod
    bool                  prevLastMcastSet; ///< `prevLastMcast` set?
    TaskState             state;     ///< State of task
} Backstop;
static Backstop backstop;

/**
 * Initializes the concurrent task for requesting data-products missed by the
 * multicast component of the one-time, downstream LDM7.
 *
 * @param[in] mrm          Multicast receiver memory. Must exist until
 *                         `backstop_destroy()` returns.
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `backstop_destroy()`
 */
static int
backstop_init(McastReceiverMemory* const mrm)
{
    log_assert(mrm);
    log_assert(backstop.state == TASK_UNINITIALIZED);

    (void)memset(&backstop, 0, sizeof(backstop));

    int status = mutex_init(&backstop.mutex, PTHREAD_MUTEX_ERRORCHECK, true);

    if (status) {
        status = LDM7_SYSTEM;
    }
    else {
        backstop.mrm = mrm;
        backstop.prevLastMcastSet = mrm_getLastMcastProd(backstop.mrm,
                backstop.prevLastMcast);
        backstop.state = TASK_INITIALIZED;
    } // Mutex is initialized

    return 0;
}

/**
 * Destroys the concurrent task for requesting data-products missed by the
 * multicast component of the one-time, downstream LDM7.
 */
static void
backstop_destroy()
{
    mutex_lock(&backstop.mutex);
        log_assert(backstop.state == TASK_INITIALIZED ||
                backstop.state == TASK_STOPPED);

        backstop.state = TASK_UNINITIALIZED;
    mutex_unlock(&backstop.mutex);
    (void)mutex_destroy(&backstop.mutex);
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
    log_debug("Entered");

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

				status = up7Proxy_reqProd(iProd);

				if (status) {
					log_add("Couldn't request product");
					break;
				}
			} // product-index added to requested-but-not-received queue
		} // have peeked-at product-index from missed-but-not-requested queue
	} // Indefinite loop

	log_debug("Calling downlet_taskCompleted(%d)", status);
	downlet_taskCompleted(status);

	log_flush_error(); // Just in case

    log_debug("Returning");
    log_free();        // Because end of thread

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
    log_assert(backstop.state == TASK_UNINITIALIZED);

    int status = backstop_init(mrm);

    if (status) {
        log_add("Couldn't initialize backstop");
    }
    else {
		mutex_lock(&backstop.mutex);
			status = pthread_create(&backstop.thread, NULL, backstop_run, NULL);

			if (status == 0)
				backstop.state = TASK_STARTED;
		mutex_unlock(&backstop.mutex);

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
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
static void
backstop_stop(void)
{
    mutex_lock(&backstop.mutex);
        const bool stopTask = backstop.state == TASK_STARTED;

        if (stopTask) {
            mrm_shutDownMissedFiles(backstop.mrm);

            backstop.state = TASK_STOPPED;
        }
    mutex_unlock(&backstop.mutex);

    if (stopTask) {
        int status = pthread_join(backstop.thread, NULL);
        log_assert(status == 0);

        backstop_destroy();
    }
}

/******************************************************************************
 * Receiver of unicast products from an upstream LDM7
 ******************************************************************************/

typedef struct {
    pthread_t             thread;      ///< Thread on which task executes
    pthread_mutex_t       mutex;       ///< Mutex
    SVCXPRT*              xprt;        ///< Server-side RPC transport for
                                       ///< receiving missed data-products
    char*                 remoteStr;   ///< ID of remote peer
    TaskState             state;       ///< State of task
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
    log_assert(sock >= 0);
    log_assert(ucastRcvr.state == TASK_UNINITIALIZED);

    (void)memset(&ucastRcvr, 0, sizeof(ucastRcvr));

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
                else {
                    ucastRcvr.state = TASK_INITIALIZED;
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
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 * @see `ucastRcvr_init()`
 */
static void
ucastRcvr_destroy(void)
{
    mutex_lock(&ucastRcvr.mutex);
        log_assert(ucastRcvr.state == TASK_INITIALIZED ||
                ucastRcvr.state == TASK_STOPPED);
        svc_unregister(LDMPROG, SEVEN);
        free(ucastRcvr.remoteStr);

        // Transport might have been destroyed by `ucastRcvr_run()`
        if (ucastRcvr.xprt)
            svc_destroy(ucastRcvr.xprt);

        ucastRcvr.state = TASK_UNINITIALIZED;
    mutex_unlock(&ucastRcvr.mutex);
    mutex_destroy(&ucastRcvr.mutex);
}

/**
 * Runs the RPC-based server of a downstream LDM7. *Might* destroy and
 * unregister the service transport. Doesn't return until `ucastRcvr_stop()` is
 * called or an error occurs
 *
 * Called by `pthread_create()`.
 *
 * @param[in]     arg            Ignored
 * @see `ucastRcvr_stop()`
 */
static void*
ucastRcvr_run(void* const restrict arg)
{
    log_debug("Entered");

	blockSigs(SIGCONT, SIGALRM, 0);

	int           status = 0;
	const int     sock = ucastRcvr.xprt->xp_sock;
	const int     timeout = interval * 1000; // Probably 30 seconds
	struct pollfd pfd;

	pfd.fd = sock;
	pfd.events = POLLRDNORM;

	log_info("Starting unicast receiver: sock=%d, timeout=%d ms", sock,
			timeout);

	mutex_lock(&ucastRcvr.mutex);
		while (ucastRcvr.state == TASK_STARTED) {

			// Excessive output
			log_debug("Calling poll(): socket=%d, timeout=%d", sock,
					timeout);
			mutex_unlock(&ucastRcvr.mutex);
				// poll() is async-signal safe
				status = poll(&pfd, 1, timeout);
			mutex_lock(&ucastRcvr.mutex);

			if (0 == status) {
				log_debug("Timeout");
				continue; // Timeout
			}

			if (status < 0) {
				if (errno == EINTR) {
					log_debug("poll() was interrupted");
					/*
					 * Might not be meaningful. For example, the GNUlib
					 * seteuid() function generates a non-standard signal in
					 * order to synchronize UID changes amongst threads.
					 */
					status = 0;
					continue;
				}
				log_debug("poll() failure");
				log_add_syserr("poll() failure on socket %d to upstream "
						"LDM7 %s", sock, ucastRcvr.remoteStr);
				status = LDM7_SYSTEM;
				break;
			}

			if (pfd.revents & POLLERR) {
				log_debug("Socket failure");
				log_add("Error on socket %d to upstream LDM7 %s", sock,
						ucastRcvr.remoteStr);
				status = LDM7_SYSTEM;
				break;
			}

			if (pfd.revents & POLLHUP) {
				log_debug("Socket closed");
				log_add_syserr("Socket %d to upstream LDM7 %s was closed",
						sock, ucastRcvr.remoteStr);
				status = LDM7_SYSTEM;
				break;
			}

			if (pfd.revents & (POLLIN | POLLRDNORM)) {
				/*
				 * Processes RPC message. Calls select(). Calls
				 * `ldmprog_7()`. Calls `svc_destroy(ucastRcvr.xprt)` on
				 * error.
				 */
				log_debug("Got RPC message");
				svc_getreqsock(sock);

				if (!FD_ISSET(sock, &svc_fdset)) {
					log_debug("Transport destroyed");
					// `svc_getreqsock()` destroyed `ucastRcvr.xprt`
					log_add("Connection to upstream LDM7 %s was closed by "
							"RPC layer", ucastRcvr.remoteStr);
					ucastRcvr.xprt = NULL; // To inform others
					status = LDM7_RPC;
					break;
				}
				else {
					log_debug("RPC message processed");
					status = 0;
				}
			} // Input is available
		} // Input loop
	mutex_unlock(&ucastRcvr.mutex);

	if (status)
		log_flush_error();

	log_debug("Calling downlet_taskCompleted(%d)", status);
	downlet_taskCompleted(status);

	unblockSigs(SIGCONT, SIGALRM, 0);

    log_debug("Returning");
    log_free(); // Because end of thread

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
    log_assert(ucastRcvr.state == TASK_UNINITIALIZED);

    int status = ucastRcvr_init(sock);

    if (status) {
        log_add("Couldn't initialize unicast receiver");
    }
    else {
        mutex_lock(&ucastRcvr.mutex);
            status = pthread_create(&ucastRcvr.thread, NULL, ucastRcvr_run,
                    NULL);

            if (status == 0) {
            	/*
            	 * State must be set while locked because `ucastRcvr_run()`
            	 * accesses `ucastRcvr.state`
            	 */
            	ucastRcvr.state = TASK_STARTED;
            }
		mutex_unlock(&ucastRcvr.mutex);

        if (status) {
			log_add("Couldn't create thread for unicast receiver");
            ucastRcvr_destroy();
            status = LDM7_SYSTEM;
        }
    } // Unicast receiver initialized

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
    log_debug("Entered");

    mutex_lock(&ucastRcvr.mutex);
        int        status;
        const bool stopTask = ucastRcvr.state == TASK_STARTED;

        if (stopTask) {
            ucastRcvr.state = TASK_STOPPED;

            // Interrupts `poll()`
            status = pthread_kill(ucastRcvr.thread, SIGHUP);

            // Apparently, a terminated thread cannot be sent a signal.
            if (status && status != ESRCH) {
                log_add_errno(status, "Couldn't kill unicast receiver");
                log_flush_error();
            }
        }
    mutex_unlock(&ucastRcvr.mutex);

    if (stopTask) {
        status = pthread_join(ucastRcvr.thread, NULL);
        log_assert(status == 0);

        ucastRcvr_destroy();
    }

    log_debug("Returning");
}

/******************************************************************************
 * Submodule for local VLAN interface
 ******************************************************************************/

/*
 * For an unknown reason, sh(1) on Ubuntu doesn't find vlanUtil(1) unless the
 * absolute pathname is given.
 */
static const char vlanUtil[] = LDM_BIN_DIR "/vlanUtil";

/**
 * Creates a local VLAN interface. Replaces the relevant routing, address, and
 * link information.
 *
 * @param[in] srvrAddrStr  Dotted-decimal IP address of sending FMTP server
 * @param[in] ifaceName    Name of virtual interface to be created (e.g.,
 *                         "eth0.0") or "dummy", in which case no virtual
 *                         interface will be created.
 * @param[in] ifaceCidr    CIDR address to be assigned to virtual interface
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @retval    LDM7_INVAL   Invalid argument. `log_add()` called.
 */
static int
vlanIface_create(
        const char* const restrict     srvrAddrStr,
        const char* const restrict     ifaceName,
        const CidrAddr* const restrict ifaceCidr)
{
    log_assert(srvrAddrStr);
    log_assert(ifaceName);

    int  status;

    if (strncmp(ifaceName, "dummy", 5) == 0) {
        status = 0;
    }
    else {
        char ifaceCidrStr[CIDRSTRLEN];

        // Can't fail
        (void)cidrAddr_snprintf(ifaceCidr, ifaceCidrStr, sizeof(ifaceCidrStr));

        const char* const cmdVec[] = {"sh", vlanUtil, "create", ifaceName,
                ifaceCidrStr, srvrAddrStr, NULL};

        int childStatus;
        status = sudo(cmdVec, &childStatus);

        if (status || childStatus)
            status = LDM7_SYSTEM;
    } // A virtual interface is actually requested

    return status;
}

/**
 * Destroys a local VLAN interface.
 *
 * @param[in] srvrAddrStr  IP address of sending FMTP server in dotted-decimal
 *                         form
 * @param[in] ifaceName    Name of virtual interface to be destroyed (e.g.,
 *                         "eth0.0") or "dummy", in which case no virtual
 *                         interface will be destroyed
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
        status = 0;
    }
    else {
        int               childStatus;
        const char* const cmdVec[] = {"sh", vlanUtil, "destroy", ifaceName,
                srvrAddrStr, NULL};

        status = sudo(cmdVec, &childStatus);

        if (status || childStatus) {
            log_add("Couldn't destroy local VLAN interface");
            status = LDM7_SYSTEM;
        }
    } // Not a dummy FMTP virtual interface

    return status;
}

/******************************************************************************
 * Requester of the backlog of data products (i.e., products missed since the
 * end of the previous session).
 ******************************************************************************/

typedef struct backlogger {
    BacklogSpec     backlog;    ///< Backlog specification
    pthread_t       thread;     ///< `backlogger_run()` thread
    pthread_mutex_t mutex;      ///< Mutex
    TaskState       state;      ///< State of task
} Backlogger;
static Backlogger backlogger;

/**
 * Initializes the concurrent task that requests the backlog of data-products
 * that were missed since the end of the previous downstream LDM7 session.
 *
 * @param[in]  before       Signature of first product received via multicast
 * @param[in]  mrm          Persistent, multicast-receiver memory
 * @retval     0            Success
 * @retval     LDM7_SYSTEM  System failure. `log_add()` called.
 */
static int
backlogger_init(
        const signaturet                    before,
        McastReceiverMemory* const restrict mrm)
{
    log_debug("Entered");

    log_assert(before);
    log_assert(backlogger.state == TASK_UNINITIALIZED);

    (void)memset(&backlogger, 0, sizeof(backlogger));

    int status = mutex_init(&backlogger.mutex, PTHREAD_MUTEX_ERRORCHECK, true);

    if (status) {
        status = LDM7_SYSTEM;
    }
    else {
        BacklogSpec* const spec = &backlogger.backlog;

        spec->afterIsSet = mrm_getLastMcastProd(mrm, spec->after);
        if (!spec->afterIsSet)
            (void)memset(spec->after, 0, sizeof(signaturet));
        (void)memcpy(spec->before, before, sizeof(signaturet));
        spec->timeOffset = getTimeOffset();

        log_debug("after=%s", s_signaturet(NULL, 0, spec->after));
        log_debug("before=%s", s_signaturet(NULL, 0, spec->before));

        backlogger.state = TASK_INITIALIZED;
    }

    log_debug("Returning");

    return status;
}

/**
 * Destroys the backlog concurrent task.
 */
static void
backlogger_destroy()
{
    log_debug("Entered");

    mutex_lock(&backlogger.mutex);
        log_assert(backlogger.state == TASK_INITIALIZED ||
                backlogger.state == TASK_STOPPED);

        backlogger.state = TASK_UNINITIALIZED;
    mutex_unlock(&backlogger.mutex);
    (void)mutex_destroy(&backlogger.mutex);

    log_debug("Returning");
}

// Forward declaration
static Ldm7Status
downlet_requestBacklog(const BacklogSpec* backlog);

/**
 * Executes the concurrent task that requests the backlog of data-products
 * missed since the end of the previous session. Doesn't return until
 *   - The request has been successfully made;
 *   - An error occurs; or
 *   - `backlogger_stop()` is called
 * Calls `downlet_taskCompleted()` on exit.
 *
 * @param[in] arg   Ignored
 * @retval    NULL  Always
 * @see `backlogger_stop()`
 */
static void*
backlogger_run(void* const arg)
{
    log_debug("Entered");

	// SIGHUP sensitive
	int status = downlet_requestBacklog(&backlogger.backlog);

	log_debug("Calling downlet_taskCompleted(%d)", status);
	downlet_taskCompleted(status);

	log_flush_error(); // Just in case

    log_debug("Returning");
    log_free();        // Because end of thread

    return NULL;
}

/**
 * Starts the concurrent task that requests the backlog of missed data-products.
 *
 * @param[in]  before       Signature of first product received via multicast
 * @param[in]  mrm          Persistent, multicast-receiver memory
 * @retval     0            Success
 * @retval     LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
backlogger_start(
        const signaturet                    before,
        McastReceiverMemory* const restrict mrm)
{
    log_debug("Entered");

    log_assert(before);
    log_assert(backlogger.state == TASK_UNINITIALIZED);

    int status = backlogger_init(before, mrm);

    if (status) {
        log_add("Couldn't initialize backlogger");
    }
    else {
        mutex_lock(&backlogger.mutex);
            status = pthread_create(&backlogger.thread, NULL, backlogger_run,
                    NULL);

            if (status == 0)
            	backlogger.state = TASK_STARTED;
        mutex_unlock(&backlogger.mutex);

        if (status) {
            log_add("Couldn't create thread for backlogger");
            backlogger_destroy();
            status = LDM7_SYSTEM;
        }
    } // Backlogger initialized

    log_debug("Returning");

    return status;
}

/**
 * Stops the concurrent backlog-requesting task. Idempotent.
 *
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 * @see `backlogger_start()`
 * @see `backlogger_run()`
 */
static void
backlogger_stop(void)
{
    log_debug("Entered");

    mutex_lock(&backlogger.mutex);
        int        status;
        const bool stopTask = backlogger.state == TASK_STARTED;

        if (stopTask) {
            backlogger.state = TASK_STOPPED;

            status = pthread_kill(backlogger.thread, SIGHUP);

            // Apparently, a terminated thread cannot be sent a signal.
            if (status && status != ESRCH) {
                log_add_errno(status, "Couldn't kill backlog requester");
                log_flush_error();
            }
        }
    mutex_unlock(&backlogger.mutex);

    if (stopTask) {
        status = pthread_join(backlogger.thread, NULL);
        log_assert(status == 0);

        backlogger_destroy();
    }

    log_debug("Returning");
}

/******************************************************************************
 * Receiver of multicast products from an upstream LDM7 (uses the FMTP layer)
 ******************************************************************************/

typedef struct {
    pthread_t       thread;            ///< `mcastRcvr_run()` thread
    pthread_mutex_t mutex;             ///< Mutex
    Mlr*            mlr;               ///< Multicast LDM receiver
    /// Dotted decimal form of sending FMTP server
    char            fmtpSrvrAddr[INET_ADDRSTRLEN];
    /// Name of interface to be used by FMTP layer or `NULL`
    const char*     fmtpIface;
    TaskState       state;             ///< State of the task
    bool            backloggerStarted; ///< Backlogger task started?
} McastRcvr;
static McastRcvr mcastRcvr;

/**
 * Initializes a multicast receiver.
 *
 * @param[in] mcastInfo    Information on multicast group
 * @param[in] fmtpIface    Name of virtual interface to be created and used by
 *                         FMTP layer or "dummy", indicating that no virtual
 *                         interfaces is to be created. Must exist until
 *                         `mcastRcvr_destroy()` returns.
 * @param[in] ifaceCidr    CIDR address to be assigned to `fmtpIface`
 * @param[in] pq           Product queue
 * @retval    0            Success or `ifaceName` starts with "dummy"
 * @retval    LDM7_INVAL   Invalid address of sending FMTP server. `log_add()`
 *                         called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @see `mcastRcvr_destroy()`
 */
static int
mcastRcvr_init(
        SepMcastInfo* const restrict   mcastInfo,
        const char* const restrict     fmtpIface,
        const CidrAddr* const restrict ifaceCidr,
        pqueue* const restrict         pq)
{
    log_assert(mcastInfo);
    log_assert(fmtpIface);
    log_assert(pq);
    log_assert(mcastRcvr.state == TASK_UNINITIALIZED); // True because static & enum == 0

    (void)memset(&mcastRcvr, 0, sizeof(mcastRcvr));

    mcastRcvr.mlr = NULL;

    const char* fmtpSrvrId = isa_getInetAddrStr(smi_getFmtpSrvr(mcastInfo));
    int         status = getDottedDecimal(fmtpSrvrId, mcastRcvr.fmtpSrvrAddr);

    if (status) {
        log_add("Invalid address of sending FMTP server: \"%s\"", fmtpSrvrId);
        status = LDM7_INVAL;
    }
    else {
        status = mutex_init(&mcastRcvr.mutex, PTHREAD_MUTEX_ERRORCHECK, true);

        if (status) {
            status = LDM7_SYSTEM;
        }
        else {
            status = vlanIface_create(mcastRcvr.fmtpSrvrAddr, fmtpIface,
                    ifaceCidr);

            if (status) {
                log_add("Couldn't create VLAN virtual-interface");
            }
            else {
                const in_addr_t ifaceAddr = cidrAddr_getAddr(ifaceCidr);
                char            ifaceAddrStr[INET_ADDRSTRLEN];

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
                    mcastRcvr.fmtpIface = fmtpIface;
                    mcastRcvr.backloggerStarted = false;
                    mcastRcvr.state = TASK_INITIALIZED;
                } // `mlr` allocated

                if (status)
                    vlanIface_destroy(mcastRcvr.fmtpSrvrAddr, fmtpIface);
            } // FMTP VLAN interface created or not necessary

            if (status)
                (void)mutex_destroy(&mcastRcvr.mutex);
        } // Mutex initialized
    } // `fmtpSrvrId` is valid

    return status;
}

/**
 * Destroys the concurrent task that receives multicast data-products.
 */
static void
mcastRcvr_destroy()
{
    mutex_lock(&mcastRcvr.mutex);
        log_assert(mcastRcvr.state == TASK_INITIALIZED ||
                mcastRcvr.state == TASK_STOPPED);

        mlr_free(mcastRcvr.mlr);
        mcastRcvr.mlr = NULL;

        if (vlanIface_destroy(mcastRcvr.fmtpSrvrAddr, mcastRcvr.fmtpIface)) {
            log_add("Couldn't destroy VLAN virtual interface \"%s\"",
                    mcastRcvr.fmtpIface);
            log_flush_error();
        }

        mcastRcvr.state = TASK_UNINITIALIZED;
    mutex_unlock(&mcastRcvr.mutex);
    (void)mutex_destroy(&mcastRcvr.mutex);
}

/**
 * Receives data-products via multicast. Doesn't return until `mcastRcvr_stop()`
 * is called or an error occurs. Calls `downlet_taskCompleted()` on exit.
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

	log_debug("Calling downlet_taskCompleted(%d)", status);
	downlet_taskCompleted(status);

	log_flush_error(); // Just in case

    log_debug("Returning");
    log_free();        // Because end of thread
    return NULL;
}

/**
 * Starts the multicast receiver concurrent task.
 *
 * @param[in] mcastInfo   Information on multicast feed
 * @param[in] fmtpIface   Name of virtual interface to be created and used by
 *                        FMTP layer or "dummy", indicating that no virtual
 *                        interfaces is to be created. Must exist until
 *                        `mcastRcvr_stop()` returns.
 * @param[in] ifaceAddr   IP address to be assigned to the virtual interface if
 *                        it's created
 * @param[in] pq          Output product queue
 * @retval    0           Success
 * @retval    LDM7_INVAL  Invalid address of sending FMTP server. `log_add()`
 *                        called.
 * @retval    LDM7_SYSTEM System failure. `log_add()` called.
 * @see `mcastRcvr_stop()`
 */
static Ldm7Status
mcastRcvr_start(
        SepMcastInfo* const restrict   mcastInfo,
        const char* const restrict     fmtpIface,
        const CidrAddr* const restrict ifaceCidr,
        pqueue* const restrict         pq)
{
    log_assert(mcastInfo);
    log_assert(fmtpIface);
    log_assert(pq);
    log_assert(mcastRcvr.state == TASK_UNINITIALIZED); // True because static & enum == 0

    int status = mcastRcvr_init(mcastInfo, fmtpIface, ifaceCidr, pq);

    if (status) {
        log_add("Couldn't initialize multicast receiver");
    }
    else {
		mutex_lock(&mcastRcvr.mutex);
			status = pthread_create(&mcastRcvr.thread, NULL, mcastRcvr_run,
					NULL);

			if (status == 0)
				mcastRcvr.state = TASK_STARTED;
		mutex_unlock(&mcastRcvr.mutex);

		if (status) {
            log_add("Couldn't create thread for multicast receiver");
            mcastRcvr_destroy();
            status = LDM7_SYSTEM;
		}
    } // Multicast receiver is initialized

    return status;
}

/**
 * Stops the multicast receiver concurrent task. Idempotent.
 *
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 * @see `mcastRcvr_start()`
 * @see `mcastRcvr_run()`
 */
static void
mcastRcvr_stop(void)
{
    log_debug("Entered");

    mutex_lock(&mcastRcvr.mutex);
        const bool stopTask = mcastRcvr.state == TASK_STARTED;

        if (stopTask) {
            mcastRcvr.state = TASK_STOPPED;

            if (mcastRcvr.backloggerStarted)
                backlogger_stop();

            mlr_halt(mcastRcvr.mlr);
        }
    mutex_unlock(&mcastRcvr.mutex);

    if (stopTask) {
        int status = pthread_join(mcastRcvr.thread, NULL);
        log_assert(status == 0);

        mcastRcvr_destroy();
    }

    log_debug("Returning");
}

/**
 * Accepts notification of the last data-product to be received via multicast.
 *
 * @param[in] signature    MD5 checksum of the data-product
 * @param[in] mrm          Persistent, multicast-receiver memory
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
static int
mcastRcvr_lastReceived(
        const signaturet                    signature,
        McastReceiverMemory* const restrict mrm)
{
    log_assert(signature);

    int status;

    mutex_lock(&mcastRcvr.mutex);
        if (mcastRcvr.state != TASK_STARTED) {
            status = 0;
        }
        else {
            if (mcastRcvr.backloggerStarted) {
                status = 0;
            }
            else {
                status = backlogger_start(signature, mrm);

                if (status) {
                    log_add("Couldn't start backlog-requesting task");
                }
                else {
                    mcastRcvr.backloggerStarted = true;
                }
            }
        }
    mutex_unlock(&mcastRcvr.mutex);

    return status;
}

/******************************************************************************
 * One-time downstream LDM7
 ******************************************************************************/

/**
 * The data structure of the downstream LDM7. Defined here so that it can be
 * accessed by the one-time, downstream LDM7.
 */
static struct {
    pqueue*               pq;      ///< Product-queue
    InetSockAddr*         ldmSrvr; ///< Address of remote LDM7 server
    /**
     * IPv4 address of interface to be used by FMTP layer
     */
    char*                 fmtpIface;
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
    pthread_t             thread;           ///< Thread down7_run() executes on
    bool                  prevLastMcastSet; ///< `prevLastMcast` set?
    volatile sig_atomic_t done;             ///< Termination requested?
    volatile sig_atomic_t state;            ///< State of downstream LDM7
    sem_t                 intrSem;          ///< Interrupt semaphore
} down7 = {};

/**
 * Data structure of the one-time, downstream LDM7. It is initialized and
 * destroyed on every connection attempt.
 */
typedef struct downlet {
    pthread_mutex_t       mutex;      ///< Mutex
    pthread_cond_t        cond;       ///< Condition variable
    SepMcastInfo*         mcastInfo;  ///< information on multicast group
    /**
     * IP address of interface to use for receiving multicast and unicast
     * FMTP packets
     */
    char*                 feedId;     ///< Desired feed specification
    /// Server-side transport for receiving products
    SVCXPRT*              xprt;
    CidrAddr              ifaceCidr;  ///< VLAN virtual interface CIDR address
    int                   sock;       ///< Socket with remote LDM7
    volatile sig_atomic_t taskStatus; ///< Concurrent task status
} Downlet;
static Downlet downlet;

static InetId* inAddrAny;             ///< INADDR_ANY

/**
 * Starts the concurrent subtasks of the one-time, downstream LDM7 to receive
 * data-products. Blocks until all subtasks have started. The tasks are:
 * - Multicast data-product receiver
 * - Missed data-product (i.e., "backstop") requester
 * - Unicast data-product receiver
 *
 * NB: The subtask to request data-products missed since the end of the previous
 * session (i.e., the "backlogger") is managed by the multicast receiver task.
 *
 * @retval     0              Success.
 * @retval     LDM7_SHUTDOWN  The LDM7 has been shut down. No task is running.
 * @retval     LDM7_SYSTEM    Error. `log_add()` called. No task is running.
 */
static Ldm7Status
downlet_startTasks(void)
{
    int status = ucastRcvr_start(downlet.sock);

    if (status == 0) {
        status = backstop_start(down7.mrm);

        if (status == 0) {
            status = mcastRcvr_start(downlet.mcastInfo, down7.fmtpIface,
                    &downlet.ifaceCidr, down7.pq);

            if (status)
                backstop_stop();
        } // Backstop started

        if (status)
            ucastRcvr_stop();
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
    mcastRcvr_stop();
    backstop_stop();
    ucastRcvr_stop();
}

/**
 * Waits for the one-time, downstream LDM7 to complete.
 *
 * @retval 0            Success
 * @retval LDM7_SYSTEM  System failure. `log_add()` called.
 * @return              `downlet.taskStatus`
 */
static Ldm7Status
downlet_wait(void)
{
	while (!down7.done && downlet.taskStatus == 0) {
		int status = sem_wait(&down7.intrSem);
		if (status && errno != EINTR) {
			log_add_syserr("sem_wait() failure");
			return LDM7_SYSTEM;
		}
	}

	return downlet.taskStatus;
}

/**
 * Handles the termination of a concurrent task. If the task status is non-zero,
 * then
 *   - The status is saved if it's the first non-zero status; and
 *   - The one-time downstream LDM7 is signaled;
 * otherwise, nothing happens.
 *
 * @param[in] status   Status of terminated task
 * @asyncsignalsafety  Unsafe
 */
static void
downlet_taskCompleted(const Ldm7Status status)
{
    if (status) {
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

        if (cancelDownlet && sem_post(&down7.intrSem))
			log_syserr("sem_post() failure");
    } // Task status is non-zero
}

/**
 * Called by `backlogger_run()`.
 *
 * @param[in] backlog   Backlog specification
 * @retval    0         Success
 * @retval    LDM7_RPC  Error in RPC layer. `log_add()` called.
 */
static Ldm7Status
downlet_requestBacklog(const BacklogSpec* backlog)
{
    int         status;

    status = up7Proxy_requestBacklog(backlog);

    if (status)
        log_add("Couldn't request session backlog");

    return status;
}

/**
 * Returns a socket that's connected to an Internet server via TCP. This is a
 * potentially lengthy operation.
 *
 * @param[in]  ldmSrvr        Address of the LDM7 server.
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
downlet_getSocket(
    InetSockAddr* const restrict    ldmSrvr,
    const int                       family,
    int* const restrict             sock,
    struct sockaddr* const restrict sockAddr)
{
    struct sockaddr addr;
    int             status = isa_initSockAddr(ldmSrvr, family, false, &addr);

    if (status) {
        log_add("Couldn't initialize socket address from \"%s\"",
                isa_toString(ldmSrvr));
        status = LDM7_SYSTEM;
    }
    else {
        const int fd = socket(addr.sa_family, SOCK_STREAM, IPPROTO_TCP);

        if (fd == -1) {
            log_add_syserr("Couldn't create TCP socket");
            status = LDM7_SYSTEM;
        }
        else {
            if (connect(fd, &addr, sizeof(addr))) {
                log_add_syserr("connect() failure to \"%s\"",
                        isa_toString(ldmSrvr));

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

    // Potentially lengthy
    status = downlet_getSocket(down7.ldmSrvr, AF_INET, &sock, &sockAddr);

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
    log_debug("Entered");

    up7Proxy_destroy(); // Won't close externally-created socket
    (void)close(downlet.sock);

    downlet.sock = -1;

    smi_free(downlet.mcastInfo);
    downlet.mcastInfo = NULL;
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
 *                            `downlet_destroy()` returns. If the endpoint isn't
 *                            valid, then the VLAN virtual interface will not be
 *                            created.
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

    (void)memset(&downlet, 0, sizeof(downlet));

    downlet.sock = -1;
    downlet.feedId = feedtypet_format(down7.feedtype);

    if (downlet.feedId == NULL) {
        log_add("Couldn't format desired feed specification");
        status = LDM7_SYSTEM;
    }
    else {
        status = mutex_init(&downlet.mutex, PTHREAD_MUTEX_ERRORCHECK,
                true);

        if (status) {
            log_add("Couldn't initialize one-time, downstream LDM7 mutex");
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

    return status;
}

/**
 * Destroys the one-time, downstream LDM7.
 */
static void
downlet_destroy()
{
    (void)pthread_cond_destroy(&downlet.cond);
    (void)mutex_destroy(&downlet.mutex);
    free(downlet.feedId);
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
                downlet.feedId, isa_toString(down7.ldmSrvr));
    }
    else {
        /*
         * Blocks until error, reply, or timeout. Sets `downlet.mcastInfo`
         * and `downlet.fmtpAddr`. Potentially lengthy.
         */
        status = up7Proxy_subscribe(down7.feedtype, &down7.vcEnd,
                &downlet.mcastInfo, &downlet.ifaceCidr);

        if (status) {
            log_add("Couldn't subscribe to feed %s from %s",
                    downlet.feedId, isa_toString(down7.ldmSrvr));
        }
        else {
            char* const miStr = smi_toString(downlet.mcastInfo);
            char        ifaceCidrStr[CIDRSTRLEN];

            (void)cidrAddr_snprintf(&downlet.ifaceCidr, ifaceCidrStr,
                    sizeof(ifaceCidrStr));
            log_info("Subscription reply from %s: mcastGroup=%s, "
                    "ifaceCidrAddr=%s", isa_toString(down7.ldmSrvr), miStr,
                    ifaceCidrStr);
            free(miStr);

            {
                InetSockAddr* const fmtpSrvr =
                        smi_getFmtpSrvr(downlet.mcastInfo);

                if (inetId_compare(inAddrAny, isa_getInetId(fmtpSrvr)) == 0) {
                    status = isa_setInetId(fmtpSrvr,
                            isa_getInetId(down7.ldmSrvr));

                    if (status) {
                        log_add("Couldn't change address of FMTP server from "
                                "INADDR_ANY to %s", isa_toString(fmtpSrvr));
                    }
                    else {
                        log_notice("Address of FMTP server changed from "
                                "INADDR_ANY to %s", isa_toString(fmtpSrvr));
                    }
                }
            }

            if (status == 0) {
                status = downlet_startTasks();

                if (status) {
                    log_add("Couldn't create concurrent tasks for feed %s from "
                            "%s", downlet.feedId, isa_toString(down7.ldmSrvr));
                }
                else {
                    /*
                     * Returns when `down7_halt()` is called, either
                     * internally on a subtask's thread or externally.
                     */
                    status = downlet_wait();

                    log_debug("Status changed");
                    (void)downlet_stopTasks();
                } // Subtasks created

                smi_free(downlet.mcastInfo); // NULL safe
                downlet.mcastInfo = NULL;
            }
        } // `downlet.mcastInfo` set

        downlet_destroyClient();
    } // Client created

    return status;
}

/**
 * Executes the one-time, downstream LDM7.
 *
 * @retval 0              Success
 * @retval LDM7_SYSTEM    System error. `log_add()` called.
 * @retval LDM7_INTR      Signal caught. `log_add()` called.
 * @retval LDM7_INVAL     Invalid port number or host identifier. `log_add()`
 *                        called.
 * @retval LDM7_LOGIC     Logic error. `log_add()` called.
 * @retval LDM7_MCAST     Multicast layer failure. `log_add()` called.
 * @retval LDM7_RPC       RPC failure (including interrupt). `log_add()` called.
 * @retval LDM7_SYSTEM    System error. `log_add()` called.
 * @retval LDM7_NOENT     The upstream LDM7 doesn't multicast `feed`.
 *                        `log_add()` called.
 * @retval LDM7_SHUTDOWN  Shutdown requested
 * @retval LDM7_TIMEDOUT  Subscription request timed-out. `log_add()` called.
 * @retval LDM7_REFUSED   Upstream host refused connection (LDM7 not running?).
 *                        `log_add()` called.
 * @retval LDM7_UNAUTH    Upstream LDM7 denied request. `log_add()` called.
 */
static Ldm7Status
downlet_execute(void)
{
    int status = downlet_init();

    if (status) {
        log_add("Couldn't initialize one-time downstream LDM7");
    }
    else {
        status = downlet_run();

        downlet_destroy();
    } // One-time downstream LDM7 initialized

    return status;
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
 * @param[in] backlog      Product is part of the backlog? If `false`, then
 *                         product was received on the backstop thread.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
static int
downlet_recvProd(
        product* const prod,
        const bool     backlog)
{
    // Products are also inserted on the multicast-receiver thread
    pqueue* const restrict  pq = down7.pq;
    int                     status = pq_insert(pq, prod);

    if (status == 0) {
        if (log_is_enabled_info) {
            char buf[LDM_INFO_MAX];

            (void)s_prod_info(buf, sizeof(buf), &prod->info,
                    log_is_enabled_debug);
            log_info("%s: %s", backlog ? "Backlog" : "Inserted", buf);
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

    log_debug("Returning");
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
downlet_lastReceived(const prod_info* const last)
{
    // Calls mrm_getLastMcastProd()
    int status = mcastRcvr_lastReceived(last->signature, down7.mrm);

    if (status) {
        mutex_lock(&downlet.mutex);
            downlet.taskStatus = status;
        mutex_unlock(&downlet.mutex);

        (void)downlet_stopTasks();
        log_flush_error();
    }
    else {
        /*
         * Because mcastRcvr_lastReceived() calls mrm_getLastMcastProd(), the
         * following *must* be after mcastRcvr_lastReceived()
         */
        mrm_setLastMcastProd(down7.mrm, last->signature);
    }
}

/******************************************************************************
 * Downstream LDM7
 ******************************************************************************/

/**
 * Initializes this module.
 *
 * @param[in] ldmSrvr     Internet socket address of the LDM7 server
 * @param[in] feed        LDM feed
 * @param[in] fmtpIface   Virtual interface to be created for the FMTP
 *                        component to use or "dummy", indicating that no
 *                        virtual interface should be created, in which case
 *                        `vcEnd` must be invalid. Caller may free.
 * @param[in] vcEnd       Local virtual-circuit endpoint of AL2S VLAN. It must
 *                        be valid only if communication with the FMTP server
 *                        will be over an AL2S VLAN. Caller may free.
 * @param[in] pq          Product-queue for received data-products
 * @param[in] mrm         Persistent multicast receiver memory
 * @retval    0           Success
 * @retval    LDM7_INVAL  `fmtpIface` is inconsistent with `vcEnd`. `log_add()`
 *                        called.
 */
Ldm7Status
down7_init(
        InetSockAddr* const restrict        ldmSrvr,
        const feedtypet                     feed,
        const char* const restrict          fmtpIface,
        const VcEndPoint* const restrict    vcEnd,
        pqueue* const restrict              pq,
        McastReceiverMemory* const restrict mrm)
{
    log_debug("Entered. down7.done=%d", (int)down7.done);

    int status;

    log_assert(ldmSrvr);
    log_assert(fmtpIface);
    log_assert(vcEnd);
    log_assert(pq);
    log_assert(mrm);

    if (vcEndPoint_isValid(vcEnd) != (strcmp(fmtpIface, "dummy") != 0)) {
        char* vcEndStr = vcEndPoint_format(vcEnd);
        log_add("FMTP interface specification, %s, is inconsistent with "
                "local virtual-circuit endpoint, %s", fmtpIface, vcEndStr);
        free(vcEndStr);
        status = LDM7_INVAL;
    }
    else {
        status = mutex_init(&down7.mutex, PTHREAD_MUTEX_ERRORCHECK, true);

        if (status) {
            status = LDM7_SYSTEM;
        }
        else {
			/*
			 * The product-queue must be thread-safe because this module
			 * accesses it on these threads:
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
				if ((down7.ldmSrvr = isa_clone(ldmSrvr)) == NULL) {
					log_add("Couldn't clone LDM7 server address \"%s\"",
							isa_toString(ldmSrvr));
					status = LDM7_SYSTEM;
				}
				else {
					down7.fmtpIface = strdup(fmtpIface);

					if (down7.fmtpIface == NULL) {
						log_add("Couldn't copy FMTP interface name");
						status = LDM7_SYSTEM;
					}
					else {
						if (!vcEndPoint_copy(&down7.vcEnd, vcEnd)) {
							log_add("Couldn't copy local AL2S "
									"virtual-circuit endpoint");
							status = LDM7_SYSTEM;
						}
						else {
							if (inAddrAny == NULL) {
								inAddrAny = inetId_newFromStr("0.0.0.0");

								if (inAddrAny == NULL) {
									log_add("inetId_newFromStr() "
											"failure");
									status = LDM7_SYSTEM;
								}
								else if (sem_init(&down7.intrSem, 0, 0) ==
										-1) {
									log_add_syserr("Couldn't initialize "
											"semaphore");
									inetId_free(inAddrAny);
									inAddrAny = NULL;
									status = LDM7_SYSTEM;
								} // `inAddrAny` initialized
							} // `inAddrAny` uninitialized

							if (status == 0) {
								down7.pq = pq;
								down7.feedtype = feed;
								down7.mrm = mrm;
								down7.state = TASK_INITIALIZED;

								sigemptyset(&termMask);
								sigaddset(&termMask, SIGTERM);
							}

							if (status)
								vcEndPoint_destroy(&down7.vcEnd);
						} // `down7.vcEnd` initialized

						if (status)
							free(down7.fmtpIface);
					} // `down7.fmtpIface` set

					if (status) {
						isa_free(down7.ldmSrvr);
						down7.ldmSrvr = NULL;
					}
				} // `down7.servAddr` initialized
			} // Product-queue is thread-safe

            if (status)
                mutex_destroy(&down7.mutex);
        } // Downstream LDM7 mutex initialized
    } //  FMTP interface spec and local virtual-circuit endpoint are consistent

    log_debug("Returning. down7.done=%d", (int)down7.done);
    return status;
}

/**
 * Destroys the downstream LDM7 module.
 *
 * @asyncsignalsafety  Unsafe
 */
void
down7_destroy(void)
{
    log_debug("Entered");

    if (down7.state != TASK_UNINITIALIZED) {
		int status = mutex_lock(&down7.mutex);
			log_assert(status == 0);
			log_assert(down7.state != TASK_STARTED);

			if (sem_destroy(&down7.intrSem))
				log_syserr("Couldn't destroy semaphore");

			inetId_free(inAddrAny);
			inAddrAny = NULL;

			vcEndPoint_destroy(&down7.vcEnd);

			free(down7.fmtpIface);
			down7.fmtpIface = NULL;

			isa_free(down7.ldmSrvr);
			down7.ldmSrvr = NULL;

			down7.state = TASK_UNINITIALIZED;
		mutex_unlock(&down7.mutex);
		mutex_destroy(&down7.mutex);
    }

    log_debug("Returning");
}

/**
 * Sleeps for a given amount of time or until `down7.intrSem` can be locked --
 * whichever comes first.
 *
 * @param[in] amount       Amount of time, in seconds, to sleep
 * @retval    0            Time was slept or `down7.intrSem` was locked
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
down7_sleep(const unsigned amount)
{
	int             status;
	struct timespec until = {.tv_sec=time(NULL) + amount};

	for (;;) {
		status = sem_timedwait(&down7.intrSem, &until);

		if (status == 0)
			break;

		if (errno == EINTR)
			continue;
		if (errno == ETIMEDOUT) {
			status = 0;
			break;
		}

		log_add_syserr("sem_timedwait() failure");
		status = LDM7_SYSTEM;
		break;
	}

	return status;
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
 * @retval        LDM7_LOGIC   Logic error. `log_add()` called.
 * @retval        LDM7_RPC     RPC error. `log_add()` called.
 * @retval        LDM7_SYSTEM  System error. `log_add()` called.
 * @see `down7_halt()`
 */
Ldm7Status
down7_run()
{
    log_debug("Entered. down7.done=%d", (int)down7.done);

    int  status;

    mutex_lock(&down7.mutex);
        if (down7.state != TASK_INITIALIZED) {
            log_add("Module isn't initialized");
            status = LDM7_LOGIC;
        }
        else {
            down7.state = TASK_STARTED;
            down7.thread = pthread_self();
            status = 0;
        }
    mutex_unlock(&down7.mutex);

    if (status == 0) {
        char* feedId = feedtypet_format(down7.feedtype);
        char* vcEndId = vcEndPoint_format(&down7.vcEnd);
        log_notice("Downstream LDM7 starting up: remoteLDM7=%s, feed=%s, "
                "fmtpIface=%s, vcEndPoint=%s, pq=\"%s\"",
                isa_toString(down7.ldmSrvr), feedId, down7.fmtpIface, vcEndId,
                pq_getPathname(down7.pq));
        free(vcEndId);
        free(feedId);

        while (!down7.done) {
            status = downlet_execute(); // Indefinite execution

            mrm_restart(down7.mrm);

			if (!down7.done) {
				if (status == LDM7_TIMEDOUT || status == LDM7_MCAST) {
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

				// Problem might be temporary
				log_debug("Sleeping");
				status = down7_sleep(4*interval);
				if (status == LDM7_SYSTEM) {
					log_add("Couldn't sleep");
					break;
				}
			} // Not done yet
        } // One-time, downstream LDM7 execution loop

		mutex_lock(&down7.mutex);
			down7.state = TASK_STOPPED;
		mutex_unlock(&down7.mutex);
    } // Downstream LDM7 should be executed

    if (status && down7.done) {
    	log_flush_debug();
    	status = 0;
    }

    log_debug("Returning");

    return status;
}

/**
 * Indicates if the downstream LDM module has been initialized.
 *
 * @retval `false`  No
 * @retval `true`   Yes
 */
bool
down7_isInit()
{
	return down7.state != TASK_UNINITIALIZED;
}

/**
 * Stops the downstream LDM7 module from running. May be called from a signal
 * handler. Idempotent.
 *
 * @threadsafety       Safe
 * @asyncsignalsafety  Safe
 */
void
down7_halt()
{
	log_debug("down7.done=%d", (int)down7.done);
	down7.done = 1;

	if (down7.state != TASK_UNINITIALIZED) {
		/*
		 * An async-signal safe semaphore is used instead of an async-signal
		 * unsafe condition variable.
		 */
		if (sem_post(&down7.intrSem))
			log_syserr("sem_post() failure");

#if 0
		/*
		 * If the current thread is the one that's executing `down7_run()`, then
		 * the thread has been interrupted; consequently, it isn't signaled but,
		 * rather, relies on having been interrupted for termination.
		 */
		if (down7.thread && down7.thread != pthread_self())
			(void)pthread_kill(down7.thread, SIGTERM);
#endif
	}
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
 * product-queue by the downstream LDM7. Called by the one-time downstream LDM7
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
 * product-queue by a downstream LDM7 and resets the counter to zero. Used for
 * testing.
 *
 * @return  Number of products
 */
uint64_t
down7_getNumProds(void)
{
    (void)mutex_lock(&down7.mutex);
        uint64_t num = down7.numProds;
        down7.numProds = 0;
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

        if (downlet_recvProd(&missedProd->prod, false) != 0) {
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
    log_debug("Entered");

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

    log_debug("Returning");

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
    log_debug("Entered");

    int status = downlet_recvProd(prod, true);

    log_assert(status == 0);

    log_debug("Returning");

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
    log_debug("Entered");

    log_notice("All backlog data-products received: feed=%s, server=%s",
            s_feedtypet(down7.feedtype), isa_toString(down7.ldmSrvr));

    log_debug("Returning");

    return NULL; // causes RPC dispatcher to not reply
}

/**
 * Does nothing. Does not reply.
 *
 * @param[in] rqstp   Pointer to the RPC service-request.
 * @retval    NULL    Always.
 */
void*
test_connection_7_svc(
    void* const           no_op,
    struct svc_req* const rqstp)
{
    log_debug("Entered");
    log_debug("Returning");

    return NULL;                // don't reply
}
