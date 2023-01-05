/*
 *   Copyright 2005, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */
/* $Id: svc5.c,v 1.102.16.3.2.10 2009/06/18 16:23:17 steve Exp $ */

/* 
 * uds server side routines
 */
#include <config.h>

#include <assert.h>
#include <errno.h> /* EIO */
#include <rpc/rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ldm.h"

#include "down6.h"
#include "DownHelp.h"
#include "error.h"
#include "forn5_svc.h"
#include "globals.h"
#include "ldmprint.h"
#include "pq.h"
#include "prod_info.h"
#include "remote.h"
#include "savedInfo.h"
#include "log.h"
#include "LdmConfFile.h"
#include "xdr_data.h"

static ldm_replyt  reply;
static prod_info*  newInfo;             /* new product-information */
static char       *datap;
static unsigned    remaining;
static             pqe_index idx = {    /* PQE_NONE */
    ((off_t)(-1)), /* OFF_NONE */
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}
};


/*
 * Updates "newInfo".
 *
 * Arguments:
 *      info    The metadata of the data-product.
 * Returns:
 *      0       Success.
 *      ENOMEM  Out of memory.
 *      EINVAL  "info" is NULL.
 */
static int
setNewInfo(
    const prod_info* const      info)
{
    int         error;

    if (NULL == newInfo)
        newInfo = pi_new();

    if (NULL == newInfo) {
        log_add_syserr("Couldn't allocate new prod_info structure");
        log_flush_error();
        error = ENOMEM;
    }
    else {
        if (NULL == info) {
            error = EINVAL;
        }
        else {
            dh_setInfo(newInfo, info, remote_name());
            error = 0;                  /* success */
        }
    }

    return error;
}


/*
 * Returns "newInfo".
 *
 * Returns:
 *      Pointer to the new prod_info structure.
 */
static const prod_info*
getNewInfo()
{
    return newInfo;
}


/*ARGSUSED*/
ldm_replyt *
hereis_5_svc(product *prod, struct svc_req *rqstp)
{
    int         status;
    ldm_replyt* replyPtr = &reply;

    log_debug("hereis_5_svc()");

    (void)memset((char*)&reply, 0, sizeof(reply));

    if (done) {
        reply.code = SHUTTING_DOWN;
    }
    else {
        peer_info*      remote = get_remote();
        /*
         * Update the time bounds, fuzz by rpctimeo.
         */
        (void)set_timestamp(&remote->clssp->from);
        remote->clssp->from.tv_sec -= (max_latency + rpctimeo);

        if (!prodInClass(remote->clssp, &prod->info)) {
            /*
             * Reply with RECLASS
             */
            if (toffset != TOFFSET_NONE) {
                remote->clssp->from.tv_sec += (max_latency - toffset);
            }
            else {
                /*
                 * Undo the fuzz.
                 */
                remote->clssp->from.tv_sec += rpctimeo;
            }

            log_notice_q("RECLASS: %s", s_prod_class(NULL, 0, remote->clssp));

            if (tvCmp(remote->clssp->from, prod->info.arrival, >)) {
                char buf[32];

                (void) sprint_timestampt(buf, sizeof(buf), &prod->info.arrival);
                log_notice_q("skipped: %s (%.3f seconds)", buf,
                        d_diff_timestamp(&remote->clssp->from,
                                &prod->info.arrival));
            }

            reply.code = RECLASS;
            reply.ldm_replyt_u.newclssp = remote->clssp;
        }
        else {
            if (setNewInfo(&prod->info)) {
                svcerr_systemerr(rqstp->rq_xprt);
                replyPtr = NULL;
            }
            else {
                status = dh_saveProd(pq, getNewInfo(), prod->data, 1, 0);

                if (status && DOWN6_UNWANTED != status &&
                        DOWN6_PQ_BIG != status && DOWN6_PQ_NO_ROOM != status) {
                    svcerr_systemerr(rqstp->rq_xprt);
                    replyPtr = NULL;
                }
            }
        }
    }

    return (replyPtr);
}


