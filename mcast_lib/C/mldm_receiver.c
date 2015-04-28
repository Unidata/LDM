/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_receiver.c
 * @author: Steven R. Emmerson
 *
 * This file implements the multicast LDM receiver, which uses a VCMTP receiver
 * to receive LDM data-products sent to a multicast group via a VCMTP sender.
 */

#include "config.h"

#include "down7.h"
#include "mldm_receiver.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "pq.h"
#include "mcast.h"
#include "xdr.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>

/**
 * The multicast LDM receiver data-structure:
 */
struct mlr {
    pqueue*               pq;       // product-queue to use */
    Down7*                down7;    // pointer to associated downstream LDM-7
    McastReceiver*        receiver; // VCMTP C Receiver
    char*                 prod;     // Start of product in product-queue
    size_t                prodSize; // Size of VCMTP product in bytes
    pqe_index             index;    // Product-queue index of reserved region
    volatile sig_atomic_t done;
};

/**
 * Locks the product-queue of a multicast LDM receiver.
 *
 * @param[in] mlr       Pointer to the multicast LDM receiver.
 * @retval    0         Success.
 * @retval    EAGAIN    The lock could not be acquired because the maximum
 *                      number of recursive calls has been exceeded.
 * @retval    EDEADLK   A deadlock condition was detected.
 */
static int
lockPq(
        Mlr* const mlr)
{
    int status = pq_lock(mlr->pq);

    if (status)
        LOG_ADD1("Couldn't lock product-queue: %s", strerror(status));

    return status;
}

/**
 * Unlocks the product-queue of a multicast LDM receiver.
 *
 * @param[in] mlr       Pointer to the multicast LDM receiver.
 * @retval    0         Success.
 * @retval    EPERM     The current thread does not own the lock.
 */
static int
unlockPq(
        Mlr* const mlr)
{
    int status = pq_unlock(mlr->pq);

    if (status)
        LOG_ADD1("Couldn't unlock product-queue: %s", strerror(status));

    return status;
}

/**
 * Allocates space in a product-queue for a VCMTP product if it's not a
 * duplicate and returns the starting memory-location for the data.
 *
 * @pre                   The product-queue is locked.
 * @param[in]  mlr        Pointer to the multicast LDM receiver.
 * @param[in]  signature  The MD5 checksum of the LDM data-product.
 * @param[in]  prodSize   Size of the XDR-encoded LDM data-product in bytes.
 * @retval     0          Success. `mlr->prod` and `mlr->prodSize` are set. If
 *                        `mlr->prod == NULL`, then the data-product is already
 *                        in the LDM product-queue.
 * @retval    -1          Failure. `log_add()` called.
 * @post                  The product-queue is locked.
 */
static int
allocateSpace(
        Mlr* const restrict        mlr,
        const signaturet           signature,
        const size_t               prodSize)
{
    udebug("%s:allocateSpace(): Entered: prodSize=%lu", __FILE__,
            (unsigned long)prodSize);

    char sigStr[sizeof(signaturet)*2 + 1];
    int  status = pqe_newDirect(mlr->pq, prodSize, signature, &mlr->prod,
            &mlr->index);

    if (status) {
        if (status == PQUEUE_DUP) {
            if (ulogIsVerbose()) {
                (void)sprint_signaturet(sigStr, sizeof(sigStr), signature);
                uinfo("Duplicate product: sig=%s, size=%lu", sigStr,
                        (unsigned long)prodSize);
            }
            mlr->prod = NULL;
            status = 0;
        }
        else {
            LOG_ADD1("Couldn't allocate region for %lu-byte data-product",
                    prodSize);
            status = -1;
        }
    }
    else {
        if (ulogIsDebug()) {
            (void)sprint_signaturet(sigStr, sizeof(sigStr), signature);
            udebug("Allocated queue-space for product: sig=%s, size=%lu",
                    sigStr, (unsigned long)prodSize);
        }
        mlr->prodSize = prodSize;
        status = 0;
    } /* region allocated in product-queue */

    udebug("%s:allocateSpace(): Returning: mlr->prod=%p, mlr->prodSize=%lu",
            __FILE__, mlr->prod, (unsigned long)mlr->prodSize);

    return status;
}

