/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_receiver.c
 * @author: Steven R. Emmerson
 *
 * This file implements the multicast LDM receiver, which uses a FMTP receiver
 * to receive LDM data-products sent to a multicast group via a FMTP sender.
 */

#include "config.h"

#include "down7.h"
#include "fmtp.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "mcast_info.h"
#include "mldm_receiver.h"
#include "PerProdNotifier.h"
#include "pq.h"
#include "prod_info.h"
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
    pqueue*               pq;         // Product-queue to use */
    Downlet*              downlet;    // Associated one-time downstream LDM-7
    FmtpReceiver*         receiver;   // FMTP C Receiver
    volatile sig_atomic_t done;
};

/**
 * Allocates space in a product-queue for a FMTP product if it's not a
 * duplicate and returns the starting memory-location for the data.
 *
 * @param[in]  mlr        Pointer to the multicast LDM receiver.
 * @param[in]  signature  The MD5 checksum of the LDM data-product.
 * @param[in]  prodSize   Size of the XDR-encoded LDM data-product in bytes.
 * @param[out] prodStart  Start of the region in the product-queue to which to
 *                        write the product.
 * @param[out] pqeIndex   Reference to reserved space in product-queue.
 * @retval     0          Success. `prodStart` is set. If `NULL`, then the
 *                        data-product is already in the LDM product-queue.
 * @retval    -1          Failure. `log_add()` called.
 */
static int
allocateSpace(
        Mlr* const restrict       mlr,
        const signaturet          signature,
        const size_t              prodSize,
        char** const restrict     prodStart,
        pqe_index* const restrict pqeIndex)
{
    log_debug_1("Entered: prodSize=%zu", prodSize);

    char sigStr[sizeof(signaturet)*2 + 1];
    int  status = pqe_newDirect(mlr->pq, prodSize, signature, prodStart,
            pqeIndex);

    if (status) {
        if (status == PQUEUE_DUP) {
            if (log_is_enabled_info) {
                (void)sprint_signaturet(sigStr, sizeof(sigStr), signature);
                log_info_q("Duplicate product: sig=%s, size=%zu", sigStr,
                        prodSize);
            }
            *prodStart = NULL;
            status = 0;
        }
        else {
            log_add("Couldn't allocate region for %zu-byte data-product",
                    prodSize);
            status = -1;
        }
    }
    else {
        if (log_is_enabled_debug) {
            (void)sprint_signaturet(sigStr, sizeof(sigStr), signature);
            log_debug_1("Allocated queue-space for product: "
                    "sig=%s, size=%zu", sigStr, prodSize);
        }
    } /* region allocated in product-queue */

    log_debug_1("Returning: prodStart=%p, prodSize=%zu", *prodStart,
            prodSize);

    return status;
}

/**
 * Accepts notification of the beginning of a FMTP product. Allocates a region
 * in the LDM product-queue to receive the FMTP product, which is an
 * XDR-encoded LDM data-product. Called by FMTP layer.
 *
 * @param[in,out]  mlr          The associated multicast LDM receiver.
 * @param[in]      prodSize     Size of the product in bytes.
 * @param[in]      metadata     Information about the product.
 * @param[in]      metaSize     Size of the information.
 * @param[out]     prod         Starting location for product or `NULL` if
 *                              duplicate product.
 * @param[out]     pqeIndex     Reference to reserved space in product-queue.
 * @retval         0            Success. `*prod` is set. If NULL, then
 *                              data-product is already in LDM product-queue.
 * @retval         -1           Failure. `log_add()` called.
 */
static int
bop_func(
        Mlr* const restrict        mlr,
        const size_t               prodSize,
        const void* const restrict metadata,
        const unsigned             metaSize,
        void** const restrict      prod,
        pqe_index* const restrict  pqeIndex)
{
    /*
     * This function is called on both the multicast and unicast threads of the
     * FMTP module.
     */
    int  status;

    log_debug_1("prodSize=%zu, metaSize=%u, prod=%p",
            prodSize, metaSize, prod);

    if (sizeof(signaturet) > metaSize) {
        log_add("Product metadata too small for signature: %u bytes",
                metaSize);
        status = -1;
    }
    else {
        char* prodStart;
        status = allocateSpace(mlr, metadata, prodSize, &prodStart, pqeIndex);

        if (status == 0)
            *prod = prodStart; // will be `NULL` if duplicate product
    }

    if (status)
        log_flush_error(); // because called by FMTP layer

    log_debug_1("Returning: prod=%p, prodSize=%zu",
            *prod, prodSize);

    return status;
}

/**
 * Tries to insert a data-product in its allocated product-queue region
 * that was received via multicast.
 *
 * @param[in] mlr       Pointer to the multicast LDM receiver.
 * @param[in] pqeIndex  Pointer to the reference to allocated space in the
 *                      product-queue.
 * @retval    0         Success.
 * @retval    -1        Error. `log_add()` called.
 */