ldm_replyt * 
feedme_5_svc(prod_class_t *want, struct svc_req *rqstp)
{
    log_debug("feedme_5_svc()");

    if(log_is_enabled_info) {
        if(remote_name() == NULL)
            svc_setremote(rqstp);
        log_info_q("feedme5: %s: %s", remote_name(), s_prod_class(NULL, 0, want));
    }

    return forn_5_svc(want, rqstp, "(feed)", feed5_sqf);
}


/*ARGSUSED*/
ldm_replyt * 
hiya_5_svc(prod_class_t *offerd, struct svc_req *rqstp)
{
        const char* const       pqfname = getQueuePath();
        peer_info*              remote = get_remote();

        log_debug("hiya_5_svc()");

        (void)memset((char*)&reply, 0, sizeof(reply));

        if(remote_name() == NULL)
                svc_setremote(rqstp);
        else if(remote->addr.s_addr == 0)
        {
                struct sockaddr_in *paddr = 
                    (struct sockaddr_in *)svc_getcaller(rqstp->rq_xprt);
                remote->addr.s_addr = paddr->sin_addr.s_addr;
        }

        if(log_is_enabled_info)
        {
                log_info_q("hiya5: %s: %s",
                        remote_name(),
                        s_prod_class(NULL, 0, offerd));
        }

        if(done)
        {
                reply.code = SHUTTING_DOWN;
                return(&reply);
        }

        if(!clsspsa_eq(remote->clssp, offerd))
        {
                /* request doesn't match cache */

                free_remote_clss();

                switch(lcf_isHiyaAllowed(remote, offerd)) {
                case ENOERR:
                        break;
                case EINVAL:
                        log_add_errno(EINVAL, "hiya_acl_ck: BADPATTERN");
                        log_flush_error();
                        reply.code = BADPATTERN;
                        return(&reply);
                default:
                        log_error_q("hiya_acl_ck");
                        svcerr_systemerr(rqstp->rq_xprt);
                        return NULL;
                }
        }
        else
        {
                /* Update the time range */
                remote->clssp->from = offerd->from;
                remote->clssp->to = offerd->to;
        }
        

        if(remote->clssp == NULL || remote->clssp->psa.psa_len == 0)
        {
                if(!log_is_enabled_info)
                        log_notice_q("hiya5: Accept: No Match: %s",
                                s_prod_class(NULL, 0, offerd));
                else
                        log_notice_q("hiya5: Accept: No Match");
                /* ??? */
                svcerr_weakauth(rqstp->rq_xprt);
                return NULL;
        }

        if(!clss_eq(remote->clssp, offerd))
        {
                reply.code = RECLASS;
                if(log_is_enabled_info)
                        log_info_q("hiya5: reclss: %s: %s",
                                remote_name(),
                                s_prod_class(NULL, 0, remote->clssp));
                reply.ldm_replyt_u.newclssp = remote->clssp;
        }
        /* else, reply.code == OK */
        if(!log_is_enabled_info) {
                log_notice_q("hiya5: %s",
                        s_prod_class(NULL, 0, remote->clssp));
                hiyaCalled = true;
        }

        /*
         * Ensure that the product-queue is open for writing.  It will be closed
         * by cleanup() during process termination.
         */
        if (NULL != pq) {
            (void)pq_close(pq);
            pq = NULL;
        }
        {
            int error = pq_open(pqfname, PQ_DEFAULT, &pq);

            if (error) {
                err_log_and_free(
                    ERR_NEW2(
                        error, NULL,
                        "Couldn't open product-queue \"%s\" for writing: %s",
                        pqfname, 
                        PQ_CORRUPT == error
                            ? "The product-queue is inconsistent"
                            : strerror(error)),
                    ERR_FAILURE);
                svcerr_systemerr(rqstp->rq_xprt);
                return NULL;
            }
        }

        return(&reply);
}


