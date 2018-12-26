/**
 * This file implements the upstream LDM-7. The upstream LDM-7
 *     - Is a child-process of the top-level LDM server;
 *     - Ensures that a multicast LDM sender processes is running for its
 *       associated multicast group;
 *     - Handles one and only one downstream LDM-7;
 *     - Implements a server on its TCP connection that accepts requests for
 *       files missed by the multicast component of its downstream LDM-7; and
 *     - Sends such files to its downstream LDM-7.
 *
 * NB: Using a single TCP connection and having both client-side and server-side
 * transports on both the upstream and downstream LDM-7s only works because,
 * after the initial subscription, all exchanges are asynchronous; consequently,
 * the servers don't interfere with the (non-existent) RPC replies.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: up7.c
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "ChildCommand.h"
#include "CidrAddr.h"
#include "globals.h"
#include "inetutil.h"
#include "ldm.h"
#include "LdmConfFile.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "pq.h"
#include "priv.h"
#include "prod_class.h"
#include "prod_index_map.h"
#include "prod_info.h"
#include "remote.h"
#include "rpcutil.h"
#include "timestamp.h"
#include "uldb.h"
#include "up7.h"
#include "UpMcastMgr.h"
#include "VirtualCircuit.h"

#include <arpa/inet.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <rpc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef _XOPEN_PATH_MAX
/* For some reason, the following isn't defined by gcc(1) 4.8.3 on Fedora 19 */
#   define _XOPEN_PATH_MAX 1024 // value mandated by XPG6; includes NUL
#endif

/******************************************************************************
 * Subscription Reply:
 ******************************************************************************/

/**
 * Returns the string representation of subscription reply.
 *
 * @param[in] reply  Subscription reply
 * @retval    NULL   System failure
 * @return           String representation of `reply`. Caller should free when
 *                   it's no longer needed.
 */
static char*
subRep_toString(const SubscriptionReply* const reply)
{
    log_assert(reply);

    char* str;

    if (reply->status) {
        str = ldm_format(128, "status=%d", reply->status);
    }
    else {
        char* const miStr = mi_format(&reply->SubscriptionReply_u.info.mcastInfo);
        char* const cidrStr =
                cidrAddr_format(&reply->SubscriptionReply_u.info.fmtpAddr);

        str = ldm_format(128, "{status=LDM7_OK, mcastSubInfo={mcastInfo=%s, "
                "cidrAddr=%s}}", miStr, cidrStr);

        free(cidrStr);
        free(miStr);
    }

    return str;
};

/******************************************************************************
 * Upstream LDM7:
 ******************************************************************************/

/// Module is initialized?
static bool               initialized = FALSE;

/// Separate, server-side RPC transport with downstream LDM7:
static SVCXPRT*           svcXprt = NULL;

/// Client-side RPC transport with downstream LDM-7:
static CLIENT*            clnt = NULL;

/// Feedtype of the subscription:
static feedtypet          feedtype = NONE;

/// Information on multicast
static McastInfo*         mcastInfo;

/// IP address of the client FMTP component:
static in_addr_t          fmtpClntAddr = INADDR_ANY;

/// Product-index map is open?
static bool               pimIsOpen = false;

/// Reply to subscription request:
static SubscriptionReply* subReply = NULL;

/// This module is done?
static bool               isDone = false;

/**
 * Opens the product-index map associated with a feedtype.
 *
 * @param[in] feed         The feedtype.
 * @retval    0            Success.
 * @retval    LDM7_LOGIC   The product-index map is already open. `log_add()`
 *                         called.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called. The state of the
 *                         associated file is unspecified.
 */
static int
openProdIndexMap(
        const feedtypet   feed)
{
    char pathname[_XOPEN_PATH_MAX];

    (void)strncpy(pathname, getQueuePath(), sizeof(pathname));
    pathname[sizeof(pathname)-1] = 0;

    int status = pim_openForReading(dirname(pathname), feed);

    if (status == 0)
        pimIsOpen = true;

    return status;
}

/**
 * Closes the open product-index map. Registered by `atexit()`. Idempotent.
 */
