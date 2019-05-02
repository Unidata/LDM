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
#include "pq.h"
#include "prod_info.h"
#include "ProdNotifier.h"
#include "xdr.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include "MldmRcvr.h"

/**
 * The multicast LDM receiver data-structure:
 */
struct mlr {
    pqueue*               pq;         // Product-queue to use */
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
 * @retval     0          Success. `*prodStart` is set.
 * @retval     EINVAL     `prodStart == NULL || pqeIndex == NULL`. `log_add()`
 *                        called.
 * @retval     EEXIST     The data-product is already in the LDM product-queue.
 *                        `*prodStart` is not set. `log_add()` called.
 * @retval     E2BIG      Product is too large for the queue. `*prodStart` is
 *                        not set. `log_add()` called.
 * @return                <errno.h> error code. `*prodStart` is not set.
 *                        `log_add()` called.
 */
static int
allocateSpace(
        Mlr* const restrict       mlr,
        const signaturet          signature,
        const size_t              prodSize,
        void** const restrict     prodStart,
        pqe_index* const restrict pqeIndex)
{
    log_debug("Entered: prodSize=%zu", prodSize);

    char sigStr[sizeof(signaturet)*2 + 1];
    int  status = pqe_newDirect(mlr->pq, prodSize, signature, prodStart,
            pqeIndex);

    if (status) {
        if (status == PQ_DUP) {
            // `log_add()` was not called
            if (log_is_enabled_info) {
                (void)sprint_signaturet(sigStr, sizeof(sigStr), signature);
                log_add("Duplicate product: sig=%s, size=%zu", sigStr,
                        prodSize);
            }
            status = EEXIST;
        }
        else if (status == PQ_BIG) {
            status = E2BIG;
        }
        else {
            log_add("pqe_newDirect() failure");
        }
    }
    else {
        if (log_is_enabled_debug) {
            (void)sprint_signaturet(sigStr, sizeof(sigStr), signature);
            log_debug("Allocated queue-space for product: "
                    "sig=%s, size=%zu", sigStr, prodSize);
        }
    } /* region allocated in product-queue */

    log_debug("Returning: status=%d, prodStart=%p, prodSize=%zu", status,
            *prodStart, prodSize);

    return status;
}

/**
 * Accepts notification from the FMTP component of the beginning of a product.
 * Allocates a region in the LDM product-queue to receive the product,
 * which is an XDR-encoded LDM data-product. Called by FMTP component.
 *
 * @param[in,out]  mlr          The associated multicast LDM receiver.
 * @param[in]      prodSize     Size of the product in bytes.
 * @param[in]      metadata     Information about the product.
 * @param[in]      metaSize     Size of the information.
 * @param[out]     prod         Starting location for product or `NULL` if
 *                              duplicate product.
 * @param[out]     pqeIndex     Reference to reserved space in product-queue.
 * @retval         0            Success. `*prod` is set.
 * @retval         EINVAL       `prod == NULL || pqeIndex == NULL || metaSize <
 *                              sizeof(signaturet)`. `*prod` is not set.
 *                              `log_add()` called.
 * @retval         EEXIST       The data-product is already in the LDM
 *                              product-queue. `*prod` is not set. `log_add()`
 *                              called.
 * @retval         E2BIG        Product is too large for the queue. `*prod` is
 *                              not set. `log_add()` called.
 * @return                      <errno.h> error code. `*prod` is not set.
 *                              `log_add()` called.
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

    log_debug("Entered: prodSize=%zu, metaSize=%u, prod=%p", prodSize, metaSize,
            prod);
    log_assert(mlr && metadata && prod && pqeIndex);

    int  status;

    if (sizeof(signaturet) > metaSize) {
        log_add("Metadata of product {prodSize=%zu, metaSize=%u} is too small "
                "for signature", prodSize, metaSize);
        status = EINVAL;
    }
    else {
        status = allocateSpace(mlr, metadata, prodSize, prod, pqeIndex);
    }

    log_debug("Returning: status=%d, *prod=%p", status, *prod);

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
    downlet_lastReceived(info);
}


/**
 * Accepts notification from the FMTP layer of the complete reception of a
 * product. Finishes inserting the FMTP product (which is an XDR-encoded
 * data-product) into the associated LDM product-queue.
 *
 * @param[in,out]  mlr         Pointer to the associated multicast LDM receiver.
 * @param[in]      prodIndex   FMTP product-index
 * @param[in]      prodStart   Pointer to the start of the XDR-encoded
 *                             data-product in the product-queue or NULL,
 *                             indicating a duplicate product.
 * @param[in]      prodSize    The size of the XDR-encoded data-product in
 *                             bytes. Ignored if `prodStart == NULL`.
 * @param[in]      pqeIndex    Reference to the reserved space in the product-
 *                             queue. Ignored if `prodStart == NULL`.
 * @param[in]      duration    Amount of time, in seconds, it took to transmit
 *                             the product
 * @param[in]      numRetrans  Number of FMTP data-block retransmissions
 * @retval         0           Success. `pqe_discard()` called.
 * @retval         EPROTO      RPC decode error. `pqe_discard()` called.
 *                             `log_add()` called.
 * @retval         EIO         Product-queue error. `pqe_discard()` called.
 *                             `log_add()` called.
 */
static int
eop_func(
        Mlr* const restrict             mlr,
        const FmtpProdIndex             prodIndex,
        void* const restrict            prodStart,
        const size_t                    prodSize,
        const pqe_index* const restrict pqeIndex,
        const double                    duration,
        const unsigned                  numRetrans)
{
    /*
     * This function is called on both the FMTP multicast and unicast threads.
     */

    log_assert(mlr && pqeIndex);

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
            log_add("Couldn't decode LDM product metadata from %zu-byte FMTP "
                    "product", prodSize);
            (void)pqe_discard(mlr->pq, pqeIndex);
            status = EPROTO;
        }
        else {
            status = pqe_insert(mlr->pq, pqeIndex);

            if (status) {
                log_add("Couldn't insert %u-byte data-product \"%s\"", info->sz,
                        info->ident);
                status = EIO;
            }
            else {
                downlet_incNumProds();
                lastReceived(mlr, info);

                char infoStr[LDM_INFO_MAX];
                log_info("Received: {time: %.7f s, index: %lu, retrans: %u, "
                        "info: \"%s\"}",
                        duration, (unsigned long)prodIndex, numRetrans,
                        s_prod_info(infoStr, sizeof(infoStr), info,
                                log_is_enabled_debug));

            } // Product successfully inserted into product-queue
        } // "info" initialized

