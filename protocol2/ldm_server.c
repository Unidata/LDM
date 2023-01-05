/**
 * This module contains the server-side functions that are invoked by the
 * "ldm_svc" module.  Because RPC assumes a client/server structure, this
 * module contains code for both upstream and downstream LDM-s.
 * <p>
 * From a design-pattern perspective, this module is a combination fa�ade and
 * adapter for the "up6" and "down6" modules.
 */

#include "config.h"

#include "abbr.h"
#include "autoshift.h"
#include "child_process_set.h"       /* cps_contains() */
#include "data_prod.h"
#include "down6.h"       /* the pure "downstream" LDM module */
#include "error.h"
#include "forn.h"
#include "globals.h"
#include "remote.h"
#include "inetutil.h"    /* hostbyaddr() */
#include "ldm.h"         /* *_svc() functions */
#include "ldmprint.h"    /* s_prod_class() */
#include "pq.h"
#include "prod_class.h"  /* free_prod_class() */
#include "log.h"
#include "UpFilter.h"
#include "uldb.h"

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

#include "LdmConfFile.h"         /* acl_product_intersection(), acl_check_hiya() */
#include "up6.h"         /* the pure "upstream" LDM module */

bool hiyaCalled;

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
 * Feeds or notifies a downstream LDM. This function returns either NULL or a
 * reply to be sent to the downstream LDM (e.g., a RECLASS message) or
 * terminates this process (hopefully after sending some data).
 * 
 * @param xprt          [in/out] Pointer to server-side transport handle.
 * @param want          [in] Pointer to subscription by downstream LDM.
 *                      May contain a "signature" product-specification.
 * @param isNotifier    [in] Whether or not the upstream LDM is a feeder or a
 *                      notifier.
 * @param maxHereis     Maximum HEREIS size parameter. Ignored if "isNotifier"
 *                      is true.
 * @return              The reply for the downstream LDM or NULL if no reply
 *                      should be made.
 */
