/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * This module contains the requester, which implements the REQUEST action for
 * version 6 of the LDM.
 */

#include "config.h"

#include <log.h>      /* log_assert() */
#include <errno.h>       /* system error codes */
#include <limits.h>      /* *INT_MAX */
#include <arpa/inet.h>   /* for <netinet/in.h> under FreeBSD 4.5-RELEASE */
#include <netinet/in.h>  /* sockaddr_in */
#include <rpc/rpc.h>
#include <signal.h>      /* sig_atomic_t */
#include <stdlib.h>      /* NULL, malloc() */
#include <string.h>      /* strerror() */
#include <strings.h>     /* strncasecmp() */
#include <sys/time.h>    /* struct timeval */
#include <unistd.h>      /* close() */

#include "autoshift.h"
#include "down6.h"       /* down6_init(), down6_destroy() */
#include "error.h"
#include "globals.h"
#include "remote.h"
#include "inetutil.h"    /* hostbyaddr() */
#include "ldm.h"         /* client-side LDM functions */
#include "ldm_clnt_misc.h"    /* client-side LDM functions */
#include "ldmprint.h"    /* s_prod_info() */
#include "prod_class.h"  /* clss_eq() */
#include "prod_info.h"
#include "rpcutil.h"     /* clnt_errmsg() */
#include "savedInfo.h"
#include "timestamp.h"
#include "log.h"

#include "requester6.h"

static int      dataSocket = -1;

/**
 * Runs the downstream LDM server. On return, the socket will be closed.
 *
 * @param[in]     socket           Transport socket
 * @param[in]     inactiveTimeout  Timeout, in seconds, for the upstream LDM
 *                                 before closing the connection and
 *                                 re-connecting
 * @param[in]     upName           Name of upstream host.
 * @param[in,out] upAddr           IP address of upstream LDM server
 * @param[in]     upId             PID of associated upstream LDM
 * @param[in]     pqPathname       Pathname of product-queue.
 * @param[in]     expect           Data-products to expect
 * @param[in]     pq               Product-queue.  Must be open for writing.
 * @param[in]     isPrimary        Whether or not the initial data-product
 *                                 exchange-mode should use HEREIS or
 *                                 COMINGSOON/BLKDATA messages.
 * @retval NULL   Success.
 * @return        Error object. err_code() values:
 *                  - REQ6_SYSTEM_ERROR
 *                        err_cause() will be the system error.
 *                  - REQ6_DISCONNECT
 *                        The upstream LDM closed the connection. err_cause()
 *                        will be NULL.
 *                  - REQ6_TIMED_OUT
 *                        The connection timed-out. err_cause() will be NULL.
 */