static int
tryToInsert(
        Mlr* const restrict       mlr,
        pqe_index* const restrict pqeIndex)
{
    int status = pqe_insert(mlr->pq, *pqeIndex);

    if (status != 0) {
        log_add("Couldn't insert data-product into product-queue");
        status = -1;
    }
    else {
        downlet_incNumProds(mlr->downlet);
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
    downlet_lastReceived(mlr->downlet, info);
}

/**
 * Finishes inserting a received FMTP product into an LDM product-queue as an
 * LDM data-product.
 *
 * @param[in] mlr          Pointer to the multicast LDM receiver.
 * @param[in] info         LDM data-product metadata. Caller may free when it's
 *                         no longer needed.
 * @param[in] dataSize     Maximum possible size of the data component of the
 *                         data-product in bytes.
 * @param[in] pqeIndex     Pointer to the reference to the allocated space in
 *                         the product-queue.
 * @retval    0            Success.
 * @retval    -1           Error. `log_add()` called. The allocated region in
 *                         the product-queue was released.
 */
static int
finishInsertion(
        Mlr* const restrict             mlr,
        const prod_info* const restrict info,
        pqe_index* const restrict       pqeIndex)
{
    int status = tryToInsert(mlr, pqeIndex);
    if (status) {
        log_add("Couldn't insert %u-byte data-product \"%s\"", info->sz,
                info->ident);
    }
    else {
        if (log_is_enabled_info) {
            char infoStr[LDM_INFO_MAX];

            log_info_1("Received: %s",
                    s_prod_info(infoStr, sizeof(infoStr), info, 1));
        }
        lastReceived(mlr, info);
    }
    return status;
}

/**
 * Accepts notification from the FMTP layer of the complete reception of a
 * product. Finishes inserting the FMTP product (which is an XDR-encoded
 * data-product) into the associated LDM product-queue.
 *
 * @param[in,out]  mlr       Pointer to the associated multicast LDM receiver.
 * @param[in]      prodStart Pointer to the start of the XDR-encoded
 *                           data-product in the product-queue or NULL,
 *                           indicating a duplicate product.
 * @param[in]      prodSize  The size of the XDR-encoded data-product in bytes.
 *                           Ignored if `prodStart == NULL`.
 * @param[in]      pqeIndex  Reference to the reserved space in the product-
 *                           queue. Ignored if `prodStart == NULL`.
 * @retval         0         Success.
 * @retval         -1        Error. `log_add()` called. The allocated space
 *                           in the LDM product-queue was released.
 */
static int
eop_func(
        Mlr* const restrict  mlr,
        void* const restrict prodStart,
        const size_t         prodSize,
        pqe_index* const     pqeIndex)
{
    /*
     * This function is called on both the FMTP multicast and unicast threads.
     */

    int status;

    if (prodStart == NULL) {
        // Duplicate product
        status = 0;
    }
    else {
        InfoBuf    infoBuf;
        prod_info* info = ib_init(&infoBuf);
        XDR        xdrs;

        xdrmem_create(&xdrs, prodStart, prodSize, XDR_DECODE);

        if (!xdr_prod_info(&xdrs, info)) {
            log_add("Couldn't decode LDM product metadata from %zu-byte "
                    "FMTP product", prodSize);
            status = -1;
            pqe_discard(mlr->pq, *pqeIndex);
        }
        else {
            status = finishInsertion(mlr, info, pqeIndex);
        }                                       // "info" allocated

        xdr_destroy(&xdrs);
    }

    if (status)
        log_flush_error(); // because called by FMTP layer

    return status;
}

/**
 * Accepts notification from the FMTP layer of the missed reception of a
 * product. Queues the product for reception by other means. This function must
 * and does return immediately.
 *
 * @param[in,out]  obj          Pointer to the associated multicast LDM receiver
 *                              object.
 * @param[in]      iProd        Index of the product that was missed.
 * @param[in]      pqeIndex     Reference to reserved space in product-queue or
 *                              `NULL`.
 */
static void
missed_prod_func(
        void* const restrict      obj,
        const FmtpProdIndex       iProd,
        pqe_index* const restrict pqeIndex)
{
    /*
     * This function is called on both the FMTP multicast and unicast threads.
     */

    Mlr* mlr = obj;

    if (pqeIndex)
        (void)pqe_discard(mlr->pq, *pqeIndex);

    downlet_missedProduct(((Mlr*)obj)->downlet, iProd);
}

/**
 * Initializes a multicast LDM receiver.
 *
 * @param[out] mlr            The multicast LDM receiver to initialize.
 * @param[in]  mcastInfo      Pointer to information on the multicast group.
 * @param[in]  iface          IPv4 address of interface to use for receiving
 *                            multicast and unicast packets.
 * @param[in]  pq             Product queue. Must exist until `deinit()`
 *                            returns.
 * @param[in]  downlet        Pointer to associated one-time downstream LDM-7
 * @retval     0              Success.
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 * @retval     LDM7_INVAL     `pq == NULL || missed_product == NULL ||
 *                            mcastInfo == NULL || downlet == NULL`. `log_add()`
 *                            called.
 * @retval     LDM7_FMTP     FMTP error. `log_add()` called.
 */
static int
init(
        Mlr* const restrict             mlr,
        const McastInfo* const restrict mcastInfo,
        const char* const restrict      iface,
        pqueue* const restrict          pq,
        Downlet* const restrict         downlet)
{
    int            status;
    FmtpReceiver* receiver;

    if (mlr == NULL) {
        log_add("NULL multicast-LDM-receiver argument");
        return LDM7_INVAL;
    }
    if (mcastInfo == NULL) {
        log_add("NULL multicast-group-information argument");
        return LDM7_INVAL;
    }
    if (downlet == NULL) {
        log_add("NULL one-time downstream LDM-7 argument");
        return LDM7_INVAL;
    }

    void* notifier;
    status = ppn_new(&notifier, bop_func, eop_func, missed_prod_func, mlr);
    if (status) {
        log_add("Couldn't create per-product notifier");
        return LDM7_MCAST;
    }
    else {
        if (log_is_enabled_info) {
            char* const miStr = mi_format(mcastInfo);
            if (miStr == NULL) {
                log_add("Couldn't format multicast information");
                ppn_free(notifier);
                return LDM7_SYSTEM;
            }
            log_info_q("Initializing FMTP receiver with mcastInfo=%s, iface=%s",
                    miStr, iface);
            free(miStr);
        }

        status = fmtpReceiver_new(&receiver, mcastInfo->server.inetId,
                mcastInfo->server.port, notifier, mcastInfo->group.inetId,
                mcastInfo->group.port, iface);
        if (status) {
            log_add("Couldn't create FMTP receiver");
            ppn_free(notifier);
            return LDM7_MCAST;
        }
    } // `notifier` allocated

    mlr->receiver = receiver;
    mlr->pq = pq;
    mlr->downlet = downlet;
    mlr->done  = 0;

    return 0;
}

static int
deinit(Mlr* const mlr)
{
    fmtpReceiver_free(mlr->receiver);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new multicast LDM receiver object.
 *
 * @param[in]  mcastInfo      Pointer to information on the multicast group.
 * @param[in]  iface          IP address of interface to use for receiving
 *                            multicast and unicast packets.
 * @param[in]  pq             Product queue. Must exist until `deinit()`
 *                            returns.
 * @param[in]  downlet        Pointer to associated one-time downstream LDM-7
 * @retval     NULL           Failure. `log_add()` called.
 * @return                    Pointer to a new multicast LDM receiver object.
 *                            The caller should call `mlr_free()` when it's no
 *                            longer needed.
 */
Mlr*
mlr_new(
        const McastInfo* const restrict mcastInfo,
        const char* const restrict      iface,
        pqueue* const restrict          pq,
        Downlet* const restrict         downlet)
{
    Mlr* mlr = log_malloc(sizeof(Mlr), "multicast LDM receiver object");

    if (mlr) {
        if (init(mlr, mcastInfo, iface, pq, downlet)) {
            log_add("Couldn't initialize multicast LDM receiver");
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
mlr_delete(
        Mlr* const  mlr)
{
    deinit(mlr);
    free(mlr);
}

/**
 * Executes a multicast LDM receiver. Doesn't return until `mlr_halt()` is
 * called or an error occurs.
 *
 * @param[in] mlr            The multicast LDM receiver to execute.
 * @retval    0              `mlr_stop()` was called.
 * @retval    LDM7_INVAL     `mlr == NULL`. `log_add()` called.
 * @retval    LDM7_MCAST     Multicast error. `log_add()` called.
 * @see `mlr_stop()`
 */
int
mlr_run(
        Mlr* const  mlr)
{
    int status;

    if (NULL == mlr) {
        log_add("NULL multicast-LDM-receiver argument");
        status = LDM7_INVAL;
    }
    else {
        status = fmtpReceiver_execute(mlr->receiver);
        if (mlr->done) {
            status = LDM7_OK;
        }
        else if (status) {
            log_add("Error executing multicast LDM receiver");
            status = LDM7_MCAST;
        }
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
mlr_halt(
        Mlr* const mlr)
{
    mlr->done = 1;
    fmtpReceiver_stop(mlr->receiver);
}