        xdr_destroy(&xdrs);
    } // Not a duplicate product

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
        void* const restrict            obj,
        const FmtpProdIndex             iProd,
        const pqe_index* const restrict pqeIndex)
{
    /*
     * This function is called on both the FMTP multicast and unicast threads.
     */

    Mlr* mlr = obj;

    if (pqeIndex)
        (void)pqe_discard(mlr->pq, pqeIndex);

    downlet_missedProduct(iProd);
}

/**
 * Initializes a multicast LDM receiver.
 *
 * @param[out] mlr            The multicast LDM receiver to initialize.
 * @param[in]  mcastInfo      Pointer to information on the multicast group.
 * @param[in]  iface          IPv4 address of interface to use for receiving
 *                            multicast and unicast packets. Caller may free.
 * @param[in]  pq             Product queue. Must exist until `destroy()`
 *                            returns.
 * @retval     0              Success.
 * @retval     LDM7_SYSTEM    System error. `log_add()` called.
 * @retval     LDM7_INVAL     `pq == NULL || missed_product == NULL ||
 *                            mcastInfo == NULL || downlet == NULL`. `log_add()`
 *                            called.
 * @retval     LDM7_FMTP     FMTP error. `log_add()` called.
 */
static int
init(
        Mlr* const restrict                mlr,
        const SepMcastInfo* const restrict mcastInfo,
        const char* const restrict         iface,
        pqueue* const restrict             pq)
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

    void* notifier;
    status = ppn_new(&notifier, bop_func, eop_func, missed_prod_func, mlr);
    if (status) {
        log_add("Couldn't create per-product notifier");
        return LDM7_MCAST;
    }
    else {
        if (log_is_enabled_info) {
            char* const miStr = smi_toString(mcastInfo);
            if (miStr == NULL) {
                log_add("Couldn't format multicast information");
                ppn_free(notifier);
                return LDM7_SYSTEM;
            }
            log_info("Initializing FMTP receiver with mcastInfo=%s, iface=%s",
                    miStr, iface);
            free(miStr);
        }

        const InetSockAddr* const fmtpSrvr = smi_getFmtpSrvr(mcastInfo);
        const InetSockAddr* const mcastGroup = smi_getMcastGrp(mcastInfo);

        status = fmtpReceiver_new(&receiver,
                isa_getInetAddrStr(fmtpSrvr),
                isa_getPort(fmtpSrvr), notifier,
                isa_getInetAddrStr(mcastGroup),
                isa_getPort(mcastGroup), iface);

        if (status) {
            log_add("Couldn't create FMTP receiver");
            ppn_free(notifier);
            return LDM7_MCAST;
        }
    } // `notifier` allocated

    mlr->receiver = receiver;
    mlr->pq = pq;
    mlr->done  = 0;

    return 0;
}

static void
destroy(Mlr* const mlr)
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
 *                            multicast and unicast packets. Caller may free.
 * @param[in]  pq             Product queue. Must exist until `deinit()`
 *                            returns.
 * @retval     NULL           Failure. `log_add()` called.
 * @return                    Pointer to a new multicast LDM receiver object.
 *                            The caller should call `mlr_free()` when it's no
 *                            longer needed.
 */
Mlr*
mlr_new(const SepMcastInfo* const restrict mcastInfo,
        const char* const restrict         iface,
        pqueue* const restrict             pq)
{
    Mlr* mlr = log_malloc(sizeof(Mlr), "multicast LDM receiver object");

    if (mlr) {
        if (init(mlr, mcastInfo, iface, pq)) {
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
mlr_free(Mlr* const mlr)
{
    if (mlr) {
        destroy(mlr);
        free(mlr);
    }
}

/**
 * Executes a multicast LDM receiver. Doesn't return until `mlr_halt()` is
 * called or an error occurs.
 *
 * @param[in] mlr            The multicast LDM receiver to execute.
 * @retval    LDM7_SHUTDOWN  `mlr_stop()` was called.
 * @retval    LDM7_INVAL     `mlr == NULL`. `log_add()` called.
 * @retval    LDM7_MCAST     Multicast error. `log_add()` called.
 * @see `mlr_stop()`
 */
int
mlr_run(Mlr* const mlr)
{
    int status;

    if (NULL == mlr) {
        log_add("NULL multicast-LDM-receiver argument");
        status = LDM7_INVAL;
    }
    else {
        status = fmtpReceiver_execute(mlr->receiver);

        if (mlr->done || status == 0) {
            status = LDM7_SHUTDOWN;
        }
        else {
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
 * @param[in] mlr      Pointer to the multicast LDM receiver to stop.
 * @asyncsignalsafety  Unsafe
 */
void
mlr_halt(Mlr* const mlr)
{
    mlr->done = 1;
    fmtpReceiver_stop(mlr->receiver);
}