static ErrorObj*
run_service(
    int                    socket,
    int                    inactiveTimeout,
    const char            *const upName,
    struct sockaddr_in    *const upAddr,
    const unsigned int     upId,
    const char            *pqPathname,
    prod_class_t          *const expect,
    pqueue                *const pq,
    const int              isPrimary)
{
    ErrorObj*   error = NULL; /* success */
    SVCXPRT*    xprt;

    log_assert(socket >= 0);
    log_assert(inactiveTimeout != 0);
    log_assert(upName != NULL);
    log_assert(upAddr != NULL);
    log_assert(pqPathname != NULL);
    log_assert(pq != NULL);

    xprt = svcfd_create(socket, 0, MAX_RPC_BUF_NEEDED);

    if (xprt == NULL) {
        error = ERR_NEW1(REQ6_SYSTEM_ERROR, NULL, 
            "Couldn't create RPC service for %s", upName);
    }
    else {
        if (!svc_register(xprt, LDMPROG, SIX, ldmprog_6, 0)) {
            error = ERR_NEW(REQ6_SYSTEM_ERROR, NULL, 
                "Couldn't register LDM service");
        }
        else {
            if (down6_init(upName, upAddr, pqPathname, pq)) {
                error = ERR_NEW(REQ6_SYSTEM_ERROR, NULL, 
                    "Couldn't initialize downstream LDM");
            }
            else {
                if (down6_set_prod_class(expect)) {
                    error = ERR_NEW1(REQ6_SYSTEM_ERROR, NULL, 
                        "Couldn't set expected product class: %s",
                        s_prod_class(NULL, 0, expect));
                }
                else {
                    as_init(isPrimary);

                    log_debug("Downstream LDM initialized");

                    do {
                        /*
                         * The only possible return values are:
                         *     0          if as_shouldSwitch()
                         *     ETIMEDOUT  if timeout exceeded.
                         *     ECONNRESET if connection closed.
                         *     EBADF      if socket not open.
                         *     EINVAL     if invalid timeout.
                         */
                        int status = one_svc_run(socket, inactiveTimeout);

                        (void)exitIfDone(0);

                        if (status == ECONNRESET) {
                            /*
                             * one_svc_run() called svc_getreqset(), which
                             * called svc_destroy()
                             */
                        }
                        else {
                            if (status == ETIMEDOUT) {
                                error = ERR_NEW1(REQ6_TIMED_OUT, NULL,
                                        "No heartbeat from upstream LDM for %u "
                                        "seconds. Disconnecting.",
                                        inactiveTimeout);
                            }
                            else if (status) {
                                error = ERR_NEW1(REQ6_SYSTEM_ERROR, NULL,
                                    "Error running downstream LDM6 server: "
                                        "pid=%u", upId);
                            }

                            svc_destroy(xprt);
                        } // Connection wasn't reset and `xprt` wasn't destroyed

                        xprt = NULL;
                    } while (xprt); // Service timeout loop
                } // Expected product-class set

                down6_destroy();
            } // Down6 module initialized

            // svc_destroy() calls svc_unregister(LDMPROG, SIX).
        } // RPC service registered

        if (xprt)
            svc_destroy(xprt);
    } // Server-side RPC transport created

    return error;
}


/**
 * Makes a request for data to an upstream LDM.
 *
 * @param upName        [in] The name of the host running the upstream LDM.
 * @param prodClass     [in] The desired class of data-products.
 * @param isPrimary     [in] Whether or not the transmission-mode should be
 *                      primary or alternate.
 * @param clnt          [in] The client-side handle to the upstream LDM.
 * @param id            [out] The PID of the upstream LDM.
 * @return              NULL on success; otherwise, the error-object.
 */
