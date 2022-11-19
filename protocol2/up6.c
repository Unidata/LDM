/*
 * Copyright 2018 University Corporation for Atmospheric Research.
 * All rights reserved.
 * See file "COPYRIGHT" in the top-level source-directory for conditions.
 *
 * This module contains the "upstream" code for version 6 of the LDM.
 */
#include "config.h"

#include <log.h>      /* log_assert() */
#include <errno.h>       /* error numbers */
#include <arpa/inet.h>   /* for <netinet/in.h> under FreeBSD 4.5-RELEASE */
#include <netinet/in.h>  /* sockaddr_in */
#include <rpc/rpc.h>     /* CLIENT, clnt_stat */
#include <signal.h>      /* sig_atomic_t */
#include <stdbool.h>
#include <stdlib.h>      /* NULL, malloc() */
#include <string.h>      /* strerror() */
#include <strings.h>     /* strncasecmp() */
#if defined(_AIX) || 1
#   include <unistd.h>
#   include <fcntl.h>
#endif
#include <time.h>

#include "abbr.h"        /* set_abbr_ident() */
#include "LdmConfFile.h"
#include "autoshift.h"
#include "error.h"
#include "ldm.h"         /* LDM version 6 client-side functions */
#include "ldmprint.h"    /* s_prod_class(), s_prod_info() */
#include "log.h"
#include "peer_info.h"   /* peer_info */
#include "pq.h"          /* pq_close(), pq_open() */
#include "prod_class.h"  /* clss_eq() */
#include "rpcutil.h"     /* clnt_errmsg() */
#include "UpFilter.h"
#include "log.h"
#include "globals.h"
#include "remote.h"
#include "uldb.h"

#include "up6.h"

typedef enum {
    FEED, NOTIFY
} up6_mode_t;

static struct pqueue* _pq; /* the product-queue */
static const prod_class_t* _class; /* selected product-class */
static const signaturet* _signature; /* signature of last product */
static pq_match _mt = TV_GT; /* time-matching condition */
static CLIENT* _clnt; /* client-side transport */
static struct sockaddr_in _downAddr; /* downstream host addr. */
static UpFilter* _upFilter; /* filters data-products */
static up6_mode_t _mode; /* FEED, NOTIFY */
static int _socket = -1; /* socket # */
static int _isPrimary; /* use HEREIS or CSBD */
static unsigned _interval; /* pq_suspend() interval */
static const char* _downName; /* downstream host name */
static time_t _lastSendTime; /* time of last activity */
static int _flushNeeded; /* connection needs a flush? */

typedef enum clnt_stat clnt_stat_t;

static up6_error_t up6_error(
        clnt_stat_t stat)
{
    up6_error_t error;

    switch (stat) {
    case RPC_PROGVERSMISMATCH:
        error = UP6_VERSION_MISMATCH;
        break;
    case RPC_TIMEDOUT:
        error = UP6_TIME_OUT;
        break;
    case RPC_UNKNOWNHOST:
    case RPC_PMAPFAILURE:
    case RPC_PROGNOTREGISTERED:
    case RPC_PROGUNAVAIL:
        error = UP6_UNAVAILABLE;
        break;
    case RPC_CANTSEND:
        error = UP6_CLOSED;
        break;
    default:
        error = UP6_SYSTEM_ERROR;
        /* no break */
    }

    return error;
}

/**
 * Returns the logging-level appropriate to an upstream LDM 6 error-code.
 *
 * @param errCode       [in] Upstream LDM 6 error-code.
 * @return              The logging-level corresponding to the error-code.
 */
static int loggingLevel(
        const up6_error_t   errCode)
{
    /*
     * A failure due to a closed connection or time-out is not unusual because
     * a downstream LDM being fed is expected to auto-shift its transmission-
     * mode and one being notified is expected to be manually terminated.
     */
    return (errCode == UP6_TIME_OUT || errCode == UP6_CLOSED)
            ? ERR_INFO
            : ERR_NOTICE;
}