static void
closeProdIndexMap()
{
    if (pimIsOpen) {
        if (pim_close()) {
            char feedStr[256];
            int  nbytes = ft_format(feedtype, feedStr, sizeof(feedStr));
            if (nbytes == -1 || nbytes >= sizeof(feedStr)) {
                log_error("Couldn't close product-index map for feed %#lx",
                        (unsigned long)feedStr);
            }
            else {
                log_error("Couldn't close product-index map for feed %s", feedStr);
            }
        }
        else {
            pimIsOpen = false;
        }
    }
}

/**
 * Ensures that the global product-queue is closed at process termination.
 * Referenced by `atexit()`.
 */
static void
closePq(void)
{
    if (pq) {
        if (pq_close(pq)) {
            log_error("Couldn't close global product-queue");
        }
        pq = NULL;
    }
}

/**
 * Ensures that the product-queue is open for reading.
 *
 * @retval false  Failure.   `log_add()` called.
 * @retval true   Success.
 */
static bool
ensureProductQueueOpen(void)
{
    bool success;

    if (pq) {
        success = true;
    }
    else {
        const char* const pqPath = getQueuePath();
        int               status = pq_open(pqPath, PQ_READONLY, &pq);

        if (status) {
            if (PQ_CORRUPT == status) {
                log_add("The product-queue \"%s\" is corrupt", pqPath);
            }
            else {
                log_add("Couldn't open product-queue \"%s\": %s", pqPath);
            }
            success = false;
        }
        else {
            if (atexit(closePq)) {
                log_add_syserr("Couldn't register product-queue closing "
                        "function");
                success = false;
            }
            else {
                success = true;
            }
        }
    }

    return success;
}

/**
 * Creates the client-side RPC transport on the TCP connection of the
 * server-side RPC transport.
 *
 * @retval 0           Success
 * @retval LDM7_SYSTEM System error. `log_add()` called.
 */
static Ldm7Status
initClient()
{
    log_debug("Entered");

    log_assert(svcXprt);
    log_assert(svcXprt->xp_raddr.sin_port != 0);
    log_assert(svcXprt->xp_sock >= 0); // So client-error won't close socket

    int status;

    // `xprt->xp_sock >= 0` => socket won't be closed by client-side error
    // TODO: adjust sending buffer size
    CLIENT* client = clnttcp_create(&svcXprt->xp_raddr, LDMPROG, SEVEN,
            &svcXprt->xp_sock, MAX_RPC_BUF_NEEDED, 0);

    if (client == NULL) {
        log_assert(rpc_createerr.cf_stat != RPC_TIMEDOUT);
        log_add("Couldn't create client-side transport to downstream LDM-7 on "
                "%s%s", hostbyaddr(&svcXprt->xp_raddr), clnt_spcreateerror(""));

        status = LDM7_RPC;
    }
    else {
        clnt = client;
        status = 0;
    } // Client-side transport created

    log_debug("Returning %d", status);
    return status;
}

/**
 * Destroys the separate, client-side RPC transport for unicasting missed
 * data-products to the downstream LDM7. Idempotent.
 */
static void
destroyClient(void)
{
    if (clnt) {
        // Doesn't check for `NULL`. Doesn't close socket. Frees `clnt`.
        clnt_destroy(clnt);
        clnt = NULL;
    }
}

/**
 * Adds an upstream LDM7 process to the upstream LDM database.
 *
 * @param[in] clientAddr   Socket address of the downstream LDM7 client
 * @param[in] feed         LDM7 feed
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
addToUldb(
        const struct sockaddr_in* const clientAddr,
        const feedtypet                 feed)
{
    int status;

    prod_class* const prodCls = dup_prod_class(&_clss_all);

    if (prodCls == NULL) {
        log_add("dup_prod_class() failure");
        status = LDM7_SYSTEM;
    }
    else {
        prodCls->psa.psa_val->feedtype = feed;

        if (uldb_addProcess(getpid(), 7, clientAddr, prodCls, NULL, false,
                true)) {
            log_add("uldb_addProcess() failure");
            status = LDM7_SYSTEM;
        }
        else {
            status = 0;
        }

        free_prod_class(prodCls);
    } // `prodCls` allocated

    return status;
}

/**
 * Reduces the feed requested by a host according to what it is allowed to
 * receive.
 * @param[in] feed    Feed requested by host
 * @param[in] inAddr  Address of the host
 * @return            Reduced feed. Might be `NONE`.
 */
