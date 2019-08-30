/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 *   This module contains the "downstream" code for version 6 of the LDM.
 */

#include "config.h"

#include <arpa/inet.h>   /* inet_ntoa() */
#include <log.h>      /* log_assert() */
#include <errno.h>       /* error numbers */
#include <limits.h>      /* UINT_MAX */
#include <netinet/in.h>  /* sockaddr_in */
#include <rpc/rpc.h>
#include <stdlib.h>      /* NULL, malloc() */
#include <string.h>      /* strerror(), ... */

#include "autoshift.h"
#include "DownHelp.h"
#include "error.h"
#include "globals.h"     /* inactive_timeo */
#include "remote.h"
#include "ldm.h"         /* client-side LDM functions */
#include "ldmprint.h"    /* s_prod_info() */
#include "peer_info.h"   /* peer_info */
#include "pq.h"          /* pq_*(), pqe_*() */
#include "prod_class.h"  /* clss_eq(), prodInClass() */
#include "prod_info.h"
#include "savedInfo.h"
#include "timestamp.h"   /* clss_eq(), prodInClass() */
#include "log.h"
#include "xdr_data.h"

#include "down6.h"


typedef enum {
    DONT_EXPECT_BLKDATA,
    EXPECT_BLKDATA,
    IGNORE_BLKDATA
} comingSoonMode;


static struct pqueue* _pq;              /* product-queue */
static prod_class_t*    _class;         /* product-class to accept */
static char*          _datap;           /* BLKDATA area */
static prod_info*     _info;            /* product-info */
static unsigned       _remaining;       /* remaining BLKDATA bytes */
static int            _expectBlkdata;   /* expect BLKDATA message? */
static int            _initialized;     /* module initialized? */
static char           _upName[MAXHOSTNAMELEN+1];        /* upstream host name */
static char           _dotAddr[DOTTEDQUADLEN];  /* dotted-quad IP address */


/*******************************************************************************
 * Begin public API.
 ******************************************************************************/


/**
 * Initializes this module. The "arrival" time of the most-recently-received
 * product is initialized to TS_NONE.  This function prints diagnostic
 * messages via the ulog(3) module.  This function may be called multiple times
 * without calling down6_destroy().
 *
 * @param upName                 Name of upstream host.
 * @param upAddr                 Address of upstream host.
 * @param pqPath                 Pathname of product-queue.
 * @param pq                     Product-queue.
 * @retval 0                     if success.
 * @retval DOWN6_SYSTEM_ERROR    if system-error occurred (check errno or see
 *                               log).
 */
int down6_init(
    const char               *upName, 
    const struct sockaddr_in *upAddr,
    const char               *pqPath,
    pqueue                   *const pq)
{
    int         errCode = 0;            /* success */

    log_assert(upName != NULL);
    log_assert(upAddr != NULL);
    log_assert(pqPath != NULL);
    log_assert(pq != NULL);

    _initialized = 0;

    (void)strncpy(_dotAddr, inet_ntoa(upAddr->sin_addr), sizeof(_dotAddr)-1);
    (void)strncpy(_upName, upName, sizeof(_upName)-1);

    _upName[sizeof(_upName)-1] = 0;

    free_prod_class(_class);            /* NULL safe */
    _class = NULL;

    _datap = NULL;
    _pq = pq;
    _expectBlkdata = 0;

    if (NULL == _info) {
        _info = pi_new();

        if (NULL == _info) {
            err_log_and_free(
                ERR_NEW1(0, NULL,
                    "Couldn't allocate new product-information structure: %s",
                    strerror(errno)),
                ERR_FAILURE);

            errCode = DOWN6_SYSTEM_ERROR;
        }
    }

    if (!errCode)
        _initialized = 1;

    return errCode;
}


/*
 * Sets the class of products that the downstream LDM module will accept.
 * The caller may free the "offered" argument on return.
 *
 * Arguments:
 *      offered         Pointer to class of offered products.
 * Returns:
 *      0                       Success.
 *      DOWN6_SYSTEM_ERROR      System error.  errno is set.
 *      DOWN6_UNINITIALIZED     Module not initialized.
 */