/**
 * Logs a failure to transmit to the downstream LDM 6.
 *
 * @param msg           [in] The log message.
 * @param errObj        [in] The error-object.
 * @return              The error-code of the error-object.
 */
static up6_error_t logFailure(
        const char* const   msg,
        ErrorObj* const     errObj)
{
    up6_error_t errCode = (up6_error_t)err_code(errObj);

    err_log_and_free(ERR_NEW(0, errObj, msg), loggingLevel(errCode));

    return errCode;
}

/**
 * @param[in] info    Pointer to the data-product's metadata.
 * @param[in] data    Pointer to the data-product's data.
 * @param[in] xprod   Pointer to an XDR-encoded version of the data-product (data and metadata).
 * @param[in] size    Size, in bytes, of the XDR-encoded version.
 * @param[in] arg     Ignored
 * @retval 0                     Success.
 * @retval UP6_CLIENT_FAILURE    Client-side RPC transport couldn't be created from Internet address
 *                               and LDM program number. `log_add()` called.
 * @retval UP6_VERSION_MISMATCH  Downstream LDM isn't version 6. `log_add()` called.
 * @retval UP6_TIME_OUT          Communication timed-out. `log_add()` called.
 * @retval UP6_INTERRUPT         This function was interrupted.   `log_add()` called.
 * @retval UP6_UNKNOWN_HOST      Downstream host is unknown. `log_add()` called.
 * @retval UP6_UNAVAILABLE       Downstream LDM can't be reached for some reason (see log).
 *                               `log_add()` called.
 * @retval UP6_SYSTEM_ERROR      System-error occurred (check errno or see log). `log_add()` called.
 * @retval UP6_CLOSED            Connection closed. `log_add()` called.
 */
/*ARGSUSED*/
static int notify(
        const prod_info* const info,
        const void* const      data,
        void* const            xprod,
        const size_t           size,
        void* const            arg)
{
    int status = 0; // Default success

    if (upFilter_isMatch(_upFilter, info)) {
        int isDebug = log_is_enabled_debug;

        log_log(log_is_enabled_debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO,
                "notifying: %s", s_prod_info(NULL, 0, info, isDebug));

        (void)notification_6((prod_info*) info, _clnt);
        /*
         * The status will be RPC_TIMEDOUT unless an error occurs because the
         * RPC call uses asynchronous message-passing.
         */
        if (clnt_stat(_clnt) != RPC_TIMEDOUT) {
            log_add("NOTIFICATION failure: %s", clnt_errmsg(_clnt));
            status = up6_error(clnt_stat(_clnt));
        }
        else {
            _lastSendTime = time(NULL );
            _flushNeeded = 1;
        }
    }

    return status;
}

/**
 * Asynchronously sends a data-product to the downstream LDM.
 *
 * @param[in] infop                 Pointer to the metadata of the data.
 * @param[in] datap                 Pointer to beginning of data.
 * @retval    0                     Success.
 * @retval    UP6_CLIENT_FAILURE    Client-side RPC transport couldn't be  created from Internet
 *                                  address and LDM program number. `log_add()` called.
 * @retval    UP6_VERSION_MISMATCH  Downstream LDM isn't version 6. `log_add()` called.
 * @retval    UP6_TIME_OUT          Communication timed-out. `log_add()` called.
 * @retval    UP6_INTERRUPT         This function was interrupted.     `log_add()` called.
 * @retval    UP6_UNKNOWN_HOST      Downstream host is unknown. `log_add()` called.
 * @retval    UP6_UNAVAILABLE       Downstream LDM can't be reached for some reason (see log).
 *                                  `log_add()` called.
 * @retval    UP6_SYSTEM_ERROR      System-error occurred (check errno or see log). `log_add()`
 *                                  called.
 * @retval    UP6_CLOSED            Connection closed. `log_add()` called.
 */
