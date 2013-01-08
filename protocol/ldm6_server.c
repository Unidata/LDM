/*
 * This module contains the server-side functions that are invoked by the
 * "ldm6_svc" module.  Because RPC assumes a client/server structure, this
 * module contains code for both upstream and downstream LDM-s.
 *
 * From a design-pattern perspective, this module is a combination fa�ade and
 * adapter for the "up6" and "down6" modules.
 */
/* $Id: ldm6_server.c,v 1.13.8.2.2.3.2.18 2009/06/18 23:37:22 steve Exp $ */

#include "config.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>       /* ENOMEM */
#include <netinet/in.h>
#include <rpc/rpc.h>     /* SVCXPRT, xdrproc_t */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>      /* getenv, exit */
#include <string.h>      /* strncpy(), strerror() */
#include <strings.h>     /* strncasecmp() */
#include <sys/types.h>
#include <unistd.h>      /* getpid() */

#include "abbr.h"
#include "acl.h"         /* acl_product_intersection(), acl_check_hiya() */
#include "autoshift.h"
#include "child_process_set.h"       /* cps_contains() */
#include "down6.h"       /* the pure "downstream" LDM module */
#include "error.h"
#include "forn.h"
#include "globals.h"
#include "remote.h"
#include "inetutil.h"    /* hostbyaddr() */
#include "ldm.h"         /* *_6_svc() functions */
#include "ldmprint.h"    /* s_prod_class() */
#include "pq.h"
#include "prod_class.h"  /* free_prod_class() */
#include "ulog.h"
#include "log.h"
#include "UpFilter.h"
#include "uldb.h"

#include "up6.h"         /* the pure "upstream" LDM module */

/*
 * Decodes a data-product signature from the last product-specification of a
 * product-class if it exists.
 *
 * Arguments:
 *      class           Pointer to the product-class.  Caller may free upon
 *                      return.
 * Returns:
 *      NULL            The last product-specification didn't contain a valid,
 *                      encoded signature.
 *      else            Pointer to a static signature buffer into which the
 *                      signature specification was successfully decoded.
 */
static const signaturet*
decodeSignature(
        const prod_class_t* const prodClass)
{
    const signaturet* sig = NULL; /* no valid, encoded signature */

    if (0 < prodClass->psa.psa_len) {
        const prod_spec* const lastProdSpec =
                &prodClass->psa.psa_val[prodClass->psa.psa_len - 1];

        if (NONE == lastProdSpec->feedtype) {
            char* pat = lastProdSpec->pattern;

            if (strncasecmp("SIG=", pat, 4) == 0) {
                char* encodedSig = pat + 4;
                int i;
                unsigned value;
                static signaturet sigBuf;

                errno = 0;

                for (i = 0; i < sizeof(signaturet); i++) {
                    if (sscanf(encodedSig + 2 * i, "%2x", &value) != 1)
                        break;

                    sigBuf[i] = (unsigned char) value;
                }

                if (i == sizeof(signaturet)) {
                    sig = (const signaturet*) &sigBuf[0];
                }
                else {
                    if (0 == errno) {
                        err_log_and_free(
                                ERR_NEW1(1, NULL, "Invalid signature (%s)",
                                        encodedSig), ERR_NOTICE);
                    }
                    else {
                        err_log_and_free(
                                ERR_NEW2(1, NULL, "Invalid signature (%s): %s",
                                        encodedSig, strerror(errno)),
                                ERR_NOTICE);
                    }
                } /* signature not decoded */
            } /* "SIG=" found */
        } /* last feedtype is NONE */
    } /* at least one product-specification */

    return sig;
}

/*
 * Separates a product-class into a signature component and a non-signature
 * component.
 *
 * Arguments:
 *      prodClass       Pointer to product-class to be separated.  Caller may
 *                      free upon return.
 *      noSigProdClass  Pointer to pointer to be set to an allocated product-
 *                      class that will not have a signature encoded within
 *                      it.  Set on and only on success.  On success, caller
 *                      should invoke free_prod_class(*noSigProdClass).
 *      signature       Pointer to pointer to signature.  Set on and only on
 *                      success.  Will be set to NULL if and only if "prodClass"
 *                      didn't contain an encoded signature; otherwise, will
 *                      be set to point to a static buffer that contains the
 *                      signature.  Caller must not free.
 * Returns:
 *      NULL            Success.
 *      else            Error object.
 */