static feedtypet
reduceFeed(
        feedtypet                            feed,
        const struct in_addr* const restrict inAddr)
{
    static const size_t maxFeeds = 128;
    feedtypet           allowedFeeds[maxFeeds];
    size_t              numFeeds = lcf_getAllowedFeeds(remote_name(), inAddr,
            maxFeeds, allowedFeeds);
    if (numFeeds > maxFeeds) {
        log_error("numFeeds (%u) > maxFeeds (%d)", numFeeds, maxFeeds);
        numFeeds = maxFeeds;
    }
    return lcf_reduceFeed(feed, allowedFeeds, numFeeds);
}

/**
 * Initializes the separate, server-side RPC transport. A separate transport is
 * created because `svc_getreqsock()` and `svc_getreqset()` mustn't be nested
 * because they destroy the transport on error and the outer one will,
 * consequently, cause a segfault.
 *
 * @param[in] xprt         Initial, server-side RPC transport
 * @retval    0            Success
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 * @retval    LDM7_RPC     Failure in RPC component. `log_add()` called.
 */
static int
initXprt(const SVCXPRT* const xprt)
{
    log_debug("Entered");
    log_assert(xprt);

    int status;
    int sock = dup(xprt->xp_sock);

    if (sock == -1) {
        log_add_syserr("dup() failure");
        status = LDM7_SYSTEM;
    }
    else {
        svcXprt = svcfd_create(sock, 0, 0);

        if (svcXprt == NULL) {
            log_add("svcfd_create() failure");
            (void)close(sock);
            status = LDM7_RPC;
        }
        else {
            // svcfd_create() doesn't initialize remote address
            svcXprt->xp_raddr = xprt->xp_raddr;
            svcXprt->xp_addrlen = sizeof(xprt->xp_raddr);

            // `ldmprog_7()` is already registered with RPC module

            status = 0;
        } // Separate, server-side RPC transport created
    } // Socket duplicated

    log_debug("Returning %d", status);
    return status;
}

/**
 * Destroys the separate, server-side RPC transport if appropriate. Idempotent.
 */
static void
destroyXprt()
{
    if (svcXprt) {
        // NB: svc_destroy() must only be called once per transport
        svc_destroy(svcXprt);
        svcXprt = NULL;
    }
}

/**
 * Finishes initializing this module:
 *   - Opens the product-to-index map
 *   - Initializes a separate, client-side RPC transport for unicasting missed
 *     data-products to the downstream LDM7
 *   - Sets the file-scoped variables `feedtype` and `fmtpClntAddr`
 *   - Sets the reply to the RPC client
 *   - Sets `isInitialized` to true
 *
 * @param[in]  fmtpClntCidr  Address for FMTP client
 * @param[out] reply         RPC reply. Only modified on success.
 * @retval     0             Success. `*reply` is set.
 * @retval     LDM7_LOGIC    Logic error. `log_add()` called.
 * @retval     LDM7_RPC      Couldn't create client-side RPC transport.
 *                           `log_add()` called.
 * @retval     LDM7_SYSTEM   System error. `log_add()` called.
 */
static int
init2(  const CidrAddr* const restrict    fmtpClntCidr,
        SubscriptionReply* const restrict reply)
{
    // The cleanup() function in ldmd.c destroys this instance

    log_assert(!initialized);
    log_assert(svcXprt);
    log_assert(mcastInfo);
    log_assert(remote_name());
    log_assert(fmtpClntCidr);
    log_assert(reply);

    int       status;
    status = openProdIndexMap(mcastInfo->feed);

    if (status) {
        log_add("Couldn't open product-to-index map for feed %s",
                s_feedtypet(mcastInfo->feed));
    }
    else {
        if (!ensureProductQueueOpen()) {
            status = LDM7_PQ;
        }
        else {
            status = initClient();

            if (status) {
                log_add("Couldn't create client-side RPC transport to "
                        "downstream host %s", remote_name());
            }
            else {
                // Failure is ignored to enable testing
                if (addToUldb(&svcXprt->xp_raddr, mcastInfo->feed)) {
                    log_add("Couldn't add LDM7 process for client %s, feed "
                            "%s to upstream LDM database", remote_name(),
                            s_feedtypet(mcastInfo->feed));
                    log_flush_error();
                }

                reply->SubscriptionReply_u.info.mcastInfo = *mcastInfo;
                reply->SubscriptionReply_u.info.fmtpAddr = *fmtpClntCidr;
                reply->status = LDM7_OK;
                feedtype = mcastInfo->feed;
                fmtpClntAddr = cidrAddr_getAddr(fmtpClntCidr);
                initialized = true;
            } // Client-side transport created

            if (status)
                closePq();
        } // Product-queue opened

        if (status)
            closeProdIndexMap();
    } // Product-index map is opened

    return status;
}