int down6_set_prod_class(prod_class_t *offered)
{
    int         errCode = 0;            /* success */

    if (!_initialized) {
        log_error_q("down6_set_prod_class(): Module not initialized");
        errCode = DOWN6_UNINITIALIZED;
    }
    else {
        if (clsspsa_eq(_class, offered)) {
            /*
             * Update the time range.
             */
            _class->from = offered->from;
            _class->to = offered->to;
        }
        else {
            prod_class_t *pc = dup_prod_class(offered);

            if (pc == NULL) {
                errCode = DOWN6_SYSTEM_ERROR;
            }
            else {
                free_prod_class(_class);  /* NULL safe */

                _class = pc;
            }
        }
    }                                   /* module initialized */

    return errCode;
}


/*
 * Returns a copy of the product-class that this downstream LDM module will
 * accept.  The caller is responsible for invoking free_prod_class(prod_class_t*)
 * on a returned, non-NULL pointer.  This function prints diagnostic messages
 * via ulog(3).
 *
 * Returns:
 *      NULL            The product-class of this module couldn't be duplicated
 *                      or down6_set_prod_class() hasn't been called with a
 *                      valid product-class.
 *      else            A pointer to a copy of the product-class that this 
 *                      downstream LDM module wants.  free_prod_class() when 
 *                      done if non-NULL.
 */
prod_class_t *down6_get_prod_class()
{
    prod_class_t *clone;

    if (NULL == _class) {
        log_error_q("down6_get_prod_class(): Product-class not set");
        clone = NULL;
    }
    else {
        clone = dup_prod_class(_class);

        if (clone == NULL)
            log_error_q("Couldn't allocate new product-class: %s",
                strerror(errno));
    }

    return clone;
}


/**
 * Vets a product by determining if it's in the desired product class.
 * @param[in] infop               Product metadata
 * @retval    0                   Product is in the desired product class
 * @retval    DOWN6_UNWANTED      Product is not in the desired product class.
 *                                Notice message logged.
 * @retval    ENOMEM              Out of memory.  Error message logged.
 * @retval    DOWN6_SYSTEM_ERROR  System failure. Error message logged.
 */
static int
vetProduct(const prod_info *infop)
{
	int status;

	(void)set_timestamp(&_class->from);
	_class->from.tv_sec -= max_latency;
	dh_setInfo(_info, infop, _upName);

	if (prodInClass(_class, infop)) {
		status = 0;
	}
	else {
		const char* reason = tvCmp(_class->from, infop->arrival, >)
				? "too-old" : "unrequested";
		log_notice("Ignoring %s product: %s", reason,
					s_prod_info(NULL, 0, infop, log_is_enabled_debug));

		status = savedInfo_set(_info);
		if (status) {
			log_error("Couldn't save product-information: %s",
					savedInfo_strerror(status));
			status = DOWN6_SYSTEM_ERROR;
		}
		else {
			status = DOWN6_UNWANTED;
		}
	}                               // product not in desired class

	return status;
}


/**
 * Handles a product. On successful return, `savedInfo_get()` will return the
 * metadata of the product.
 *
 * This function updates `_class->from`.
 *
 * @param[in] prod                 Pointer to the data-product.
 * @retval    0                    Success.
 * @retval    DOWN6_UNWANTED       Unwanted data-product (e.g., duplicate, too
 *                                 old). Notice logged.
 * @retval    DOWN6_PQ             The product couldn't be inserted into the
 *                                 product-queue. Error logged.
 * @retval    DOWN6_PQ_BIG         The product is too big to be inserted into
 *                                 the product-queue. Error logged.
 * @retval    DOWN6_SYSTEM_ERROR   System error. Error logged.
 * @retval    DOWN6_UNINITIALIZED  Module not initialized. Error logged.
 */
