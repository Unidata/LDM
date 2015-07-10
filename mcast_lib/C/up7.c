/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: up7.c
 * @author: Steven R. Emmerson
 *
 * This file implements the upstream LDM-7, which
 *     - Is a child-process of the top-level LDM server;
 *     - Ensures that a multicast LDM sender processes is running for its
 *       associated multicast group;
 *     - Handles one and only one downstream LDM-7;
 *     - Runs a server on its TCP connection that accepts requests for files
 *       missed by the multicast component of its downstream LDM-7; and
 *     - Sends such files to its downstream LDM-7.
 *
 * NB: Using a single TCP connection and having both client-side and server-side
 * transports on both the upstream and downstream LDM-7s only works because,
 * after the initial subscription, all exchanges are asynchronous; consequently,
 * the servers don't interfere with the (non-existent) RPC replies.
 */

#include "config.h"

#include "prod_index_map.h"
#include "globals.h"
#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "mldm_sender_manager.h"
#include "mcast_info.h"
#include "pq.h"
#include "prod_class.h"
#include "rpcutil.h"
#include "timestamp.h"
#include "up7.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <rpc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/**
 * The RPC client-side transport to the downstream LDM-7
 */
static CLIENT*   clnt;
/**
 * The feedtype of the subscription.
 */
static feedtypet feedtype;

static void up7_destroyClient(void)
{
    if (clnt) {
        clnt_destroy(clnt);
        clnt = NULL;
    }
}

/**
 * Creates a client-side transport on the TCP connection.
 *
 * @param[in] xprt      Server-side RPC transport.
 * @retval    true      Success.
 * @retval    false     System error. `log_start()` called.
 */
static bool
up7_createClientTransport(
        struct SVCXPRT* const xprt)
{
    bool success;

    /*
     * Create a client-side RPC transport on the TCP connection.
     */
    UASSERT(clnt == NULL);
    UASSERT(xprt->xp_raddr.sin_port != 0);
    UASSERT(xprt->xp_sock >= 0);
    // `xprt->xp_sock >= 0` => socket won't be closed by client-side error
    // TODO: adjust sending buffer size
    clnt = clnttcp_create(&xprt->xp_raddr, LDMPROG, SEVEN, &xprt->xp_sock,
            MAX_RPC_BUF_NEEDED, 0);
    if (clnt == NULL) {
        UASSERT(rpc_createerr.cf_stat != RPC_TIMEDOUT);
        LOG_START2("Couldn't create client-side transport to downstream LDM-7 on "
                "%s%s", hostbyaddr(&xprt->xp_raddr), clnt_spcreateerror(""));
        success = false;
    }
    else {
        if (atexit(up7_destroyClient)) {
            LOG_SERROR0("Couldn't register upstream LDM-7 cleanup function");
            log_log(LOG_ERR);
            up7_destroyClient();
            success = false;
        }
        else {
            success = true;
        }
    }

    return success;
}

/**
 * Sets information on a multicast group suitable for a subscribing client from
 * a server's perspective of the information. Specifically, if the server will
 * listen on all interfaces, then the Internet identifier of the server will be
 * set to the canonical name of the local host; otherwise, the Internet
 * identifier is used unmodified.
 *
 * @param[out] clientView   Client-side multicast information. Call should call
 *                          `xdr_free(xdr_SubscriptionReply, clientView)` when
 *                          it's no longer needed.
 * @param[in]  serverView   Server-side multicast information. Caller may
 *                          modify or free.
 * @retval     0            Success. `*clientView` is set.
 * @retval     LDM7_SYSTEM  System failure. `log_start()` called.
 */
static Ldm7Status
up7_setMcastInfo(
        McastInfo* const restrict       clientView,
        const McastInfo* const restrict serverView)
{
    int              status;
    McastInfo* const info = mi_clone(serverView);

    if (NULL == info) {
        status = LDM7_SYSTEM;
    }
    else {
        status = (strcmp(info->server.inetId, "0.0.0.0"))
                ? 0
                : mi_replaceServerId(info, ghostname());

        if (0 == status)
            status = mi_copy(clientView, info);

        mi_free(info);
    } // `info` allocated

    return status;
}