/**
 * Accepts notification of the beginning of a VCMTP product. Allocates a region
 * in the LDM product-queue to receive the VCMTP product, which is an
 * XDR-encoded LDM data-product. Called by VCMTP layer.
 *
 * @param[in,out]  obj          Pointer to the associated multicast LDM receiver
 *                              object.
 * @param[in]      prodSize     Size of the product in bytes.
 * @param[in]      metadata     Information about the product.
 * @param[in]      metaSize     Size of the information.
 * @param[out]     prod         Starting location for product or `NULL` if
 *                              duplicate product.
 * @retval         0            Success. `*prod` is set. If NULL, then
 *                              data-product is already in LDM product-queue.
 * @retval         -1           Failure. `log_add()` called.
 */
static int
bop_func(
        void* const       obj,
        const size_t      prodSize,
        const void* const metadata,
        const unsigned    metaSize,
        void** const      prod)
{
    Mlr* mlr = obj;
    int  status;

    udebug("%s:bop_func(): Entered: prodSize=%lu, metaSize=%lu, prod=%p",
            __FILE__, (unsigned long)prodSize, metaSize, prod);

    if (sizeof(signaturet) > metaSize) {
        LOG_START1("Product metadata too small for signature: %u bytes",
                metaSize);
        status = -1;
    }
    else {
        /*
         * Because this function is called by the VCMTP multicast and unicast
         * threads, visibility of changes is ensured by locking the
         * product-queue.
         */
        if (lockPq(mlr)) {
            status = -1;
        }
        else {
            if (mlr->prod ) {
                uerror("Premature product arrival. Discarding previous product.");
                (void)pqe_discard(mlr->pq, mlr->index);
                mlr->prod  = NULL;
            }

            status = allocateSpace(mlr, metadata, prodSize);

            if (status == 0)
                *prod = mlr->prod; // will be `NULL` if duplicate product

            (void)unlockPq(mlr);
        }
    }

    if (status)
        log_log(LOG_ERR); // because called by VCMTP layer
    udebug("%s:bop_func(): Returning: mlr->prod=%p, mlr->prodSize=%lu",
            __FILE__, mlr->prod, (unsigned long)mlr->prodSize);

    return status;
}

/**
 * Finishes inserting a data-product into the allocated product-queue region
 * associated with a multicast LDM receiver or discards the region.
 *
 * @pre              The product-queue is locked.
 * @param[in] mlr    Pointer to the multicast LDM receiver.
 * @retval    0      Success.
 * @retval    -1     Error. `log_add()` called.
 * @post             The product-queue is locked.
 */
static int
insertOrDiscard(
        Mlr* const restrict mlr)
{
    int status;

    if ((status = pqe_insert(mlr->pq, mlr->index)) != 0)
        (void)pqe_discard(mlr->pq, mlr->index);

    if (status) {
        LOG_ADD("Couldn't insert data-product into product-queue: status=%d",
                status);
        status = -1;
    }

    return status;
}

/**
 * Tracks the last data-product to be successfully received.
 *
 * @param[in] mlr   Pointer to the multicast LDM receiver.
 * @param[in] info  Pointer to the metadata of the last data-product to be
 *                  successfully received. Caller may free when it's no longer
 *                  needed.
 */
static inline void
lastReceived(
        Mlr* const restrict             mlr,
        const prod_info* const restrict info)
{
    down7_lastReceived(mlr->down7, info);
}

/**
 * Finishes inserting a received VCMTP product into an LDM product-queue as an
 * LDM data-product.
 *
 * @pre                    The product-queue is locked.
 * @param[in] mlr          Pointer to the multicast LDM receiver.
 * @param[in] info         LDM data-product metadata. Caller may free when it
 *                         is no longer needed.
 * @param[in] dataSize     Actual number of bytes received.
 * @retval    0            Success.
 * @retval    -1           Error. `log_add()` called. The allocated region in
 *                         the product-queue was released.
 * @pre                    The product-queue is locked.
 */