int
down6_hereis(
    product*    prod)
{
    int         status = 0;            /* success */

    if (!_initialized) {
        log_error("Module not initialized");
        status = DOWN6_UNINITIALIZED;
    }
    else {
    	status = vetProduct(&prod->info);

    	if (status == 0)
            status = dh_saveProd(_pq, _info, prod->data, 1, 1);
    }                                   /* module initialized */

    return status;
}


/*
 * Handles a product notification.  This method should never be called.
 * An informational message is emitted via the ulog(3) module.
 *
 * Arguments:
 *      info                    Pointer to the product metadata.
 *      0                       Sucess.
 *      DOWN6_UNINITIALIZED     Module not initialized.
 */
/*ARGSUSED*/
int down6_notification(prod_info *info)
{
    /*
     * This should never be called.
     */
    int         errCode = 0;            /* success */

    if (!_initialized) {
        log_error_q("Module not initialized");
        errCode = DOWN6_UNINITIALIZED;
    }
    else {
        log_warning_q("notification6: %s", s_prod_info(NULL, 0, info,
                log_is_enabled_debug));
    }

    return errCode;
}


/**
 * Handles a product that will be delivered in pieces.
 *
 * This function updates `_class->from`.
 *
 * @param[in] argp                 Pointer to COMINGSOON argument
 * @retval    0                    Success.
 * @retval    DOWN6_UNWANTED       Unwanted data-product (e.g., duplicate, too
 *                                 old). Notice logged.
 * @retval    DOWN6_PQ             The product couldn't be inserted into the
 *                                 product-queue. Error logged.
 * @retval    DOWN6_PQ_BIG         The product is too big to be inserted into
 *                                 the product-queue. Error logged.
 * @retval    DOWN6_SYSTEM_ERROR   System error. Error logged.
 * @retval    DOWN6_UNINITIALIZED  Module not initialized. Error logged.
 */
int
down6_comingsoon(
    comingsoon_args*    argp)
{
    int         status = 0;            /* success */

    if (!_initialized) {
        log_error_q("Module not initialized");
        status = DOWN6_UNINITIALIZED;
    }
    else {
        prod_info *infop = argp->infop;

        if (_expectBlkdata) {
            log_warning_q("Discarding incomplete product: %s",
                s_prod_info(NULL, 0, infop,
                        log_is_enabled_debug));

            _expectBlkdata = 0;
        }

    	status = vetProduct(infop);

    	if (status == 0) {
            pqe_index      idx;             /* product-queue index */

            /*
             * Reserve space for the data-product in the product-queue.
             */
            status = pqe_new(_pq, _info, (void **)&_datap, &idx);

            if (!status) {
                /*
                 * The data-product isn't in the product-queue.  Setup for
                 * receiving the product's data via BLKDATA messages.
                 */
                (void)pqe_discard(_pq, &idx);
                _expectBlkdata = 1;

                /*
                 * Use the growable buffer of the "XDR-data" module as the
                 * location into which the XDR layer will decode the data.
                 */
                _remaining = _info->sz;
                _datap = xd_getBuffer(_remaining);
            }                           /* pqe_new() success */
            else if (status == EINVAL) {
                /*
                 * The data-product is invalid.
                 */
                err_log_and_free(
                    ERR_NEW1(0, NULL, "Invalid product: %s", 
                        s_prod_info(NULL, 0, _info,
                                log_is_enabled_debug)),
                    ERR_FAILURE);

                status = DOWN6_UNWANTED;
                if (savedInfo_set(_info))
                    status = DOWN6_SYSTEM_ERROR;
            }                           /* invalid product */
            else if (status == PQUEUE_BIG) {
                /*
                 * The data-product is too big to insert into the
                 * product-queue.
                 */
                log_error_q("Product too big: %s",
                        s_prod_info(NULL, 0, infop,
                                log_is_enabled_debug));

                status = DOWN6_PQ_BIG;
                if (savedInfo_set(_info)) {
                    status = DOWN6_SYSTEM_ERROR;
                }
            }                           /* product too big */
            else if (status == PQUEUE_DUP) {
                /*
                 * The data-product is already in the product-queue.
                 */
                if (log_is_enabled_info ||
                        log_is_enabled_debug)
                    log_info_q("comingsoon6: duplicate: %s",
                        s_prod_info(NULL, 0, infop,
                                log_is_enabled_debug));

                status = DOWN6_UNWANTED;
                if (savedInfo_set(_info)) {
                    status = DOWN6_SYSTEM_ERROR;
                }
                else {
                    /*
                     * Notify the autoshift module of the rejection.
                     * Use the approximate size of the COMINGSOON
                     * argument packet as the amount of data.
                     */
                    int error = as_process(0,
                        (size_t)(sizeof(InfoBuf) + 2*sizeof(u_int)));

                    if (error) {
                        err_log_and_free(
                            ERR_NEW1(0, NULL,
                                "Couldn't process rejection of "
                                "data-product: %s", strerror(error)),
                            ERR_FAILURE);

                        status = DOWN6_SYSTEM_ERROR;
                    }
                }                   /* "savedInfo" updated */
            }                       /* duplicate data-product */
            else {
            	log_error("pqe_new() failed: %s: %s",
                        strerror(status), s_prod_info(NULL, 0, _info, 1));

                status = DOWN6_PQ;     /* fatal product-queue error */
            }                           /* general pqe_new() failure */
        }                               /* product is in desired class */
    }                                   /* module initialized */

    return status;
}