/**
 * Sets or resets the subscription of the associated downstream LDM-7. Ensures
 * that the multicast LDM sender process that's associated with the given
 * feedtype is running.
 *
 * @param[in]  feed         Feedtype of multicast group.
 * @param[in]  xprt         RPC transport.
 * @param[out] reply        Reply or NULL (which means no reply). If non-NULL,
 *                          then caller should call
 *                          `xdr_free(xdr_SubscriptionReply, reply)` when it's
 *                          no longer needed.
 * @retval     0            Success. `*reply` is set.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
static Ldm7Status
up7_subscribe(
        const feedtypet                   feed,
        struct SVCXPRT* const restrict    xprt,
        SubscriptionReply* const restrict reply)
{
    const McastInfo* mcastInfo;
    pid_t            pid;
    int              status = mlsm_ensureRunning(feed, &mcastInfo, &pid);

    if (LDM7_SYSTEM == status)
        return status;

    if (LDM7_NOENT == status) {
        log_log(LOG_INFO);
        reply->status = LDM7_INVAL; // invalid request
        return 0;
    }

    /* TODO: Add authorization */
    /* TODO: Reduce subscription */

    if ((status = up7_setMcastInfo(&reply->SubscriptionReply_u.mgi, mcastInfo)))
        return status;

    feedtype = feed;
    reply->status = LDM7_OK;

    return 0;
}

/**
 * Ensures that a reply to an RPC service routine has been freed.
 *
 * @param[in] xdrProc  Associated XDR function.
 * @param[in] reply    RPC reply.
 */
static inline void
up7_ensureFree(
        xdrproc_t const      xdrProc,
        void* const restrict reply)
{
    if (reply)
        xdr_free(xdrProc, (char*)reply);
}

/**
 * Delivers a data-product to the associated downstream LDM-7. Called by
 * `pq_processProdBySig()`.
 *
 * @param[in] info         Data-product metadata.
 * @param[in] data         Data-product data.
 * @param[in] xprod        XDR-encoded data-product.
 * @param[in] len          Size of XDR-encoded data-product in bytes.
 * @param[in] optArg       Optional argument passed to `pq_processProduct()`.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  Failure. `log_start()` called.
 */
static int
up7_deliverProduct(
        const prod_info* const restrict info,
        const void* const restrict      data,
	void* const restrict            xprod,
	const size_t                    len,
	void* const restrict            optArg)
{
    MissedProduct missedProd;

    missedProd.iProd = *(VcmtpProdIndex*)optArg;
    missedProd.prod.info = *info;
    missedProd.prod.data = (void*)data; // cast away `const`

    (void)deliver_missed_product_7(&missedProd, clnt);

    /*
     * The status will be RPC_TIMEDOUT unless an error occurs because the RPC
     * call uses asynchronous message-passing.
     */
    if (clnt_stat(clnt) == RPC_TIMEDOUT) {
        if (ulogIsVerbose())
            unotice("Missed product sent: %s",
                    s_prod_info(NULL, 0, info, ulogIsDebug()));
        return 0;
    }

    LOG_START1("Couldn't RPC to downstream LDM-7: %s", clnt_errmsg(clnt));

    return LDM7_SYSTEM;
}

/**
 * Sends the data-product corresponding to a multicast Product-index to the
 * associated downstream LDM-7.
 *
 * @param[in]  iProd        Product-index.
 * @retval     0            Success.
 * @retval     LDM7_NOENT   No corresponding data-product. `log_start()` called.
 * @retval     LDM7_SYSTEM  System failure. `log_start()` called.
 */