static ErrorObj*
make_request(
    const char* const           upName,
    const prod_class_t* const   prodClass,
    const int                   isPrimary,
    CLIENT* const               clnt,
    unsigned* const             id)
{
    ErrorObj*   errObj = NULL; /* no error */
    int         finished = 0;
    feedpar_t   feedpar;

    log_assert(prodClass != NULL);
    log_assert(clnt != NULL);
    log_assert(id != NULL);

    feedpar.max_hereis = isPrimary ? UINT_MAX : 0;
    feedpar.prod_class = dup_prod_class(prodClass);

    if (NULL == feedpar.prod_class) {
        errObj = ERR_NEW1(errno, NULL, "Couldn't duplicate product-class: %s",
            strerror(errno));
    }
    else {
        while (!errObj && !finished && exitIfDone(0)) {
            fornme_reply_t*     feedmeReply;

            log_debug("Calling feedme_6(...)");

            feedmeReply = feedme_6(&feedpar, clnt);

            if (!feedmeReply) {
                errObj = ERR_NEW(
                    REQ6_DISCONNECT,
                    ERR_NEW(clnt_stat(clnt), NULL, clnt_errmsg(clnt)),
                    "Upstream LDM didn't reply to FEEDME request");
            }
            else {
                if (feedmeReply->code == 0) {
                    log_notice_q("Upstream LDM-6 on %s is willing to be %s feeder",
                        upName, isPrimary ? "a primary" : "an alternate");

                    *id = feedmeReply->fornme_reply_t_u.id;
                    errObj = NULL;
                    finished = 1;
                }
                else {
                    if (feedmeReply->code == BADPATTERN) {
                        errObj = ERR_NEW1(
                            REQ6_BAD_PATTERN,
                            NULL,
                            "Upstream LDM can't compile pattern: %s",
                            s_prod_class(NULL, 0, feedpar.prod_class));
                    }
                    else if (feedmeReply->code == RECLASS) {
                        prod_class_t *allow =
                            feedmeReply->fornme_reply_t_u.prod_class;

                        if (allow->psa.psa_len == 0) {
                            errObj = ERR_NEW1( REQ6_NOT_ALLOWED, NULL,
                                "Request denied by upstream LDM: %s",
                                s_prod_class(NULL, 0, feedpar.prod_class));
                        }
                        else {
                            char wantStr[1984];

                            (void)s_prod_class(wantStr, sizeof(wantStr), 
                                feedpar.prod_class);
                            log_notice_q("Product reclassification by upstream LDM: "
                                "%s -> %s",
                                wantStr, s_prod_class(NULL, 0, allow));

                            if (tvEqual(TS_NONE, allow->from) ||
                                    tvEqual(TS_NONE, allow->to)) {
                                errObj = ERR_NEW1(
                                    REQ6_BAD_RECLASS,
                                    NULL,
                                    "Invalid RECLASS from upstream LDM: %s",
                                    s_prod_class(NULL, 0, allow));
                            }
                            else {
                                /*
                                 * The product-class in the reply from the
                                 * upstream LDM will be xdr_free()ed, so it's
                                 * duplicated for use in the next iteration.
                                 */
                                prod_class_t* clone = dup_prod_class(allow);

                                if (NULL == clone) {
                                    errObj = ERR_NEW1(
                                        REQ6_SYSTEM_ERROR,
                                        ERR_NEW(errno, NULL, strerror(errno)),
                                        "Couldn't clone product-class \"%s\"",
                                        s_prod_class(NULL, 0, allow));
                                }
                                else {
                                    free_prod_class(feedpar.prod_class);

                                    feedpar.prod_class = clone;
                                    errObj = NULL;
                                }
                            }           /* valid RECLASS prod-class */
                        }               /* non-empty RECLASS */
                    }                   /* RECLASS reply */
                }                       /* feedmeReply->code != 0 */

                (void)xdr_free((xdrproc_t)xdr_fornme_reply_t,
                    (char*)feedmeReply);
            }                           /* non-NULL reply */
        }                               /* try loop */

        free_prod_class(feedpar.prod_class);
    }                                   /* "feedpar.prod_class" allocated */

    return errObj;
}


/*
 * Creates a "signature" product-class based on a prototype product-class and 
 * data-product metadata.
 *
 * Arguements:
 *      protoClass      Pointer to the prototype product-class.  Must be a
 *                      non-signature product-class.  Caller may free on
 *                      return.
 *      info            The data-product metadata to use.  Caller may free on
 *                      return.
 *      newClass        Pointer to the pointer to the returned product-class.
 *                      *newClass is set on and only on success.  Upon
 *                      successful return, it is the caller's responsibility to
 *                      invoke free_prod_class() on *newClass.
 * Returns:
 *      NULL            Success.
 *      else            Failure.  Pointer to error structure.  err_code():
 *                              ENOMEM  
 *                              EINVAL  Pattern couldn't be compiled.
 */