/**
 * Initializes this module.
 *   - Creates a separate, server-side RPC transport from the initial transport
 *   - Starts the multicast sender if necessary
 *   - Gets a CIDR address for the FMTP client if appropriate
 *   - Opens the product-to-index map
 *   - Sets the file-scoped variables `feedtype` and `downFmtpAddr`
 *   - Sets the reply to the RPC client
 *   - Sets `isInitialized` to true
 *
 * @param[in]  xprt         Server-side RPC transport
 * @param[in]  desiredFeed  Multicast feed desired by downstream client
 * @param[in]  fmtVcEnd     Remote endpoint of the AL2S virtual circuit or
 *                          `NULL`. Necessary only if the multicast LDM sender
 *                          associated with `feed` multicasts on an AL2S
 *                          multipoint VLAN.
 * @param[out] reply        RPC reply. Only modified on success.
 * @retval     0            Success. `*reply` is set.
 * @retval     LDM7_LOGIC   Logic error. `log_add()` called.
 * @retval     LDM7_PQ      Couldn't open product-queue. `log_add()` called.
 * @retval     LDM7_RPC     Couldn't create client-side RPC handle. `log_add()`
 *                          called.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
static int
init(   struct SVCXPRT* const restrict    xprt,
        const feedtypet                   desiredFeed,
        const VcEndPoint* const restrict  rmtVcEnd,
        SubscriptionReply* const restrict reply)
{
    log_assert(!initialized);
    log_assert(xprt);
    log_assert(rmtVcEnd);
    log_assert(reply);

    int                         status;
    struct sockaddr_in* const   sockAddr = svc_getcaller(xprt);
    const struct in_addr* const hostAddr = &sockAddr->sin_addr;
    const feedtypet             reducedFeed = reduceFeed(desiredFeed, hostAddr);

    if (reducedFeed == NONE) {
        log_notice("Host %s isn't allowed to receive any part of feed %s",
                remote_name(), s_feedtypet(desiredFeed));
        reply->status = LDM7_UNAUTH;
        status = 0;
    }
    else {
        const SepMcastInfo* smi;
        CidrAddr            fmtpClntCidr;

        status = umm_subscribe(reducedFeed, hostAddr->s_addr, rmtVcEnd, &smi,
                &fmtpClntCidr);

        if (LDM7_NOENT == status) {
            log_flush_notice();
            reply->status = LDM7_NOENT;
            status = 0;
        }
        else if (status) {
            log_add("Couldn't subscribe host %s to feed %s", remote_name(),
                    s_feedtypet(reducedFeed));
        }
        else {
            if (mi_new(&mcastInfo, reducedFeed,
                    isa_toString(smi_getMcastGrp(smi)),
                    isa_toString(smi_getFmtpSrvr(smi)))) {
                log_add("Couldn't set multicast information");
                status = LDM7_SYSTEM;
            }
            else {
                status = initXprt(xprt); // Initializes `svcXprt`

                if (status == 0) {
                    status = init2(&fmtpClntCidr, reply);

                    if (status)
                        destroyXprt(); // Destroys `svcXprt`
                } // Separate, server-side RPC transport initialized

                if (status) {
                    mi_free(mcastInfo);
                    mcastInfo = NULL;
                }
            } // Multicast information initialized

            if (status)
                (void)umm_unsubscribe(reducedFeed,
                        cidrAddr_getAddr(&fmtpClntCidr));
        } // `umm_subscribe()` successful
    } // Client is allowed to receive something

    return status;
}

/**
 * Destroys this module. Idempotent.
 */
static void
destroy(void)
{
    log_debug("Entered");

    if (initialized) {
        destroyClient(); // Closes socket
        closePq();
        closeProdIndexMap();

        mi_free(mcastInfo);
        mcastInfo = NULL;

        (void)umm_unsubscribe(feedtype, fmtpClntAddr);
        log_clear();

        if (subReply) {
            xdr_free(xdr_SubscriptionReply, subReply);
            subReply = NULL;
        }

        // NB: svc_destroy() must only be called once per transport
        destroyXprt(); // Ensures destruction if appropriate

        isDone = false;
        initialized = false;
    }

    log_debug("Returning");
}