static int
finishInsertion(
        Mlr* const restrict             mlr,
        const prod_info* const restrict info,
        const size_t                    dataSize)
{
    int status;

    if (info->sz > dataSize) {
        LOG_ADD3("LDM product size > VCMTP product size: "
                "LDM=%u, VCMTP=%lu, ident=\"%s\"",
                info->sz, (unsigned long)dataSize, info->ident);
        status = -1;
        (void)pqe_discard(mlr->pq, mlr->index);
    }
    else {
        status = insertOrDiscard(mlr);

        if (status) {
            LOG_ADD("Couldn't finish inserting %u-byte data-product \"%s\"",
                    info->sz, info->ident);
        }
        else {
            if (ulogIsVerbose()) {
                char infoStr[512];

                uinfo("%s: Inserted: %s",
                        __FILE__, s_prod_info(infoStr, sizeof(infoStr), info, 1));
            }
            lastReceived(mlr, info);
        }
    }
    return status;
}

/**
 * Accepts notification from the VCMTP layer of the complete reception of a
 * product. Finishes inserting the VCMTP product (which is an XDR-encoded
 * data-product) into the associated LDM product-queue.
 *
 * @param[in,out]  obj          Pointer to the associated multicast LDM receiver
 *                              object.
 * @retval         0            Success.
 * @retval         -1           Error. `log_add()` called. The allocated space
 *                              in the LDM product-queue was released.
 */
static int
eop_func(
        void* const obj)
{
    Mlr* const mlr = (Mlr*)obj;
    int        status;

    /*
     * Because this function is called by the VCMTP multicast and unicast
     * threads, visibility of changes is ensured by locking the product-queue.
     */
    if (lockPq(mlr)) {
        status = -1;
    }
    else {
        if (mlr->prod == NULL) {
            // Duplicate product
            status = 0;
        }
        else {
            prod_info     info;
            XDR           xdrs;

            xdrmem_create(&xdrs, mlr->prod, mlr->prodSize, XDR_DECODE);
            (void)memset(&info, 0, sizeof(info));   // for `xdr_prod_info()`

            if (!xdr_prod_info(&xdrs, &info)) {
                LOG_START1("Couldn't decode LDM product metadata from %lu-byte "
                        "VCMTP product", (unsigned long)mlr->prodSize);
                status = -1;
                pqe_discard(mlr->pq, mlr->index);
            }
            else {
                status = finishInsertion(mlr, &info,
                        mlr->prodSize-(xdrs.x_private-xdrs.x_base));
                xdr_free(xdr_prod_info, (char*)&info);
            }                                       // "info" allocated

            xdr_destroy(&xdrs);
            mlr->prod  = NULL;
        }

        (void)unlockPq(mlr);
    }

    if (status)
        log_log(LOG_ERR); // because called by VCMTP layer

    return status;
}

/**
 * Accepts notification from the VCMTP layer of the missed reception of a
 * product. Queues the product for reception by other means. This function must
 * and does return immediately.
 *
 * @param[in,out]  obj          Pointer to the associated multicast LDM receiver
 *                              object.
 * @param[in]      iProd        Index of the product that was missed.
 */
static void
missed_prod_func(
        void*                obj,
        const VcmtpProdIndex iProd)
{
    Mlr* mlr = obj;

    if (mlr->prod ) {
        (void)pqe_discard(mlr->pq, mlr->index);
        mlr->prod  = NULL;
    }

    down7_missedProduct(((Mlr*)obj)->down7, iProd);
}