static int
hereis(
    const prod_info* infop,
    const void*      datap)
{
    int       status = 0; // Default success
    product   prod;

    prod.info = *infop;
    prod.data = (void*) datap;

    (void)hereis_6(&prod, _clnt);
    /*
     * The status will be RPC_TIMEDOUT unless an error occurs because the RPC call uses asynchronous
     * message-passing.
     */
    if (clnt_stat(_clnt) != RPC_TIMEDOUT) {
        log_add("HEREIS: %s", clnt_errmsg(_clnt));
        status = up6_error(clnt_stat(_clnt));
    }
    else {
        /*
         * NB: Sets "_lastSendTime".
         */
        _lastSendTime = time(NULL);
        _flushNeeded = 1;

        if (log_is_enabled_debug)
            log_debug("%s", s_prod_info(NULL, 0, infop, 1));
    }

    return status;
}

/**
 * Sets "_lastSendTime".
 *
 * @param[in] infop                 Pointer to the metadata of the data.
 * @param[in] datap                 Pointer to beginning of data.
 * @retval    0                     Success
 * @retval    UP6_CLIENT_FAILURE    Client-side RPC transport couldn't be  created from Internet
 *                                  address and LDM program number. `log_add()` called.
 * @retval    UP6_VERSION_MISMATCH  Upstream LDM isn't version 6. `log_add()` called.
 * @retval    UP6_TIME_OUT          Communication timed-out. `log_add()` called.
 * @retval    UP6_INTERRUPT         This function was interrupted.     `log_add()` called.
 * @retval    UP6_UNKNOWN_HOST      Upstream host is unknown. `log_add()` called.
 * @retval    UP6_UNAVAILABLE       Upstream LDM can't be reached for some reason (see log).
 *                                  `log_add()` called.
 * @retval    UP6_SYSTEM_ERROR      System-error occurred (check errno or see log). `log_add()` called.
 * @retval    UP6_CLOSED            Connection closed. `log_add()` called.
 */
static int
csbd(   const prod_info* infop,
        const void*      datap)
{
    int                 status = 0; // Default success
    comingsoon_args     comingSoon;
    comingsoon_reply_t* reply;

    comingSoon.infop = (prod_info*) infop;
    comingSoon.pktsz = infop->sz;
    reply = comingsoon_6(&comingSoon, _clnt);

    if (NULL == reply) {
        log_add("COMINGSOON: %s", clnt_errmsg(_clnt));
        status = up6_error(clnt_stat(_clnt));
    }
    else {
        _lastSendTime = time(NULL );
        _flushNeeded = 0; /* because synchronous RPC call */

        if (*reply != DONT_SEND) {
            datapkt pkt;

            pkt.signaturep = (signaturet *) &infop->signature; /* not const */
            pkt.pktnum = 0;
            pkt.data.dbuf_len = infop->sz;
            pkt.data.dbuf_val = (void*) datap;

            (void)blkdata_6(&pkt, _clnt);
            /*
             * The status will be RPC_TIMEDOUT unless an error occurs because
             * the RPC call uses asynchronous message-passing.
             */
            if (clnt_stat(_clnt) != RPC_TIMEDOUT) {
                log_add("Error sending BLKDATA: %s", clnt_errmsg(_clnt));
                status = up6_error(clnt_stat(_clnt));
            }
            else {
                _lastSendTime = time(NULL );
                _flushNeeded = 1; /* because asynchronous RPC call */

                if (log_is_enabled_debug)
                    log_debug("%s", s_prod_info(NULL, 0, infop, 1));
            }
        }

        xdr_free((xdrproc_t) xdr_comingsoon_reply_t, (char*) reply);
    } /* successful comingsoon_6() */

    return status;
}