/**
 * Runs the upstream LDM server. The server-side RPC transport is always
 * destroyed on return.
 *
 * @retval    ECONNRESET     The connection to the client LDM was lost.
 *                           `svc_destroy(svcXprt)` called. `log_add()` called.
 * @retval    EBADF          The socket isn't open. `log_add()` called.
 * @retval    EPIPE          Testing the connection failed. `log_add()` called.
 */
static int
runSvc(void)
{
    log_debug("Entered");

    log_assert(svcXprt);
    log_assert(remote_name());

    int            status;
    const int      sock = svcXprt->xp_sock;
    const unsigned TIMEOUT = 2*interval; // 60 seconds

    do {
        status = one_svc_run(sock, TIMEOUT); // Exits if `done` set

        if (status == ECONNRESET) { // Connection to client lost
            /*
             * one_svc_run() called svc_getreqsock(), which called
             * svc_destroy(svcXprt), which must only be called once per
             * transport
             */
            log_add("Connection with client LDM, %s, has been lost",
                    remote_name());
            svcXprt = NULL; // Let others know
        }
        else if (status == ETIMEDOUT) {
            log_debug("Client LDM, %s, has been silent for %u seconds",
                    remote_name(), TIMEOUT);
            (void)test_connection_7(NULL, clnt);

            /*
             * The status will be RPC_TIMEDOUT unless an error occurs because
             * the RPC call uses asynchronous message-passing.
             */
            if (clnt_stat(clnt) == RPC_TIMEDOUT) {
                status = 0;
            }
            else {
                log_add("Connection with downstream LDM-7 is broken: %s",
                        clnt_errmsg(clnt));
                status = EPIPE;
            }
        } // one_svc_run() timed-out
        else {
            log_add("Error running upstream LDM7 server");
        }
    } while (status == 0);

    if (svcXprt) {
        svc_destroy(svcXprt);
        svcXprt = NULL;
    }

    log_debug("Returning");

    return status;
}

/**
 * Indicates if the caller should send a reply to the downstream LDM7. Will
 * start a separate upstream LDM7 server if appropriate and not return until
 * that server terminates.
 *
 *
 * @param[in] xprt     Initial, server-side RPC transport
 * @param[in] request  Subscription request
 * @param[in] result   Reply to be sent if appropriate
 * @retval    NULL     Do not reply to the client
 * @return             Response to be sent to the client
 */
static SubscriptionReply*
subscribe(
        SVCXPRT* const restrict           xprt,
        McastSubReq* const restrict       request,
        SubscriptionReply* const restrict result)
{
    log_debug("Entered");
    log_assert(xprt);
    log_assert(request);
    log_assert(result);

    SubscriptionReply* reply = NULL;

    if (init(xprt, request->feed, &request->vcEnd, result)) {
        log_add("Couldn't initialize the upstream LDM7 module");
        log_flush_error();
        svcerr_systemerr(xprt);
    }
    else {
        if (result->status == LDM7_OK) {
            if (!svc_sendreply(xprt, (xdrproc_t)xdr_SubscriptionReply,
                    (char*)result)) {
                log_error("Couldn't send subscription reply to client");
                svcerr_systemerr(xprt);
            }
            else {
                runSvc();
                log_flush_error();
            }
        }
        else {
            reply = result; // Do respond to client
        }

        destroy();
    } // Module is initialized

    log_debug("Returning %p", reply);
    return reply;
}

/**
 * Delivers a data-product to the associated downstream LDM-7. Called by
 * `pq_processProdBySig()`.
 *
 * @param[in] info         Data-product metadata.
 * @param[in] data         Data-product data.
 * @param[in] xprod        XDR-encoded data-product.
 * @param[in] len          Size of XDR-encoded data-product in bytes.
 * @param[in] optArg       Pointer to associated FMTP product-index.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  Failure. `log_add()` called.
 */