/*ARGSUSED*/
ldm_replyt * 
notification_5_svc(prod_info *infop, struct svc_req *rqstp)
{
        log_debug("notification_5_svc()");

        /* This should never be called here */
#if 0
        svcerr_noproc(rqstp->rq_xprt);
        return NULL;
#else
        (void)memset((char*)&reply, 0, sizeof(reply));

        if(log_is_enabled_info)
        {
                log_info_q("notification5: %s",
                        s_prod_info(NULL, 0, infop,
                                log_is_enabled_debug));
        }

        if(done)
                reply.code = SHUTTING_DOWN;

        return(&reply);
#endif
}


ldm_replyt * 
notifyme_5_svc(prod_class_t *want, struct svc_req *rqstp)
{
    log_debug("notifyme_5_svc()");

    if(log_is_enabled_info) {
        if(remote_name() == NULL)
            svc_setremote(rqstp);
        log_info_q("notifyme5: %s: %s", remote_name(), s_prod_class(NULL, 0, want));
    }

    return forn_5_svc(want, rqstp, "(noti)", noti5_sqf);
}


void
clr_pip_5(void)
{
        if(!pqeIsNone(idx))
        {
                (void) pqe_discard(pq, &idx);
                idx = PQE_NONE;
        }
}


/*ARGSUSED*/
ldm_replyt * 
comingsoon_5_svc(comingsoon_args *argsp, struct svc_req *rqstp)
{
        int             status;
        int             error;
        prod_info*      infop = argsp->infop;
        peer_info*      remote = get_remote();

        log_debug("comingsoon_5_svc()");

        (void)memset((char*)&reply, 0, sizeof(reply));

        if(done)
        {
                reply.code = SHUTTING_DOWN;
                return(&reply);
        }
        /* else */

        /* inline clr_pip_5(); */
        if(!pqeIsNone(idx))
        {
                log_error_q("%s: never completed",
                        s_signaturet(NULL, 0, idx.signature));
                (void) pqe_discard(pq, &idx);
                idx = PQE_NONE;
        }

        /* Update the time bounds, fuzz by rpctimeo */
        (void)set_timestamp(&remote->clssp->from);
        remote->clssp->from.tv_sec -= (max_latency + rpctimeo);

        if(!prodInClass(remote->clssp, infop))
        {
                /* Reply with RECLASS */
                if(toffset != TOFFSET_NONE)
                {
                        remote->clssp->from.tv_sec += (max_latency - toffset);
                }
                else
                {
                        /* undo the fuzz */
                        remote->clssp->from.tv_sec += rpctimeo;
                }
                log_notice_q("RECLASS: %s", s_prod_class(NULL, 0, remote->clssp));
                if(tvCmp(remote->clssp->from, infop->arrival, >))
                {
                        char buf[32];
                        (void) sprint_timestampt(buf, sizeof(buf),
                                 &infop->arrival);
                        log_notice_q("skipped: %s (%.3f seconds)", buf,
                                d_diff_timestamp(&remote->clssp->from,
                                        &infop->arrival));
                }
                reply.code = RECLASS;
                reply.ldm_replyt_u.newclssp = remote->clssp;
                return(&reply);
        }

        if (setNewInfo(infop)) {
            svcerr_systemerr(rqstp->rq_xprt);
            return NULL;
        }

        status = pqe_new(pq, getNewInfo(), (void **)&datap, &idx);

        if(status == EINVAL)
        {
                log_error_q("Invalid product: %s",
                        s_prod_info(NULL, 0, infop,
                                log_is_enabled_debug));

                error = savedInfo_set(infop);

                if (error) {
                    err_log_and_free(
                        ERR_NEW1(0, NULL,
                            "Couldn't save product-information: %s",
                            savedInfo_strerror(error)),
                        ERR_FAILURE);
                    svcerr_systemerr(rqstp->rq_xprt);
                    return NULL;
                }

                reply.code = DONT_SEND;
                return(&reply);
        }
        /* else */
        if (status == PQUEUE_BIG)
        {
                /*
                 * The data-product is too big to fit into the product-queue.
                 */
                log_error_q("Product too big: %s",
                        s_prod_info(NULL, 0, infop,
                                log_is_enabled_debug));

                error = savedInfo_set(infop);
                if (error) {
                    log_error_q("Couldn't save product-information: %s",
                            savedInfo_strerror(error));
                    svcerr_systemerr(rqstp->rq_xprt);
                    return NULL;
                }

                reply.code = DONT_SEND;
                return(&reply);
        }
        /* else */
        if(status == PQUEUE_DUP)
        {
                error = savedInfo_set(infop);
                if (error) {
                    err_log_and_free(
                        ERR_NEW1(0, NULL,
                            "Couldn't save product-information: %s",
                            savedInfo_strerror(error)),
                        ERR_FAILURE);
                    svcerr_systemerr(rqstp->rq_xprt);
                    return NULL;
                }

                reply.code = DONT_SEND;
                if(log_is_enabled_info)
                {
                        log_info_q("dup    : %s",
                                s_prod_info(NULL, 0, infop,
                                        log_is_enabled_debug));
                }
                return(&reply);
        }
        /* else */
        if(status != ENOERR)
        {
                log_error_q("origin: %s\0",infop->origin);
                log_error_q("comings: pqe_new: %s", strerror(status));
                log_error_q("       : %s",
                        s_prod_info(NULL, 0, infop, 1));
                svcerr_systemerr(rqstp->rq_xprt);
                return NULL;
        }
        /* else */

        /*
         * The data-product isn't in the product-queue.  Setup for
         * receiving the product's data via BLKDATA messages.
         */
        (void)pqe_discard(pq, &idx);

        /*
         * Use the growable buffer of the "XDR-data" module as the location
         * into which the XDR layer will decode the data in BLKDATA messages.
         */
        remaining = infop->sz;
        datap = xd_getBuffer(remaining);        /* completely allocate buffer */

        if (log_is_enabled_debug)
            log_debug("comings: %s (pktsz %u)",
                s_prod_info(NULL, 0, infop,
                        log_is_enabled_debug),
                        argsp->pktsz);

        return(&reply);
}