/**
 * Initializes a multicast LDM receiver.
 *
 * @param[out] mlr            The multicast LDM receiver to initialize.
 * @param[in]  mcastInfo      Pointer to information on the multicast group.
 * @param[in]  down7          Pointer to the associated downstream LDM-7 object.
 * @retval     0              Success.
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 * @retval     LDM7_INVAL     `pq == NULL || missed_product == NULL ||
 *                            mcastInfo == NULL || down7 == NULL`. `log_add()`
 *                            called.
 * @retval     LDM7_VCMTP     VCMTP error. `log_add()` called.
 */
static int
init(
        Mlr* const restrict             mlr,
        const McastInfo* const restrict mcastInfo,
        Down7* const restrict           down7)
{
    int            status;
    McastReceiver* receiver;

    if (mlr == NULL) {
        LOG_ADD0("NULL multicast-LDM-receiver argument");
        return LDM7_INVAL;
    }
    if (mcastInfo == NULL) {
        LOG_ADD0("NULL multicast-group-information argument");
        return LDM7_INVAL;
    }
    if (down7 == NULL) {
        LOG_ADD0("NULL downstream LDM-7 argument");
        return LDM7_INVAL;
    }

    status = mcastReceiver_new(&receiver, mcastInfo->server.inetId,
            mcastInfo->server.port, bop_func, eop_func, missed_prod_func,
            mcastInfo->group.inetId, mcastInfo->group.port, mlr);
    if (status) {
        LOG_ADD0("Couldn't create VCMTP receiver");
        return LDM7_MCAST;
    }

    mlr->receiver = receiver;
    mlr->pq = down7_getPq(down7); // for convenience
    mlr->down7 = down7;
    mlr->prod  = NULL;
    mlr->done  = 0;

    return 0;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new multicast LDM receiver object.
 *
 * @param[in]  mcastInfo      Pointer to information on the multicast group.
 * @param[in]  down7          Pointer to the associated downstream LDM-7 object.
 * @retval     NULL           Failure. `log_add()` called.
 * @return                    Pointer to a new multicast LDM receiver object.
 *                            The caller should call `mlr_free()` when it's no
 *                            longer needed.
 */
Mlr*
mlr_new(
        const McastInfo* const restrict mcastInfo,
        Down7* const restrict           down7)
{
    Mlr* mlr = LOG_MALLOC(sizeof(Mlr), "multicast LDM receiver object");

    if (mlr) {
        if (init(mlr, mcastInfo, down7)) {
            LOG_ADD0("Couldn't initialize multicast LDM receiver");
            free(mlr);
            mlr = NULL;
        }
    }

    return mlr;
}

/**
 * Frees the resources of a multicast LDM receiver object.
 *
 * @param[in,out] mlr   The multicast LDM receiver object.
 */
void
mlr_free(
        Mlr* const  mlr)
{
    mcastReceiver_free(mlr->receiver);
    free(mlr);
}

/**
 * Executes a multicast LDM receiver. Doesn't return until `mlr_stop()` is
 * called or an error occurs.
 *
 * @param[in] mlr            The multicast LDM receiver to execute.
 * @retval    LDM7_SHUTDOWN  `mlr_stop()` was called.
 * @retval    LDM7_INVAL     `mlr == NULL`. `log_add()` called.
 * @retval    LDM7_MCAST     Multicast error. `log_add()` called.
 */
int
mlr_start(
        Mlr* const  mlr)
{
    int status;

    if (NULL == mlr) {
        LOG_ADD0("NULL multicast-LDM-receiver argument");
        status = LDM7_INVAL;
    }
    else if ((status = mcastReceiver_execute(mlr->receiver)) != 0) {
        LOG_ADD0("Couldn't execute multicast LDM receiver");
        status = LDM7_MCAST;
    }
    else {
        status = LDM7_SHUTDOWN;
    }

    return status;
}

/**
 * Cleanly stops an executing multicast LDM receiver. Undefined behavior
 * results if called from a signal handler. Returns immediately. Idempotent.
 *
 * @param[in] mlr  Pointer to the multicast LDM receiver to stop.
 */
void
mlr_stop(
        Mlr* const mlr)
{
    mlr->done = 1;
    mcastReceiver_stop(mlr->receiver);
}