static int
deliverProduct(
        const prod_info* const restrict info,
        const void* const restrict      data,
	void* const restrict            xprod,
	const size_t                    len,
	void* const restrict            optArg)
{
    int           status;
    MissedProduct missedProd;

    missedProd.iProd = *(FmtpProdIndex*)optArg;
    missedProd.prod.info = *info;
    missedProd.prod.data = (void*)data; // cast away `const`

    log_debug("Delivering: iProd=%lu, ident=\"%s\"", missedProd.iProd,
            info->ident);
    (void)deliver_missed_product_7(&missedProd, clnt);

    /*
     * The status will be RPC_TIMEDOUT unless an error occurs because the RPC
     * call uses asynchronous message-passing.
     */
    if (clnt_stat(clnt) != RPC_TIMEDOUT) {
        log_add("Couldn't RPC to downstream LDM-7: %s", clnt_errmsg(clnt));
        status = LDM7_SYSTEM;
    }
    else {
        log_info("Missed product sent: %s",
                s_prod_info(NULL, 0, &missedProd.prod.info,
                log_is_enabled_debug));
        status = 0;
    }

    return status;
}

/**
 * Sends the data-product corresponding to a multicast product-index to the
 * associated downstream LDM-7.
 *
 * @param[in]  iProd        Product-index.
 * @retval     0            Success.
 * @retval     LDM7_NOENT   No corresponding data-product. `log_add()` called.
 * @retval     LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
sendProduct(
        FmtpProdIndex iProd)
{
    signaturet sig;
    int        status = pim_get(iProd, &sig);

    if (LDM7_NOENT == status) {
        log_add("No signature in product-index map corresponding to index %lu",
                (unsigned long)iProd);
    }
    else if (0 == status) {
        status = pq_processProduct(pq, sig, deliverProduct, &iProd);

        if (PQ_NOTFOUND == status) {
            char buf[sizeof(signaturet)*2+1];

            (void)sprint_signaturet(buf, sizeof(buf), sig);
            log_add("No data-product corresponding to signature %s: "
                    "prodIndex=%lu", buf, (unsigned long)iProd);
            status = LDM7_NOENT;
        }
        else if (status) {
            status = LDM7_SYSTEM;
        }
    }

    return status;
}

/**
 * Finds a data-product corresponding to a product-index. If found, then it
 * is sent to to the downstream LDM-7 via the client-side transport; otherwise,
 * the downstream LDM-7 is notified that no corresponding data-product exists.
 *
 * @param[in] iProd   Product-index.
 * @retval    true    Success. Either the product or a notice of unavailability
 *                    was sent to the client.
 * @retval    false   Failure. `log_add()` called.
 */
static bool
findAndSendProduct(
    FmtpProdIndex iProd)       // not `const` because of `no_such_product_7()`
{
    int status = sendProduct(iProd);

    if (LDM7_NOENT == status) {
        log_flush_info();
        (void)no_such_product_7(&iProd, clnt);

        if (clnt_stat(clnt) == RPC_TIMEDOUT) {
            status = 0;
        }
        else {
            /*
             * The status will be RPC_TIMEDOUT unless an error occurs because
             * the RPC call uses asynchronous message-passing.
             */
            log_add("Couldn't RPC to downstream LDM-7: %s", clnt_errmsg(clnt));
        }
    }

    return 0 == status;
}

/**
 * Sets the cursor of the product-queue to just after the data-product with a
 * given signature.
 *
 * @param[in] after        Data-product signature.
 * @retval    0            Success.
 * @retval    LDM7_NOENT   Corresponding data-product not found.
 * @retval    LDM7_SYSTEM  Failure. `log_add()` called.
 */
static Ldm7Status
setCursorFromSignature(
        const signaturet after)
{
    log_debug("Entered. after=%s", s_signaturet(NULL, 0, after));

    int status;

    switch ((status = pq_setCursorFromSignature(pq, after))) {
    case 0:
        log_debug("Returning 0");
        return 0;
    case PQ_NOTFOUND:
        log_info("Data-product with signature %s wasn't found in product-queue",
                s_signaturet(NULL, 0, after));
        log_debug("Returning LDM7_NOENT");
        return LDM7_NOENT;
    default:
        log_add("Couldn't set product-queue cursor from signature %s: %s",
                s_signaturet(NULL, 0, after), pq_strerror(pq, status));
        log_debug("Returning LDM7_SYSTEM");
        return LDM7_SYSTEM;
    }
}

/**
 * Sets the cursor of the product-queue to point a time-offset older than now.
 *
 * @param[in] offset  Time offset in seconds.
 * @retval    true    Success.
 * @retval    false   Failure. `log_add()` called.
 */