static ErrorObj*
newSigClass(
    const prod_class_t* const   protoClass,
    const prod_info* const      info,
    prod_class_t** const        newClass)
{
    ErrorObj*    error = NULL;           /* success */
    prod_class_t* prodClass = new_prod_class(protoClass->psa.psa_len + 1);

    if (NULL == prodClass) {
        error = ERR_NEW1(errno, NULL, "Couldn't allocate new product-class: %s",
            strerror(errno));
    }
    else {
        int     err = cp_prod_class(prodClass, protoClass, 0);

        if (err) {
            error = ERR_NEW1(errno, NULL,
                "Couldn't copy product-class: %s", strerror(err));
        }
        else {
            size_t      bufLen = 4 + sizeof(signaturet)*2 + 1;  /* SIG=...\0 */
            char*       buf = (char*)malloc(bufLen);

            if (NULL == buf) {
                error = ERR_NEW1(errno, NULL,
                    "Couldn't allocate pattern-buffer: %s", strerror(errno));
            }
            else {
                prod_spec*      prodSpec = prodClass->psa.psa_val
                    + prodClass->psa.psa_len;

                (void)sprintf(buf, "SIG=%s",
                    s_signaturet(NULL, 0, info->signature));

                prodSpec->feedtype = NONE;
                prodSpec->pattern = buf;
                prodClass->psa.psa_len++;
            }                           /* pattern-buffer allocated */
        }                               /* product-class copied */

        if (error) {
            free_prod_class(prodClass);
        }
        else {
            *newClass = prodClass;
        }
    }                                   /* "class" allocated */

    return error;
}


/*
 * Returns a newly-allocated data-product selection-criteria corresponding to a
 * prototype one and adjusted according to the metadata of the last received
 * data-product.
 *
 * Arguements:
 *      protoClass      Pointer to prototype data-product 
 *                      selection-criteria.  It must not be a "signature"
 *                      product-class.  Caller may free on return.
 *      newClass        Pointer to the pointer to the adjusted data-product
 *                      selection-criteria.  *newClass is set on and only on 
 *                      success.  Upon successful return, it is the caller's 
 *                      responsibility to invoke free_prod_class() on *newClass.
 * Returns:
 *      NULL            Success. *newClass is set.
 *      else            Failure.  Pointer to error structure.  err_code():
 *                              EINVAL  info->arrival is TS_NONE
 */
static ErrorObj*
adjustByLastInfo(
    const prod_class_t* const     protoClass,
    prod_class_t** const          newClass)
{
    ErrorObj*                     errObj = NULL;  /* success */
    const prod_info* const        info = savedInfo_get();

    if (NULL == info) {
        prod_class_t* const       prodClass = dup_prod_class(protoClass);

        if (NULL == prodClass) {
            errObj = ERR_NEW1(errno, NULL,
                "Couldn't duplicate product-class: %s", strerror(errno));
        }
        else {
            *newClass = prodClass;
        }
    }
    else {
        if (tvIsNone(info->arrival)) {
            errObj = ERR_NEW(EINVAL, NULL,
                "Creation-time of last data-product is TS_NONE");
        }
        else {
            errObj = newSigClass(protoClass, info, newClass);
        }                               /* info->arrival != TS_NONE */
    }                                   /* NULL != info */

    return errObj;
}


/*******************************************************************************
 * Begin public API.
 ******************************************************************************/


/**
 * Initializes requester.  The class of products actually received will be the
 * intersection of the requested class and the class allowed by the upstream
 * LDM.
 *
 * @param[in] upName            Name of upstream host.
 * @param[in] port              The port on which to connect.
 * @param[in] request           Pointer to class of products to request.
 * @param[in] inactiveTimeout   The amount of time, in seconds, without hearing
 *                              from upstream LDM before taking action
 * @param[in] pqPathname        The pathname of the product-queue.
 * @param[in] pq                The product-queue.  Must be open for writing.
 * @param[in] isPrimary         Whether or not the initial data-product
 *                              exchange-mode should use HEREIS or
 *                              COMINGSOON/BLKDATA messages.
 * @retval NULL                 Success.  as_shouldSwitch() might be true.
 * @return                      Error object: err_code() values:
 *                                 - REQ6_TIMED_OUT
 *                                      The connection timed-out.
 *                                 - REQ6_UNKNOWN_HOST
 *                                      The upstream host is unknown.
 *                                 - REQ6_BAD_VERSION
 *                                      The upstream LDM isn't version 6.
 *                                 - REQ6_NO_CONNECT
 *                                      Couldn't connect to upstream LDM.
 *                                 - REQ6_DISCONNECT
 *                                      Connection established, but then closed.
 *                                 - REQ6_BAD_PATTERN
 *                                      The upstream LDM couldn't compile the
 *                                      regular expression of the request.
 *                                 - REQ6_NOT_ALLOWED,
 *                                      The upstream LDM refuses to send the
 *                                      requested products.
 *                                 - REQ6_BAD_RECLASS,
 *                                      The upstream LDM replied with an invalid
 *                                      RECLASS message.
 *                                 - REQ6_SYSTEM_ERROR
 *                                      A fatal system-error occurred.
 */