/*ARGSUSED*/
ldm_replyt * 
blkdata_5_svc(datapkt *dpkp, struct svc_req *rqstp)
{
    int         error;
    char        gotSig[2*sizeof(signaturet)+1];
    char        expectedSig[2*sizeof(signaturet)+1];
    ldm_replyt* result = &reply;

    log_debug("blkdata_5_svc()");

    (void)memset((char*)&reply, 0, sizeof(reply));

    if(log_is_enabled_debug) {
        log_debug("blkdata5: %s %8u %5u",
                s_signaturet(gotSig, sizeof(gotSig), *dpkp->signaturep),
                dpkp->data.dbuf_len,
                dpkp->pktnum);
    }

    if(done) {
        reply.code = SHUTTING_DOWN;
    }
    else {
        if(memcmp(*dpkp->signaturep, idx.signature, sizeof(signaturet)) != 0) {
            log_notice_q("invalid signature: got=%s; expected=%s",
                s_signaturet(gotSig, sizeof(gotSig), *dpkp->signaturep),
                s_signaturet(expectedSig, sizeof(expectedSig), idx.signature));
            svcerr_systemerr(rqstp->rq_xprt);

            result = NULL;
        }
        else {
            unsigned got = dpkp->data.dbuf_len;

            if (got > remaining)
            {
                log_error_q("too much data: max=%u; got=%u", remaining, got);
                svcerr_systemerr(rqstp->rq_xprt);
                xd_reset();

                idx = PQE_NONE;
                result = NULL;
            }
            else {
                remaining -= got;

                if(remaining == 0) {
                    error = dh_saveProd(pq, getNewInfo(), datap, 0, 0);

                    if (error && DOWN6_UNWANTED != error &&
                                DOWN6_PQ_BIG != error &&
                                DOWN6_PQ_NO_ROOM != error) {
                        svcerr_systemerr(rqstp->rq_xprt);

                        result = NULL;
                    }

                    idx = PQE_NONE;

                    xd_reset();
                }
            }
        }
    }                                                   /* !done */

    return result;
}