static ErrorObj*
separateProductClass(
        const prod_class_t* const prodClass,
        prod_class_t** const noSigProdClass,
        const signaturet** const signature)
{
    ErrorObj* errObj;
    prod_class_t* noSigClass = dup_prod_class(prodClass);

    if (NULL == noSigClass) {
        errObj = ERR_NEW1(0, NULL,
                "Couldn't duplicate product-class: %s", strerror(errno));
    }
    else {
        const signaturet* sig = decodeSignature(prodClass);

        if (NULL != sig)
            clss_scrunch(noSigClass); /* removes encoded signature */

        *noSigProdClass = noSigClass;
        *signature = sig;
        errObj = NULL; /* success */
    }

    return errObj;
}

/**
 * Feeds or notifies a downstream LDM. This function either returns a reply to
 * be sent to the downstream LDM (e.g., a RECLASS message) or terminates this
 * process (hopefully after sending some data).
 * 
 * @param xprt          [in/out] Pointer to server-side transport handle.
 * @param want          [in] Pointer to subscription by downstream LDM.
 *                      May contain a "signature" product-specification.
 * @param isNotifier    [in] Whether or not the upstream LDM is a feeder or a
 *                      notifier.
 * @param maxHereis     Maximum HEREIS size parameter. Ignored if "isNotifier"
 *                      is true.
 * @return              The reply for the downstream LDM
 */