static Ldm7Status
up7_sendProduct(
        VcmtpProdIndex iProd)
{
    signaturet sig;
    int        status = pim_get(iProd, &sig);

    if (LDM7_NOENT == status) {
        LOG_START1("No signature corresponding to product-index %lu",
                (unsigned long)iProd);
    }
    else if (0 == status) {
        status = pq_processProduct(pq, sig, up7_deliverProduct, &iProd);

        if (PQ_NOTFOUND == status) {
            char buf[sizeof(signaturet)*2+1];

            (void)sprint_signaturet(buf, sizeof(buf), sig);
            LOG_START2("No data-product corresponding to signature: "
                    "fileId=%lu, sig=%s", (unsigned long)iProd, buf);
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
 * @retval    true    Success.
 * @retval    false   Failure. `log_start()` called.
 */
static bool
up7_findAndSendProduct(
    VcmtpProdIndex iProd)       // not `cont` because of `no_such_product_7()`
{
    int status = up7_sendProduct(iProd);

    if (LDM7_NOENT == status) {
        log_log(LOG_INFO);
        (void)no_such_product_7(&iProd, clnt);

        if (clnt_stat(clnt) != RPC_TIMEDOUT) {
            /*
             * The status will be RPC_TIMEDOUT unless an error occurs because
             * the RPC call uses asynchronous message-passing.
             */
            LOG_START1("Couldn't RPC to downstream LDM-7: %s", clnt_errmsg(clnt));
            return false;
        }
    }

    return 0 == status;
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
            LOG_START0("Couldn't close global product-queue");
            log_log(LOG_ERR);
        }
        pq = NULL;
    }
}

/**
 * Ensures that the product-queue is open for reading.
 *
 * @retval false  Failure. `log_start()` called.
 * @retval true   Success.
 */
static bool
up7_ensureProductQueueOpen()
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
                LOG_START1("The product-queue \"%s\" is corrupt", pqPath);
            }
            else {
                LOG_ERRNUM1(status, "Couldn't open product-queue \"%s\": %s",
                        pqPath);
            }
            success = false;
        }
        else {
            if (atexit(closePq)) {
                LOG_SERROR0("Couldn't register product-queue closing function");
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
 * Sets the cursor of the product-queue to just after the data-product with a
 * given signature.
 *
 * @param[in] after        Data-product signature.
 * @retval    0            Success.
 * @retval    LDM7_NOENT   Corresponding data-product not found.
 * @retval    LDM7_SYSTEM  Failure. `log_start()` called.
 */
static Ldm7Status
up7_setCursorFromSignature(
        const signaturet after)
{
    int status;

    switch ((status = pq_setCursorFromSignature(pq, after))) {
    case 0:
        return 0;
    case PQ_NOTFOUND:
        LOG_START1("Data-product with signature %s wasn't found in product-queue",
                s_signaturet(NULL, 0, after));
        log_log(LOG_INFO);
        return LDM7_NOENT;
    default:
        LOG_START2("Couldn't set product-queue cursor from signature %s: %s",
                s_signaturet(NULL, 0, after), pq_strerror(pq, status));
        return LDM7_SYSTEM;
    }
}

/**
 * Sets the cursor of the product-queue to point a time-offset older than now.
 *
 * @param[in] offset  Time offset in seconds.
 * @retval    true    Success.
 * @retval    false   Failure. `log_start()` called.
 */
static void
up7_setCursorFromTimeOffset(
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
 * @retval    false    Failure. `log_start()` called.
 */
static bool
up7_setProductQueueCursor(
        const BacklogSpec* const restrict backlog)
{
    if (backlog->afterIsSet) {
        switch (up7_setCursorFromSignature(backlog->after)) {
        case 0:
            return true;
        case LDM7_NOENT:
            break;
        default:
            return false;
        }
    }

    up7_setCursorFromTimeOffset(backlog->timeOffset);

    return true;
}

/**
 * Sends a data-product to the downstream LDM-7 if it doesn't have a given
 * signature.
 *
 * @param[in] info         Data-product's metadata.
 * @param[in] data         Data-product's data.
 * @param[in] xprod        XDR-encoded version of data-product (data and metadata).
 * @param[in] size         Size, in bytes, of XDR-encoded version.
 * @param[in] arg          Signature.
 * @retval    0            Success.
 * @retval    LDM7_EXISTS  Data-product has given signature. Not sent.
 * @retval    LDM7_SYSTEM  System error. `log_start()` called.
 */
static int
up7_sendIfNotSignature(
    const prod_info* const restrict info,
    const void* const restrict      data,
    void* const restrict            xprod,
    const size_t                    size,
    void* const restrict            arg)
{
    const signaturet* sig = (const signaturet*)arg;

    if (0 == memcmp(sig, info->signature, sizeof(signaturet)))
        return LDM7_EXISTS;

    product prod;
    prod.info = *info;
    prod.data = (void*)data;    // cast away `const`

    deliver_backlog_product_7(&prod, clnt);

    /*
     * The status will be RPC_TIMEDOUT unless an error occurs because the RPC
     * call uses asynchronous message-passing.
     */
    if (clnt_stat(clnt) == RPC_TIMEDOUT) {
        if (ulogIsVerbose())
            unotice("Backlog product sent: %s",
                    s_prod_info(NULL, 0, info, ulogIsDebug()));
        return 0;
    }

    LOG_START1("Couldn't RPC to downstream LDM-7: %s", clnt_errmsg(clnt));

    return LDM7_SYSTEM;
}

/**
 * Sends all data-products of the given feedtype in the product-queue from the
 * current cursor position up to (but not including) the data-product with a
 * given signature.
 *
 * @param[in] before       Signature of data-product at which to stop sending.
 * @retval    0            Success.
 * @retval    LDM7_NOENT   Data-product with given signature not found before
 *                         end of queue reached.
 * @retval    LDM7_SYSTEM  System failure. `log_start()` called.
 */
static Ldm7Status
up7_sendUpToSignature(
        const signaturet* const before)
{
    // `dup_prod_class()` compiles the patterns
    prod_class_t* const prodClass = dup_prod_class(PQ_CLASS_ALL);

    if (NULL == prodClass)
        return LDM7_SYSTEM;

    prodClass->psa.psa_val->feedtype = feedtype;        // was `ANY`

    int status;
    for (;;) {
        status = pq_sequence(pq, TV_GT, prodClass, up7_sendIfNotSignature,
                (signaturet*)before);                   // cast away `const`

        if (status) {
            status = (PQUEUE_END == status)
                ? LDM7_NOENT
                : (LDM7_EXISTS == status)
                    ? 0
                    : LDM7_SYSTEM;
            break;
        }
    }

    free_prod_class(prodClass);

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
 * @retval    false    Failure. `log_start()` called.
 */
static bool
up7_sendBacklog(
        const BacklogSpec* const restrict backlog)
{
    if (!up7_setProductQueueCursor(backlog))
        return false;

    return LDM7_SYSTEM != up7_sendUpToSignature(&backlog->before);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Sets the subscription of the associated downstream LDM-7. Called by the RPC
 * dispatch function `ldmprog_7()`.
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @param[in] feedtype   Feedtype of multicast group.
 * @param[in] rqstp      RPC service-request.
 * @retval    NULL       System error. `log_log()` and `svcerr_systemerr()`
 *                       called. No reply should be sent to the downstream
 *                       LDM-7.
 * @return               Result of the subscription request.
 */
SubscriptionReply*
subscribe_7_svc(
    feedtypet* const restrict       feedtype,
    struct svc_req* const restrict  rqstp)
{
    udebug("subscribe_7_svc(): Entered");
    static SubscriptionReply* reply;
    static SubscriptionReply  result;
    struct SVCXPRT* const     xprt = rqstp->rq_xprt;
    const char*               ipv4spec = inet_ntoa(xprt->xp_raddr.sin_addr);
    const char*               hostname = hostbyaddr(&xprt->xp_raddr);
    const char*               feedspec = s_feedtypet(*feedtype);

    unotice("Incoming subscription from %s (%s) port %u for %s", ipv4spec,
            hostname, ntohs(xprt->xp_raddr.sin_port), s_feedtypet(*feedtype));
    up7_ensureFree(xdr_SubscriptionReply, reply);       // free any prior use

    if (up7_subscribe(*feedtype, xprt, &result) ||
            !up7_ensureProductQueueOpen()) {
        LOG_ADD2("Couldn't subscribe %s to feedtype %s",
                hostbyaddr(svc_getcaller(xprt)), s_feedtypet(*feedtype));
        log_log(LOG_ERR);
        svcerr_systemerr(xprt); // in `rpc/svc.c`; only valid for synchronous RPC
        svc_destroy(xprt);
        /*
         * The reply is set to NULL in order to cause the RPC dispatch routine
         * to not reply because `svcerr_systemerr()` has been called and the
         * server-side transport destroyed.
         */
        reply = NULL;
    }
    else {
        if (!up7_createClientTransport(xprt)) {
            log_log(LOG_ERR);
            svcerr_systemerr(xprt); // in `rpc/svc.c`; only valid for synchronous RPC
            svc_destroy(xprt);
            /*
             * The reply is set to NULL in order to cause the RPC dispatch
             * routine to not reply because `svcerr_systemerr()` has been called
             * and the server-side transport destroyed.
             */
            reply = NULL;
        }
        else {
            // `clnt` set
            reply = &result; // reply synchronously
        }
    }

    return reply;
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
    VcmtpProdIndex* const iProd,
    struct svc_req* const rqstp)
{
    udebug("request_product_7_svc(): Entered");
    struct SVCXPRT* const     xprt = rqstp->rq_xprt;

    if (clnt == NULL) {
        LOG_START1("Client %s hasn't subscribed yet", rpc_getClientId(rqstp));
        log_log(LOG_ERR);
        svc_destroy(xprt);      // asynchrony => no sense replying
    }
    else if (!up7_findAndSendProduct(*iProd)) {
        log_log(LOG_ERR);
        up7_destroyClient();
        svc_destroy(xprt);      // asynchrony => no sense replying
    }

    return NULL;                // don't reply
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
    udebug("request_backlog_7_svc(): Entered");
    struct SVCXPRT* const     xprt = rqstp->rq_xprt;

    if (clnt == NULL) {
        LOG_START1("Client %s hasn't subscribed yet", rpc_getClientId(rqstp));
        log_log(LOG_ERR);
        svc_destroy(xprt);      // asynchrony => no sense replying
    }
    else if (!up7_sendBacklog(backlog)) {
        log_log(LOG_ERR);
        up7_destroyClient();
        svc_destroy(xprt);      // asynchrony => no sense replying
    }

    return NULL;                // don't reply
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
    udebug("test_connection_7_svc(): Entered");
    return NULL;                // don't reply
}