/**
 * Transmits a data-product to a downstream LDM.  Called by pq_sequence().
 *
 * @param[in] info               Pointer to the data-product's metadata.
 * @param[in] data               Pointer to the data-product's data.
 * @param[in] xprod              Pointer to an XDR-encoded version of the data-product (data and
 *                               metadata).
 * @param[in] size               Size, in bytes, of the XDR-encoded version.
 * @param[in] arg                Ignored
 * @retval 0                     Success.
 * @retval UP6_CLIENT_FAILURE    Client-side RPC transport couldn't be created from Internet address
 *                               and LDM program number. `log_add()` called.
 * @retval UP6_VERSION_MISMATCH  Downstream LDM isn't version 6. `log_add()` called.
 * @retval UP6_TIME_OUT          Communication timed-out. `log_add()` called.
 * @retval UP6_INTERRUPT         This function was interrupted. `log_add()` called.
 * @retval UP6_UNKNOWN_HOST      Downstream host is unknown. `log_add()` called.
 * @retval UP6_UNAVAILABLE       Downstream LDM can't be reached for some reason (see log).
 *                               `log_add()` called.
 * @retval UP6_SYSTEM_ERROR      System-error occurred (check errno or see log). `log_add()` called.
 * @retval UP6_CLOSED            Connection closed. `log_add()` called.
 */
/*ARGSUSED*/
static int feed(
        const prod_info* const info,
        const void* const      data,
        void* const            xprod,
        const size_t           size,
        void* const            arg)
{
    int status = 0; // Default success;

    if (upFilter_isMatch(_upFilter, info)) {
    	if (log_is_enabled_debug) {
    		log_debug("sending: %s", s_prod_info(NULL, 0, info, true));
    	}
    	else {
    		log_info("sending: %s", s_prod_info(NULL, 0, info, false));
    	}

        status = _isPrimary ? hereis(info, data) : csbd(info, data);
    } /* product passes up-filter */

    return status;
}

/**
 * Flushes the connection. Sets "_lastSendTime".
 *
 * @retval NULL     Success.
 * @return          Error object.
 */