/*
 * Accepts a block of data.  When the all of the outstanding data has been
 * successfuly received, savedInfo_get() will return the metadata of the
 * just-received data-product.
 *
 * Arguments:
 *      dpkp             Pointer to the block of data.
 * Returns:
 *      0                       Success.
 *      DOWN6_PQ                Problem with product-queue.
 *      DOWN6_PQ_BIG            Data product too big for product-queue.
 *      DOWN6_BAD_PACKET        The data packet is invalid.
 *      DOWN6_SYSTEM_ERROR      System error.
 *      DOWN6_UNINITIALIZED     Module not initialized.
 */
int
down6_blkdata(
    datapkt*    dpkp)
{
    int         errCode = 0;            /* success */

    if (!_initialized) {
        log_error_q("Module not initialized");
        errCode = DOWN6_UNINITIALIZED;
    }
    else {
        if (!_expectBlkdata) {
            log_warning_q("Unexpected BLKDATA");
        }
        else {
            if(memcmp(dpkp->signaturep, _info->signature, sizeof(signaturet))
                != 0) {

                log_warning_q("Invalid BLKDATA signature");

                errCode = DOWN6_BAD_PACKET;
            }
            else {
                dbuf*    data = &dpkp->data;
                unsigned got = data->dbuf_len;

                if (got > _remaining) {
                    log_warning_q(
                        "BLKDATA size too large: remaining %u; got %u",
                        _remaining, got);
                    xd_reset();

                    _expectBlkdata = 0;
                    errCode = DOWN6_BAD_PACKET;
                }
                else {
                    /*
                     * The XDR layer has already decoded the packet's data into
                     * the buffer of the "XDR-data" module.
                     */
                    _remaining -= got;

                    if (0 == _remaining) {
                        errCode = dh_saveProd(_pq, _info, _datap, 0, 1);
                        _expectBlkdata = 0;

                        xd_reset();
                    }                   /* received all bytes */
                }                       /* size <= remaining */
            }                           /* right MD5 checksum */
        }                               /* expect BLKDATA message */
    }                                   /* module initialized */

    return errCode;
}


/*
 * Destroys this down6_t module -- freeing any allocated resources.  This
 * function prints diagnostic messages via the ulog(3) module.  This function
 * is idempotent.
 */
void down6_destroy()
{
    free_prod_class(_class);            /* NULL safe */
    _class = NULL;

    if (_expectBlkdata) {
        log_info_q("Discarding incomplete product: %s",
            s_prod_info(NULL, 0, _info,
                    log_is_enabled_debug));

        _expectBlkdata = 0;
    }

    pi_free(_info);                     /* NULL safe */
    _info = NULL;

    _pq = NULL;
    _initialized = 0;
}