static fornme_reply_t*
feed_or_notify(
    SVCXPRT* const              xprt,
    const prod_class_t* const   want,
    const int                   isNotifier,
    const max_hereis_t          maxHereis)
{
    struct sockaddr_in      downAddr = *svc_getcaller(xprt);
    ErrorObj*               errObj;
    int                     status;
    char*                   downName = NULL;
    prod_class_t*           origSub = NULL;
    prod_class_t*           allowSub = NULL;
    const signaturet*       signature = NULL;
    UpFilter*               upFilter = NULL;
    fornme_reply_t*         reply = NULL;
    int                     isPrimary;
    static fornme_reply_t   theReply;
    static prod_class_t*    uldbSub = NULL;

    /*
     * Clean-up from a (possibly) previous invocation
     */
    (void)memset(&theReply, 0, sizeof(theReply));
    if (uldbSub != NULL) {
        free_prod_class(uldbSub);
        uldbSub = NULL;
    }

    downName = strdup(hostbyaddr(&downAddr));
    if (NULL == downName) {
        log_error_q("Couldn't duplicate downstream host name: \"%s\"",
                hostbyaddr(&downAddr));
        svcerr_systemerr(xprt);
        goto return_or_exit;
    }

    log_set_upstream_id(downName, !isNotifier);

    /*
     * Remove any "signature" specification from the subscription.
     */
    if ((errObj = separateProductClass(want, &origSub, &signature)) != NULL) {
        err_log_and_free(errObj, ERR_FAILURE);
        svcerr_systemerr(xprt);
        goto free_down_name;
    }

    /*
     * Get the upstream filter
     */
    errObj = lcf_getUpstreamFilter(downName, &downAddr.sin_addr, origSub,
            &upFilter);
    if (errObj) {
        err_log_and_free(ERR_NEW(0, errObj,
                "Couldn't get \"upstream\" filter"), ERR_FAILURE);
        svcerr_systemerr(xprt);
        goto free_orig_sub;
    }
    if (NULL == upFilter) {
        err_log_and_free(ERR_NEW1(0, NULL,
                "Upstream filter prevents data-transfer: %s",
                s_prod_class(NULL, 0, origSub)), ERR_FAILURE);
        svcerr_weakauth(xprt);
        goto free_orig_sub;
    }

    /* TODO: adjust time? */

    /*
     * Reduce the subscription according to what the downstream host is allowed
     * to receive.
     */
    status = lcf_reduceToAllowed(downName, &downAddr.sin_addr, origSub,
            &allowSub);
    if (status == ENOMEM) {
        log_add_syserr("Couldn't compute wanted/allowed product intersection");
        log_flush_error();
        svcerr_systemerr(xprt);
        goto free_up_filter;
    }
    if (status == EINVAL) {
        log_warning_q("Invalid pattern in product-class: %s",
                s_prod_class(NULL, 0, origSub));
        theReply.code = BADPATTERN;
        reply = &theReply;
        goto free_up_filter;
    }
    assert(status == 0);
    (void) logIfReduced(origSub, allowSub, "ALLOW entries");

    /*
     * Reduce the subscription according to existing subscriptions from the
     * same downstream host and, if `isAntiDosEnabled()` returns `true`,
     * terminate every previously-existing upstream LDM process that's feeding
     * (not notifying) a subset of the subscription to the same IP address.
     *
     * The following relies on atexit()-registered cleanup for removal of the
     * entry from the upstream LDM database.
     *
     * TODO: Open, add, then close the upstream LDM database
     * TODO: Add this process to the upstream LDM database only *after* no RECLASSing is necessary.
     */
    isPrimary = maxHereis > UINT_MAX / 2;
    status = uldb_addProcess(getpid(), 6, &downAddr, allowSub, &uldbSub,
            isNotifier, isPrimary);
    if (status) {
        log_error_q("Couldn't add this process to the upstream LDM database");
        svcerr_systemerr(xprt);
        goto free_allow_sub;
    }
    (void) logIfReduced(allowSub, uldbSub, "existing subscriptions");

    /*
     * Send a RECLASS reply to the downstream LDM if appropriate.
     */
    if (!clss_eq(origSub, uldbSub)) {
        theReply.code = RECLASS;

        if (0 < uldbSub->psa.psa_len) {
            /*
             * The downstream LDM is allowed less than it requested and was
             * entered into the upstream LDM database.
             */
            (void)uldb_remove(getpid()); /* maybe next time */

            theReply.fornme_reply_t_u.prod_class = uldbSub;
        }
        else {
            /*
             * The downstream LDM isn't allowed anything and wasn't entered
             * into the upstream LDM database.
             */
            static prod_class noSub = { { 0, 0 }, /* TS_ZERO */
                { 0, 0 }, /* TS_ZERO */ { 0, (prod_spec *) NULL } };

            theReply.fornme_reply_t_u.prod_class = &noSub;
        }

        reply = &theReply;

        goto free_allow_sub;
    }

    /*
     * Reply to the downstream LDM that the subscription will be honored.
     */
    theReply.code = OK;
    theReply.fornme_reply_t_u.id = (unsigned) getpid();
    if (!svc_sendreply(xprt, (xdrproc_t)xdr_fornme_reply_t,
            (caddr_t)&theReply)) {
        log_error_q("svc_sendreply(...) failure");
        svcerr_systemerr(xprt);
        goto free_allow_sub;
    }

    /*
     * Wait a second before sending anything to the downstream LDM.
     */
    (void) sleep(1);

    status = isNotifier
            ? up6_new_notifier(xprt->xp_sock, downName, &downAddr, uldbSub,
                    signature, getQueuePath(), interval, upFilter)
            : up6_new_feeder(xprt->xp_sock, downName, &downAddr, uldbSub,
                    signature, getQueuePath(), interval, upFilter,
                    isPrimary);

    svc_destroy(xprt); /* closes the socket */
    exit(status);

    /*
     * Reply and error handling:
     */
    free_allow_sub:
        free_prod_class(allowSub);

    free_up_filter:
        upFilter_free(upFilter);

    free_orig_sub:
        free_prod_class(origSub);

    free_down_name:
        free(downName);

    return_or_exit:
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
        log_error_q("Couldn't free arguments");
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
        log_error_q("Couldn't free arguments");
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

    if (log_is_enabled_debug) {
        log_debug("LDM %u is %s", *id, alive ? "alive" : "dead");
    }

    if (!svc_sendreply(xprt, (xdrproc_t) xdr_bool, (caddr_t) &alive)) {
        svcerr_systemerr(xprt);

        error = 1;
    }

    if (!svc_freeargs(xprt, xdr_u_int, (caddr_t)id)) {
        log_error_q("Couldn't free arguments");

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
        log_error_q("Couldn't initialize downstream LDM");
        svcerr_systemerr(xprt);
        svc_destroy(xprt);
        exit(error);
    }
    else {
        log_info_q("Downstream LDM initialized");
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

    error = lcf_reduceToAcceptable(upName, inet_ntoa(upAddr->sin_addr), offered,
            &accept, &isPrimary);

    maxHereis = isPrimary ? UINT_MAX : 0;

    if (error) {
        log_add_syserr("Couldn't validate HIYA");
        log_flush_error();
        svcerr_systemerr(xprt);
        svc_destroy(xprt);
        exit(error);
    }
    else {
        if (log_is_enabled_debug)
            log_debug("intersection: %s", s_prod_class(NULL, 0, accept));

        if (accept->psa.psa_len == 0) {
            log_warning_q("Empty intersection of HIYA offer from %s (%s) and ACCEPT "
                    "entries", upName, s_prod_class(NULL, 0, offered));
            svcerr_weakauth(xprt);
            svc_destroy(xprt);
            exit(0);
        }
        else {
            error = down6_set_prod_class(accept);

            if (error) {
                if (DOWN6_SYSTEM_ERROR == error) {
                    log_add_syserr("Couldn't set product class: %s",
                            s_prod_class(NULL, 0, accept));
                    log_flush_error();
                }
                else {
                    log_error_q("Couldn't set product class: %s",
                            s_prod_class(NULL, 0, accept));
                }

                svcerr_systemerr(xprt);
                svc_destroy(xprt);
                exit(EXIT_FAILURE);
            }

            /* else */

            if (clss_eq(offered, accept)) {
                log_notice_q("hiya6: %s", s_prod_class(NULL, 0, offered));

                reply.code = OK;
                reply.hiya_reply_t_u.max_hereis = maxHereis;

                hiyaCalled = true;
            }
            else {
                if (log_is_enabled_info) {
                    char off[512];
                    char acc[512];

                    (void) s_prod_class(off, sizeof(off), offered), (void) s_prod_class(
                            acc, sizeof(acc), accept);

                    log_info_q("hiya6: RECLASS: %s -> %s", off, acc);
                }

                reply.code = RECLASS;
                reply.hiya_reply_t_u.feedPar.prod_class = accept;
                reply.hiya_reply_t_u.feedPar.max_hereis = maxHereis;
            }
        } /* product-intersection != empty set */
    } /* successful acl_check_hiya() */

    return &reply;
}

void *hereis_6_svc(
        product *prod,
        struct svc_req *rqstp)
{
    if (dp_isNil(prod)) {
        /*
         * The upstream LDM sent a nil data-product to flush the connection.
         */
    }
    else {
        int error = down6_hereis(prod);

        if (error && DOWN6_UNWANTED != error && DOWN6_PQ_BIG != error &&
                DOWN6_PQ_NO_ROOM != error) {
            (void) svcerr_systemerr(rqstp->rq_xprt);
            svc_destroy(rqstp->rq_xprt);
            exit(error);
        }
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

    if (error && DOWN6_UNWANTED != error && DOWN6_PQ_BIG != error &&
            DOWN6_PQ_NO_ROOM != error) {
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