static ErrorObj*
flushConnection(
        void)
{
#if 0
    static struct timeval ZERO_TIMEOUT = { 0, 0 };
    /*
     * Flush the connection by forcing the RPC layer to send a nil data-product
     * via an asynchronous HEREIS message and then return immediately (rather
     * than by sending a synchronous NULLPROC message, which would necessitate
     * waiting for a response). The "xdr_void" and "ZERO_TIMEOUT" cause the
     * HEREIS message to be buffered, the buffer flushed, and an immediate
     * return.
     */
	if (clnt_call(_clnt, HEREIS, (xdrproc_t)xdr_product, (caddr_t)dp_getNil(),
    		(xdrproc_t)xdr_void, (caddr_t)NULL, ZERO_TIMEOUT) == RPC_TIMEDOUT) {
#else
    if (nullproc_6(NULL, _clnt)) {
#endif
        _lastSendTime = time(NULL );
        _flushNeeded = 0;
        log_debug("flushConnection() success");
        return NULL;
    }

    return ERR_NEW2(up6_error(clnt_stat(_clnt)), NULL,
                "flushConnection() failure to %s: %s",
                _downName, clnt_errmsg(_clnt));
}

/**
 * This function doesn't return until an error occurs or the connection is closed.  It calls
 * exitIfDone() after potentially lengthy operations.
 *
 * @retval 0                    Success.
 * @retval UP6_CLIENT_FAILURE   Client-side RPC transport couldn't be  created from Internet address
 *                              and LDM program number.
 * @retval UP6_SYSTEM_ERROR     System-error occurred (check errno or see log).
 * @retval UP6_PQ               Problem with product-queue.
 */
static up6_error_t up6_run(
        void)
{
    up6_error_t errCode = UP6_SUCCESS; /* success */
    int flags;
    char buf[64];
    char* sig = _signature == NULL
            ? "NONE"
            : s_signaturet(buf, sizeof(buf), *_signature);

    log_assert(_mode == FEED || _mode == NOTIFY);
    log_assert(_class != NULL);

    if (NOTIFY == _mode) {
        log_set_upstream_id(_downName, false);
        log_notice_q("Starting Up(%s/6): %s, SIG=%s", PACKAGE_VERSION,
                s_prod_class(NULL, 0, _class), sig);
    }
    else {
        log_set_upstream_id(_downName, true);
        log_notice_q("Starting Up(%s/6): %s, SIG=%s, %s", PACKAGE_VERSION,
                s_prod_class(NULL, 0, _class), sig,
                _isPrimary ? "Primary" : "Alternate");
    }

    log_notice_q("topo:  %s %s", _downName, upFilter_toString(_upFilter));
    /* s_feedtypet(clss_feedtypeU(_class))); */

    /*
     * Beginning with maintenance-level 11 of AIX 4.3.3 and
     * maintenance-level 5 of AIX 5.1, the TCP socket that will be
     * "turned around" is set to non-blocking -- contrary to the RPC,
     * socket, and TCP standards.  Because an upstream LDM assumes a
     * blocking socket to the downstream LDM, the following code is
     * necessary -- even though it shouldn't be.
     */
    flags = fcntl(_socket, F_GETFL);

    if (-1 == flags) {
        log_syserr("fcntl(F_GETFL) failure");
        errCode = UP6_SYSTEM_ERROR;
    }
    else if ((flags & O_NONBLOCK)
            && -1 == fcntl(_socket, F_SETFL, flags & ~O_NONBLOCK)) {

        log_syserr("fcntl(F_SETFL) failure");
        errCode = UP6_SYSTEM_ERROR;
    }
    else {
        /*
         * Create a client-side RPC transport on the connection.
         */
        do {
            _clnt = clnttcp_create(&_downAddr, LDMPROG, SIX, &_socket,
                    MAX_RPC_BUF_NEEDED, 0);

            /* TODO: adjust sending buffer size in above */
        } while (_clnt == NULL && rpc_createerr.cf_stat == RPC_TIMEDOUT);

        if (_clnt == NULL ) {
            log_error_q("Couldn't connect to downstream LDM on %s%s", _downName,
                    clnt_spcreateerror(""));

            errCode = UP6_CLIENT_FAILURE;
        }
        else {
            while (exitIfDone(0)) {
                const int status = pq_sequence(_pq, _mt, _class, _mode == FEED ? feed : notify,
                        NULL);

                if (status < 0) {
                    /*
                     * The product-queue module reported a problem.
                     */
                    if (status == PQ_END) {
                        log_debug("End of product-queue");

                        if (_flushNeeded) {
                                (void) exitIfDone(0);

                                ErrorObj* errObj = flushConnection();
                                if (errObj) {
                                    errCode = logFailure("Couldn't flush connection", errObj);
                                    break;
                                }
                        }

                        time_t timeSinceLastSend = time(NULL) - _lastSendTime;

                        if (_interval <= timeSinceLastSend) {
                            _flushNeeded = 1;
                        }
                        else {
                            (void) exitIfDone(0);
                            (void) pq_suspend(_interval - timeSinceLastSend);
                        }
                    } /* end-of-queue reached */
                    else {
                        log_add("pq_sequence() failure");
                        log_flush_error();

                        errCode = UP6_PQ;
                        break;
                    }
                } /* problem in product-queue module */
                else if (status == UP6_CLOSED) {
                    log_flush_info();
                    break;
                }
                else if (status) {
                    log_flush_error();
                    errCode = UP6_SYSTEM_ERROR;
                    break;
                }
            } /* pq_sequence() loop */

            auth_destroy(_clnt->cl_auth);
            clnt_destroy(_clnt);

            _clnt = NULL;
        } /* _clnt != NULL */
    } /* socket set to blocking */

    return errCode;
}

/*
 * Destroys the upstream LDM module -- freeing resources.
 * This function prints diagnostic messages via the ulog(3) module.
 */
static void up6_destroy(
        void)
{
    if (_clnt) {
        auth_destroy(_clnt->cl_auth);
        clnt_destroy(_clnt);
        _clnt = NULL;
    }

    if (_pq) {
        (void) pq_close(_pq);
        _pq = NULL;
    }
}

/**
 * Initializes the upstream LDM module.
 *
 * @param[in] socket            Connected socket to be used by up6_t module.
 * @param[in] downName          Name of host of downstream LDM.  Caller may free
 *                              after `up6_destroy()` is called.
 * @param[in] downAddr          Pointer to address of host of downstream LDM.
 *                              Caller may free after `up6_destroy()` is called.
 * @param[in] prodClass         Pointer to class of products to send. Caller may
 *                              free after `up6_destroy()` is called.
 * @param[in] signature         Pointer to the signature of the last,
 *                              successfully- received data-product.  May be
 *                              NULL. Caller may free after `up6_destroy()` is
 *                              called.
 * @param[in] pqPath            Pointer to pathname of product-queue. Caller may
 *                              free.
 * @param[in] interval          pq_suspend() interval in seconds.
 * @param[in] upFilter          Pointer to product-class for filtering
 *                              data-products. May not be NULL. Caller may free
 *                              after `up6_destroy()` is called.
 * @param[in] mode              Transfer mode: FEED or NOTIFY.
 * @param[in] isPrimary         If "mode == FEED", then data-product
 *                              transfer-mode.
 * @retval    0                 Success.
 * @retval    UP6_PQ            Problem with the product-queue.
 * @retval    UP6_SYSTEM_ERROR  System-error occurred.  `log_add()` called and
 *                              `errno` set.
 */
static up6_error_t up6_init(
        const int socket,
        const char* const downName,
        const struct sockaddr_in* const downAddr,
        const prod_class_t* const prodClass,
        const signaturet* const signature,
        const char* pqPath,
        const unsigned interval,
        UpFilter* const upFilter,
        const up6_mode_t mode,
        int isPrimary)
{
    int errCode;

    log_assert(socket >= 0);
    log_assert(downName != NULL);
    log_assert(prodClass != NULL);
    log_assert(pqPath != NULL);
    log_assert(upFilter != NULL);

    /*
     * Open the product-queue read-only.
     */
    if ((errCode = pq_open(pqPath, PQ_READONLY, &_pq))) {
        if (PQ_CORRUPT == errCode) {
            log_error_q("The product-queue \"%s\" is inconsistent", pqPath);
        }
        else {
            log_error_q("Couldn't open product-queue \"%s\": %s", pqPath,
                    strerror(errCode));
        }

        errCode = UP6_PQ;
    }
    else {
        int cursorSet = 0;

        if (signature != NULL ) {
            int err = pq_setCursorFromSignature(_pq, *signature);

            if (err == 0) {
                _mt = TV_GT;
                cursorSet = 1;
            }
            else if (PQ_NOTFOUND == err) {
                err_log_and_free(
                        ERR_NEW1(0, NULL, "Data-product with signature "
                                "%s wasn't found in product-queue",
                                s_signaturet(NULL, 0, *signature)), ERR_NOTICE);
            }
            else {
                err_log_and_free(ERR_NEW2(0,
                        ERR_NEW(UP6_PQ, NULL, pq_strerror(_pq, err)),
                        "Couldn't set product-queue (%s) cursor from signature "
                        "(%s)",
                        pqPath, s_signaturet(NULL, 0, *signature)),
                        ERR_FAILURE);

                errCode = UP6_PQ;
            }
        } /* "signature != NULL" */

        if (errCode == 0 && !cursorSet) {
            int err = pq_cClassSet(_pq, &_mt, prodClass);

            if (err) {
                err_log_and_free(ERR_NEW2(0,
                        ERR_NEW(UP6_PQ, NULL, pq_strerror(_pq, err)),
                        "Couldn't set product-queue (%s) cursor from "
                        "product-class (%s)",
                        pqPath, s_prod_class(NULL, 0, prodClass)), ERR_FAILURE);

                errCode = UP6_PQ;
            }
        }

        if (errCode == 0) {
            _class = prodClass;
            _signature = signature;
            _downName = downName;
            _clnt = NULL;
            _socket = socket;
            _downAddr = *downAddr;
            _interval = interval;
            _upFilter = upFilter;
            _lastSendTime = time(NULL );
            _flushNeeded = 0;
            _mode = mode;
            _isPrimary = isPrimary;

            errCode = UP6_SUCCESS;
        } /* product-queue cursor set */
    } /* product-queue opened */

    return (up6_error_t) errCode;
}

/*******************************************************************************
 * Begin public API.
 ******************************************************************************/

/*
 * Constructs a new, upstream LDM object that feeds a downstream LDM. function
 * prints diagnostic messages via the ulog(3) module.  It calls exitIfDone()
 * after potentially lengthy operations.
 *
 * Arguments:
 *      socket          Connected socket to be used by up6_t module.
 *      downName        Pointer to name of host of downstream LDM.  Caller may
 *                      free or modify on return.
 *      downAddr        Pointer to address of host of downstream LDM. Caller may
 *                      free or modify on return.
 *      prodClass       Pointer to class of products to send.  Caller may free
 *                      or modify on return.
 *      signature       Pointer to the signature of the last, successfully-
 *                      received data-product.  May be NULL.
 *      pqPath          Pointer to pathname of product-queue.  Caller may
 *                      free or modify on return.
 *      interval        pq_suspend() interval in seconds.
 *      upFilter        Pointer to product-class for filtering data-products.
 *                      May not be NULL.
 *      isPrimary       Whether data-product exchange-mode should be
 *                      primary (i.e., use HEREIS) or alternate (i.e.,
 *                      use COMINGSOON/BLKDATA).
 * Returns:
 *      0                       Success.
 *      UP6_PQ                  Problem with the product-queue.
 *      UP6_SYSTEM_ERROR        Failure.  errno is set and message should be
 *                              logged.
 */
int up6_new_feeder(
        const int socket,
        const char* const downName,
        const struct sockaddr_in* const downAddr,
        const prod_class_t* const prodClass,
        const signaturet* const signature,
        const char* pqPath,
        const unsigned interval,
        UpFilter* const upFilter,
        const int isPrimary)
{
    int errCode = up6_init(socket, downName, downAddr, prodClass, signature,
            pqPath, interval, upFilter, FEED, isPrimary);

    if (!errCode) {
        errCode = up6_run();

        up6_destroy();
    }

    return errCode;
}

/*
 * Constructs a new, upstream LDM object that sends product notifications to a
 * downstream LDM.  This function prints diagnostic messages via the ulog(3)
 * module.  It calls exitIfDone() after potentially lengthy operations.
 *
 * Arguments:
 *      socket          Connected socket to be used by up6_t module.
 *      downName        Pointer to name of host of downstream LDM.  Caller may
 *                      free or modify on return.
 *      downAddr        Pointer to address of host of downstream LDM. Caller may
 *                      free or modify on return.
 *      prodClass       Pointer to class of products to send.  Caller may free
 *                      or modify on return.
 *      signature       Pointer to the signature of the last, successfully-
 *                      received data-product.  May be NULL.
 *      pqPath          Pointer to pathname of product-queue.  Caller may
 *                      free or modify on return.
 *      interval        pq_suspend() interval in seconds.
 *      upFilter        Pointer to product-class for filtering data-products.
 *                      May not be NULL.
 * Returns:
 *      0                       Success.
 *      UP6_PQ                  Problem with the product-queue.
 *      UP6_SYSTEM_ERROR        Failure.  errno is set and message should be
 *                              logged.
 */
int up6_new_notifier(
        const int socket,
        const char* const downName,
        const struct sockaddr_in* const downAddr,
        const prod_class_t* const prodClass,
        const signaturet* const signature,
        const char* pqPath,
        const unsigned interval,
        UpFilter* const upFilter)
{
    int errCode = up6_init(socket, downName, downAddr, prodClass, signature,
            pqPath, interval, upFilter, NOTIFY, 0);

    if (!errCode) {
        errCode = up6_run();

        up6_destroy();
    }

    return errCode;
}

/*
 * Closes all connections to the downstream LDM.  This safe function is
 * suitable for being called from a signal handler.
 */
void up6_close()
{
    if (_socket >= 0) {
        (void) close(_socket);
        _socket = -1;
    }
}