static fornme_reply_t*
feed_or_notify(
    SVCXPRT* const xprt,
    const prod_class_t* const want,
    const int isNotifier,
    const max_hereis_t maxHereis)
{
    struct sockaddr_in downAddr = *svc_getcaller(xprt);
    ErrorObj* errObj;
    int status;
    int sock;
    char* downName = NULL;
    prod_class_t* origSub = NULL;
    prod_class_t* allowSub = NULL;
    prod_class_t* uldbSub = NULL;
    const signaturet* signature = NULL;
    UpFilter* upFilter = NULL;
    fornme_reply_t* reply = NULL;
    int terminate = 0;
    int isPrimary;
    static fornme_reply_t theReply;

    /*
     * Clean-up from a (possibly) previous invocation
     */
    (void)memset(&theReply, 0, sizeof(theReply));

    downName = strdup(hostbyaddr(&downAddr));
    if (NULL == downName) {
        LOG_ADD1("Couldn't duplicate downstream host name: \"%s\"",
                hostbyaddr(&downAddr));
        log_log(LOG_ERR);
        svcerr_systemerr(xprt);
        goto return_or_exit;
    }

    set_abbr_ident(downName, isNotifier ? "(noti)" : "(feed)");

    /*
     * Remove any "signature" specification from the subscription.
     */
    if (errObj = separateProductClass(want, &origSub, &signature)) {
        err_log_and_free(errObj, ERR_FAILURE);
        svcerr_systemerr(xprt);
        goto free_down_name;
    }

    /*
     * Get the upstream filter
     */
    errObj = acl_getUpstreamFilter(downName, &downAddr.sin_addr, origSub,
            &upFilter);
    if (errObj) {
        err_log_and_free(ERR_NEW(0, errObj,
                "Couldn't get \"upstream\" filter"), ERR_FAILURE);
        svcerr_systemerr(xprt);
        goto free_orig_sub;
    }
    if (NULL == upFilter) {
        err_log_and_free(ERR_NEW(0, NULL,
                "Upstream filter prevents data-transfer"), ERR_FAILURE);
        svcerr_weakauth(xprt);
        goto free_orig_sub;
    }

    /* TODO: adjust time? */

    /*
     * Reduce the subscription according to what the downstream host is allowed
     * to receive.
     */
    status = acl_product_intersection(downName, &downAddr.sin_addr, origSub,
            &allowSub);
    if (status == ENOMEM) {
        LOG_SERROR0("Couldn't compute wanted/allowed product intersection");
        log_log(LOG_ERR);
        svcerr_systemerr(xprt);
        goto free_up_filter;
    }
    if (status == EINVAL) {
        LOG_ADD1("Invalid pattern in product-class: %s",
                s_prod_class(NULL, 0, origSub));
        log_log(LOG_WARNING);
        theReply.code = BADPATTERN;
        reply = &theReply;
        goto free_up_filter;
    }
    assert(status == 0);
    (void) logIfReduced(origSub, allowSub, "ALLOW entries");

    /*
     * Reduce the subscription according to existing subscriptions from the
     * same downstream host.
     */
    /*
     * The following relies on atexit()-registered cleanup for removal of the
     * entry from the upstream LDM database.
     */
    isPrimary = maxHereis > UINT_MAX / 2;
    status = uldb_addProcess(getpid(), 6, &downAddr, allowSub, &uldbSub,
            isNotifier, isPrimary);
    if (status) {
        LOG_ADD0("Couldn't add this process to the upstream LDM database");
        log_log(LOG_ERR);
        svcerr_systemerr(xprt);
        goto free_allow_sub;
    }
    (void) logIfReduced(allowSub, uldbSub, "existing subscriptions");

    /*
     * Send a RECLASS reply to the downstream LDM if appropriate.
     */
    if (!clss_eq(origSub, uldbSub)) {
        (void)uldb_remove(getpid()); /* maybe next time */

        theReply.code = RECLASS;

        if (0 < uldbSub->psa.psa_len) {
            theReply.fornme_reply_t_u.prod_class = uldbSub;
        }
        else {
            /*
             * The downstream LDM isn't allowed anything.
             */
            static prod_class noSub = { { 0, 0 }, /* TS_ZERO */
                { 0, 0 }, /* TS_ZERO */ { 0, (prod_spec *) NULL } };

            theReply.fornme_reply_t_u.prod_class = &noSub;
            done = 1; /* downstream will exit, so we need to */
        }

        reply = &theReply;
        goto free_uldb_sub;
    }

    sock = dup(xprt->xp_sock);
    if (sock == -1) {
        LOG_SERROR1("Couldn't duplicate socket %d", xprt->xp_sock);
        log_log(LOG_ERR);
        svcerr_systemerr(xprt);
        goto free_uldb_sub;
    }

    /*
     * Reply to the downstream LDM that the subscription will be honored.
     */
    theReply.code = OK;
    theReply.fornme_reply_t_u.id = (unsigned) getpid();
    if (!svc_sendreply(xprt, (xdrproc_t) xdr_fornme_reply_t, (caddr_t) &reply)) {
        LOG_ADD0("svc_sendreply(...) failure");
        log_log(LOG_ERR);
        svcerr_systemerr(xprt);
        terminate = 1;
        goto close_sock;
    }

    svc_destroy(xprt);

    /*
     * Wait a second before sending anything to the downstream LDM.
     */
    (void) sleep(1);

    status = isNotifier
            ? up6_new_notifier(sock, downName, &downAddr, uldbSub,
                    signature, getQueuePath(), interval, upFilter)
            : up6_new_feeder(sock, downName, &downAddr, uldbSub,
                    signature, getQueuePath(), interval, upFilter,
                    isPrimary);

    exit(status);

    /*
     * Error handling:
     */
    close_sock:
        (void)close(sock);

    free_uldb_sub:
        if (terminate || &theReply != reply ||
                theReply.fornme_reply_t_u.prod_class != uldbSub)
            free_prod_class(uldbSub);

    free_allow_sub:
        free_prod_class(allowSub);

    free_up_filter:
        upFilter_free(upFilter);

    free_orig_sub:
        free_prod_class(origSub);

    free_down_name:
        free(downName);

    return_or_exit:
        if (terminate)
            exit(1);
        return reply;
}

/******************************************************************************
 * Begin Public API:
 ******************************************************************************/

/**
 * Sends a downstream LDM subscribed-to data-products.
 * <p>
 * This function will not normally return unless the request necessitates a
 * reply (e.g., RECLASS).
 */