static void
setCursorFromTimeOffset(
        const unsigned offset)
{
    timestampt ts;

    (void)set_timestamp(&ts);

    ts.tv_sec = (offset < ts.tv_sec) ? (ts.tv_sec - offset) : 0;

    pq_cset(pq, &ts);
}

/**
 * Sets the cursor of the product-queue from a backlog specification.
 *
 * @param[in] backlog  Backlog specification.
 * @retval    true     Success.
 * @retval    false    Failure. `log_add()` called.
 */
static bool
setProductQueueCursor(
        const BacklogSpec* const restrict backlog)
{
    log_debug("Entered");

    if (backlog->afterIsSet) {
        switch (setCursorFromSignature(backlog->after)) {
        case 0:
            log_debug("Returning true");
            return true;
        case LDM7_NOENT:
            break;
        default:
            log_debug("Returning false");
            return false;
        }
    }

    setCursorFromTimeOffset(backlog->timeOffset);

    log_debug("Returning true");
    return true;
}

/**
 * Sends a data-product to the downstream LDM-7 if it doesn't have a given
 * signature.
 *
 * Called by `pq_sequence()`.
 *
 * @param[in] info         Data-product's metadata.
 * @param[in] data         Data-product's data.
 * @param[in] xprod        XDR-encoded version of data-product (data and metadata).
 * @param[in] size         Size, in bytes, of XDR-encoded version.
 * @param[in] arg          Signature.
 * @retval    0            Success.
 * @retval    EEXIST       Data-product has given signature. Not sent.
 * @retval    EIO          Couldn't send to downstream LDM-7
 * @return                 `pq_sequence()` return value
 */
static int
sendIfNotSignature(
    const prod_info* const restrict info,
    const void* const restrict      data,
    void* const restrict            xprod,
    const size_t                    size,
    void* const restrict            arg)
{
    log_debug("Entered");

    int               status;
    const signaturet* sig = (const signaturet*)arg;

    if (0 == memcmp(sig, info->signature, sizeof(signaturet))) {
        status = EEXIST;
    }
    else {
        product prod;
        prod.info = *info;
        prod.data = (void*)data;    // cast away `const`

        deliver_backlog_product_7(&prod, clnt);

        /*
         * The status will be RPC_TIMEDOUT unless an error occurs because the
         * RPC call uses asynchronous message-passing.
         */
        if (clnt_stat(clnt) != RPC_TIMEDOUT) {
            log_add("Couldn't send backlog data-product to downstream LDM-7: "
                    "%s", clnt_errmsg(clnt));
            status = EIO;
        }
        else {
            log_notice("Backlog product sent: %s", s_prod_info(NULL, 0, info,
                    log_is_enabled_debug));
            status = 0;
        }
    }

    log_debug("Returning %d", status);
    return status;
}

/**
 * Sends all data-products of the given feedtype in the product-queue from the
 * current cursor position up to (but not including) the data-product with a
 * given signature.
 *
 * @param[in] before       Signature of data-product at which to stop sending.
 * @retval    0            Success.
 * @retval    LDM7_NOENT   Data-product with given signature not found before
 *                         end of queue reached. `log_info()` called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
static Ldm7Status
sendUpToSignature(
        const signaturet* const before)
{
    log_debug("Entered");

    // `dup_prod_class()` compiles the patterns
    prod_class_t* const prodClass = dup_prod_class(PQ_CLASS_ALL);

    if (NULL == prodClass) {
        log_debug("Returning LDM7_SYSTEM");
        return LDM7_SYSTEM;
    }

    prodClass->psa.psa_val->feedtype = feedtype;        // was `ANY`

    int  status;
    for (;;) {
        status = pq_sequence(pq, TV_GT, prodClass, sendIfNotSignature,
                (signaturet*)before);                   // cast away `const`
        if (status == EEXIST) {
            status = 0;
            break;
        }
        if (status == PQUEUE_END) {
            log_info("End-of-backlog product not found before end-of-queue");
            status = LDM7_NOENT;
            break;
        }
        if (status) {
            status = LDM7_SYSTEM;
            break;
        }
    }

    free_prod_class(prodClass);

    log_debug("Returning %d", status);
    return status; // TODO
}

/**
 * Asynchronously sends a backlog of data-products that were missed by a
 * downstream LDM-7 due to a new session being started.
 *
 * @pre                {Client-side transport exists}
 * @pre                {Product-queue is open for reading}
 * @param[in] backlog  Specification of data-product backlog.
 * @param[in] rqstp    RPC service-request.
 * @retval    true     Success.
 * @retval    false    Failure. `log_add()` called.
 */