ErrorObj*
req6_new(
    const char* const                   upName,
    const unsigned                      port,
    const prod_class_t* const           request,
    const int                           inactiveTimeout,
    const char* const                   pqPathname,
    pqueue* const                       pq,
    const int                           isPrimary)
{
    prod_class_t*   prodClass = NULL;
    ErrorObj*       errObj = adjustByLastInfo(request, &prodClass);

    log_assert(upName != NULL);
    log_assert(request != NULL);
    log_assert(inactiveTimeout > 0);
    log_assert(pqPathname != NULL);
    log_assert(pq != NULL);

    if (errObj) {
        errObj = ERR_NEW(REQ6_SYSTEM_ERROR, errObj,
            "Couldn't adjust product-class");
    }
    else {
        CLIENT*                 clnt;
        struct sockaddr_in      upAddr;

        log_notice_q("LDM-6 desired product-class: %s",
            s_prod_class(NULL, 0, prodClass));

        errObj = ldm_clnttcp_create_vers(upName, port, SIX, &clnt,
                &dataSocket, &upAddr);

        if (errObj) {
            switch (err_code(errObj)) {
                case LDM_CLNT_UNKNOWN_HOST:
                    errObj = ERR_NEW(REQ6_UNKNOWN_HOST, errObj, NULL);
                    break;

                case LDM_CLNT_TIMED_OUT:
                    errObj = ERR_NEW(REQ6_TIMED_OUT, errObj, NULL);
                    break;

                case LDM_CLNT_BAD_VERSION:
                    errObj = ERR_NEW(REQ6_BAD_VERSION, errObj, NULL);
                    break;

                case LDM_CLNT_NO_CONNECT:
                    errObj = ERR_NEW(REQ6_NO_CONNECT, errObj, NULL);
                    break;

                default: /* LDM_CLNT_SYSTEM_ERROR */
                    errObj = ERR_NEW(REQ6_SYSTEM_ERROR, errObj, NULL);
                    break;
            }
        } /* failed "ldm_clnttcp_create_vers()" */
        else {
            /*
             * "clnt" and "dataSocket" have resources.
             */
            unsigned    id;

            log_info_q("Connected to upstream LDM-6 on host %s using port %u",
                upName, (unsigned)ntohs(upAddr.sin_port));

            errObj = make_request(upName, prodClass, isPrimary, clnt, &id);

            if (!errObj) {
                log_debug("Calling run_service()");

                errObj = run_service(dataSocket, inactiveTimeout, upName,
                        &upAddr, id, pqPathname, prodClass, pq, isPrimary);
            } /* successful "make_request()" */

            /*
             * Ensure release of client resources.
             */
            auth_destroy(clnt->cl_auth);
            clnt_destroy(clnt);

            /*
             * Ensure release of socket resources.
             */
            (void)close(dataSocket); /* might already be closed */
            dataSocket = -1;
        } /* "clnt" and "requestSocket" allocated */

        free_prod_class(prodClass); /* NULL safe */
    } /* "prodClass" allocated */

    return errObj;
}


/*
 * Closes the connection.  This is a safe function suitable for calling from a
 * signal handler.
 */
void
req6_close()
{
    if (dataSocket >= 0) {
        (void)close(dataSocket);
        dataSocket = -1;
    }
}