fornme_reply_t *feedme_6_svc(
        feedpar_t *feedPar,
        struct svc_req *rqstp)
{
    SVCXPRT* const xprt = rqstp->rq_xprt;
    prod_class_t* want = feedPar->prod_class;
    fornme_reply_t* reply = feed_or_notify(xprt, want, 0, feedPar->max_hereis);

    if (!svc_freeargs(xprt, xdr_feedpar_t, (caddr_t)feedPar)) {
        uerror("Couldn't free arguments");
        svc_destroy(xprt);
        exit(1);
    }

    return reply;
}

/**
 * Notifies a downstream LDM of subscribed-to data-products.
 * <p>
 * This function will not normally return unless the request necessitates a
 * reply (e.g., RECLASS).
 */
fornme_reply_t *notifyme_6_svc(
        prod_class_t* want,
        struct svc_req* rqstp)
{
    SVCXPRT* const xprt = rqstp->rq_xprt;
    fornme_reply_t* reply = feed_or_notify(xprt, want, 1, 0);

    if (!svc_freeargs(xprt, xdr_prod_class, (caddr_t)want)) {
        uerror("Couldn't free arguments");
        svc_destroy(xprt);
        exit(1);
    }

    return reply;
}

int *is_alive_6_svc(
        unsigned *id,
        struct svc_req *rqstp)
{
    static int alive;
    SVCXPRT * const xprt = rqstp->rq_xprt;
    int error = 0;

    alive = cps_contains((pid_t) *id);

    if (ulogIsDebug()) {
        udebug("LDM %u is %s", *id, alive ? "alive" : "dead");
    }

    if (!svc_sendreply(xprt, (xdrproc_t) xdr_bool, (caddr_t) &alive)) {
        svcerr_systemerr(xprt);

        error = 1;
    }

    if (!svc_freeargs(xprt, xdr_u_int, (caddr_t)id)) {
        uerror("Couldn't free arguments");

        error = 1;
    }

    svc_destroy(xprt);
    exit(error);

    /*NOTREACHED*/

    return NULL ;
}

hiya_reply_t*
hiya_6_svc(
        prod_class_t *offered,
        struct svc_req *rqstp)
{
    const char* const pqfname = getQueuePath();
    static hiya_reply_t reply;
    SVCXPRT * const xprt = rqstp->rq_xprt;
    struct sockaddr_in *upAddr = (struct sockaddr_in*) svc_getcaller(xprt);
    const char *upName = hostbyaddr(upAddr);
    int error;
    int isPrimary;
    unsigned int maxHereis;
    static prod_class_t *accept;

    /*
     * Open the product-queue for writing.  It will be closed by cleanup()
     * during process termination.
     */
    if (pq) {
        (void) pq_close(pq);
        pq = NULL;
    }
    error = pq_open(pqfname, PQ_DEFAULT, &pq);
    if (error) {
        err_log_and_free(ERR_NEW2(error, NULL,
                "Couldn't open product-queue \"%s\" for writing: %s",
                pqfname,
                PQ_CORRUPT == error
                ? "The product-queue is inconsistent"
                : strerror(error)), ERR_FAILURE);
        svcerr_systemerr(xprt);
        svc_destroy(xprt);
        exit(error);
    }

    /* else */

    error = down6_init(upName, upAddr, pqfname, pq);
    if (error) {
        uerror("Couldn't initialize downstream LDM");
        svcerr_systemerr(xprt);
        svc_destroy(xprt);
        exit(error);
    }
    else {
        uinfo("Downstream LDM initialized");
    }

    /*
     * The previous "accept" is freed here -- rather than freeing the
     * soon-to-be-allocated "accept" at the end of its block -- because it can
     * be used in the reply.
     */
    if (accept) {
        free_prod_class(accept); /* NULL safe */
        accept = NULL;
    }

    error = acl_check_hiya(upName, inet_ntoa(upAddr->sin_addr), offered,
            &accept, &isPrimary);

    maxHereis = isPrimary ? UINT_MAX : 0;