static bool
sendBacklog(
        const BacklogSpec* const restrict backlog)
{
    log_debug("Entered");

    if (!setProductQueueCursor(backlog))
        return false;

    const bool success = LDM7_SYSTEM != sendUpToSignature(&backlog->before);

    log_debug("Returning %d", success);
    return success;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Synchronously subscribes a downstream LDM-7 to a feed. Called by the RPC
 * dispatch function `ldmprog_7()`.
 *
 * @param[in] request     Subscription request
 * @param[in] rqstp       RPC service-request
 * @retval    NULL        System error. `log_flush()` and `svcerr_systemerr()`
 *                        called. No reply should be sent to the downstream
 *                        LDM-7.
 * @return                Reason for not honoring the subscription request
 * @threadsafety          Compatible but not safe
 */
SubscriptionReply*
subscribe_7_svc(
        McastSubReq* const restrict    request,
        struct svc_req* const restrict rqstp)
{
    log_debug("Entered");

    svc_setremote(rqstp);

    struct SVCXPRT* const xprt = rqstp->rq_xprt;
    const char* const     feedspec = s_feedtypet(request->feed);

    log_notice("Incoming subscription request from %s:%u for feed %s",
            remote_name(), ntohs(xprt->xp_raddr.sin_port), feedspec);

    if (subReply) {
        xdr_free(xdr_SubscriptionReply, subReply); // Free any previous use
        subReply = NULL;
    }

    static SubscriptionReply result;

    subReply = subscribe(xprt, request, &result);

    if (subReply) {
        char* const subRepStr = subRep_toString(subReply);
        log_debug("Returning %s", subRepStr);
        free(subRepStr);
    }
    else if (log_is_enabled_debug) {
        log_debug("Returning NULL");
    }

    return subReply;
}

/**
 * Destroys this module. Idempotent.
 */
void
up7_destroy(void)
{
    log_debug("Entered");
    destroy();
    log_debug("Returning");
}

/**
 * Returns the process identifier of the associated multicast LDM sender.
 *
 * @retval 0      Multicast LDM sender doesn't exist
 * @return        PID of multicast LDM sender
 * @threadsafety  Safe
 */
pid_t
up7_mldmSndrPid(void)
{
    return initialized ? umm_getMldmSenderPid() : 0;
}

/**
 * Asynchronously sends a data-product that the associated downstream LDM-7 did
 * not receive via multicast. Called by the RPC dispatch function `ldmprog_7()`.
 *
 * @param[in] iProd   Index of missed data-product.
 * @param[in] rqstp   RPC service-request.
 * @retval    NULL    Always.
 */
void*
request_product_7_svc(
    FmtpProdIndex* const iProd,
    struct svc_req* const rqstp)
{
    log_debug("Entered: iProd=%lu", (unsigned long)*iProd);

    if (!initialized) {
        log_warning("Client %s hasn't subscribed yet", rpc_getClientId(rqstp));
        isDone = true;
    }
    else {
        if (!findAndSendProduct(*iProd)) {
            log_flush_error();
            destroyClient();
            isDone = true;
        }
    }

    log_debug("Returning");

    return NULL; // Don't reply
}

/**
 * Asynchronously sends a backlog of data-products that were missed by a
 * downstream LDM-7 due to a new session being started. Called by the RPC
 * dispatch function `ldmprog_7()`.
 *
 * @param[in] backlog  Specification of data-product backlog.
 * @param[in] rqstp    RPC service-request.
 * @retval    NULL     Always.
 */
void*
request_backlog_7_svc(
    BacklogSpec* const    backlog,
    struct svc_req* const rqstp)
{
    log_debug("Entered");

    if (clnt == NULL) {
        log_warning("Client %s hasn't subscribed yet", rpc_getClientId(rqstp));
        isDone = true;
    }
    else if (!sendBacklog(backlog)) {
        log_flush_error();
        destroyClient();
        isDone = true;
    }

    log_debug("Returning");

    return NULL;                // don't reply
}