    if (error) {
        serror("Couldn't validate HIYA");
        svcerr_systemerr(xprt);
        svc_destroy(xprt);
        exit(error);
    }
    else {
        if (ulogIsDebug())
            udebug("intersection: %s", s_prod_class(NULL, 0, accept));

        if (accept->psa.psa_len == 0) {
            uwarn("Empty intersection of HIYA offer from %s (%s) and ACCEPT "
                    "entries", upName, s_prod_class(NULL, 0, offered));
            svcerr_weakauth(xprt);
            svc_destroy(xprt);
            exit(0);
        }
        else {
            error = down6_set_prod_class(accept);

            if (error) {
                if (DOWN6_SYSTEM_ERROR == error) {
                    serror("Couldn't set product class: %s",
                            s_prod_class(NULL, 0, accept));
                }
                else {
                    uerror("Couldn't set product class: %s",
                            s_prod_class(NULL, 0, accept));
                }

                svcerr_systemerr(xprt);
                svc_destroy(xprt);
                exit(EXIT_FAILURE);
            }

            /* else */

            if (clss_eq(offered, accept)) {
                unotice("hiya6: %s", s_prod_class(NULL, 0, offered));

                reply.code = OK;
                reply.hiya_reply_t_u.max_hereis = maxHereis;
            }
            else {
                if (ulogIsVerbose()) {
                    char off[512];
                    char acc[512];

                    (void) s_prod_class(off, sizeof(off), offered), (void) s_prod_class(
                            acc, sizeof(acc), accept);

                    uinfo("hiya6: RECLASS: %s -> %s", off, acc);
                }

                reply.code = RECLASS;
                reply.hiya_reply_t_u.feedPar.prod_class = accept;
                reply.hiya_reply_t_u.feedPar.max_hereis = maxHereis;
            }
        } /* product-intersection != empty set */
    } /* successful acl_check_hiya() */

    return &reply;
}

/*ARGSUSED1*/
void *hereis_6_svc(
        product *prod,
        struct svc_req *rqstp)
{
    int error = down6_hereis(prod);

    if (error && DOWN6_UNWANTED != error && DOWN6_PQ_BIG != error) {
        (void) svcerr_systemerr(rqstp->rq_xprt);
        svc_destroy(rqstp->rq_xprt);
        exit(error);
    }

    return NULL ; /* don't reply */
}

/*ARGSUSED1*/
void *notification_6_svc(
        prod_info *info,
        struct svc_req *rqstp)
{
    (void) down6_notification(info);

    return NULL ; /* don't reply */
}

/*ARGSUSED1*/
comingsoon_reply_t *comingsoon_6_svc(
        comingsoon_args *comingPar,
        struct svc_req *rqstp)
{
    static comingsoon_reply_t reply;
    int error = down6_comingsoon(comingPar);

    if (error == 0) {
        reply = OK;
    }
    else if (DOWN6_UNWANTED == error || DOWN6_PQ_BIG == error) {
        reply = DONT_SEND;
    }
    else {
        (void) svcerr_systemerr(rqstp->rq_xprt);
        svc_destroy(rqstp->rq_xprt);
        exit(error);
    }

    return &reply;
}

/*ARGSUSED1*/
void *blkdata_6_svc(
        datapkt *argp,
        struct svc_req *rqstp)
{
    int error = down6_blkdata(argp);

    if (error && DOWN6_UNWANTED != error && DOWN6_PQ_BIG != error) {
        (void) svcerr_systemerr(rqstp->rq_xprt);
        svc_destroy(rqstp->rq_xprt);
        exit(error);
    }

    return NULL ; /* don't reply */
}

/*
 * Frees resources allocated by the creation of the return result.
 *
 * @param *transp     Server-side handle.
 * @param  xdr_result XDR function for result.
 * @param  result     Result.
 * @return 0          if failure.
 * @return !0         if success.
 */
/*ARGSUSED0*/
int ldmprog_6_freeresult(
        SVCXPRT *transp,
        xdrproc_t xdr_result,
        caddr_t result)
{
    (void) xdr_free(xdr_result, result);

    return 1;
}
