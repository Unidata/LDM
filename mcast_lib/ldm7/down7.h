/* DO NOT EDIT THIS FILE. It was created by extractDecls */
/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down7.hin
 * @author: Steven R. Emmerson
 *
 * This file specifies the API for the multicast-capable downstream LDM.
 */

#ifndef DOWN7_H
#define DOWN7_H

#include "fmtp.h"
#include "InetSockAddr.h"
#include "ldm.h"
#include "MldmRcvrMemory.h"
#include "pq.h"

#include <inetutil.h>

typedef struct down7   Down7;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Increments the number of data-products successfully inserted into the
 * product-queue. Called by the multicast LDM receiver.
 */
void
downlet_incNumProds();

/**
 * Queues a data-product for being requested by the LDM7 backstop mechanism.
 * This function is called by the multicast LDM receiver; therefore, it must
 * return immediately so that the multicast LDM receiver can continue.
 *
 * @param[in] iProd    Index of the missed FMTP product.
 */
void
downlet_missedProduct(const FmtpProdIndex iProd);

/**
 * Tracks the last data-product to be successfully received by the multicast
 * LDM receiver. This function is called by the multicast LDM receiver;
 * therefore, it must return immediately so that the multicast LDM receiver can
 * continue.
 *
 * The first time this function is called for a newly-initialized, one-time,
 * downstream LDM7, it starts a subtask that requests the backlog of
 * data-products that were missed due to the passage of time from the end of the
 * previous session to the reception of the first multicast data-product.
 *
 * @param[in] last     Pointer to the metadata of the last data-product to be
 *                     successfully received by the associated multicast
 *                     LDM receiver. Caller may free when it's no longer needed.
 */
void
downlet_lastReceived(const prod_info* const last);

/**
 * Initializes this module.
 *
 * @param[in] ldmSrvr     Internet socket address of the LDM7 server
 * @param[in] feed        LDM feed
 * @param[in] fmtpIface   Virtual interface to be created for the FMTP
 *                        component to use or "dummy", indicating that no
 *                        virtual interface should be created, in which case
 *                        `vcEnd` must be invalid. Caller may free.
 * @param[in] vcEnd       Local virtual-circuit endpoint of AL2S VLAN. It must
 *                        be valid only if communication with the FMTP server
 *                        will be over an AL2S VLAN. Caller may free.
 * @param[in] pq          Product-queue for received data-products
 * @param[in] mrm         Persistent multicast receiver memory
 * @retval    0           Success
 * @retval    LDM7_INVAL  `fmtpIface` is inconsistent with `vcEnd`. `log_add()`
 *                        called.
 */
Ldm7Status
down7_init(
        InetSockAddr* const restrict        ldmSrvr,
        const feedtypet                     feed,
        const char* const restrict          fmtpIface,
        const VcEndPoint* const restrict    vcEnd,
        pqueue* const restrict              pq,
        McastReceiverMemory* const restrict mrm);

/**
 * Destroys the downstream LDM7 module.
 *
 * @asyncsignalsafety  Unsafe
 */
void
down7_destroy(void);

/**
 * Executes a downstream LDM7. Doesn't return unless a severe error occurs or
 * `down7_halt()` is called.
 *
 * @param[in,out] arg          Downstream LDM7
 * @retval        0            `down7_halt()` called
 * @retval        LDM7_INTR    Interrupted by signal
 * @retval        LDM7_INVAL   Invalid port number or host identifier.
 *                             `log_add()` called.
 * @retval        LDM7_LOGIC   Logic error. `log_add()` called.
 * @retval        LDM7_RPC     RPC error. `log_add()` called.
 * @retval        LDM7_SYSTEM  System error. `log_add()` called.
 * @see `down7_halt()`
 */
Ldm7Status
down7_run();

/**
 * Indicates if the downstream LDM module has been initialized.
 *
 * @retval `false`  No
 * @retval `true`   Yes
 */
bool
down7_isInit();

/**
 * Stops the downstream LDM7 module from running. May be called from a signal
 * handler. Idempotent.
 *
 * @threadsafety       Safe
 * @asyncsignalsafety  Safe
 */
void
down7_halt();

/**
 * Queues a request for a product.
 *
 * @param[in] iProd  Index of product to be requested
 */
void
down7_requestProduct(const FmtpProdIndex iProd);

/**
 * Increments the number of data-products successfully inserted into the
 * product-queue by the downstream LDM7. Called by the one-time downstream LDM7
 */
void
down7_incNumProds(void);

/**
 * Returns the number of data-products successfully inserted into the
 * product-queue by a downstream LDM7 and resets the counter to zero. Used for
 * testing.
 *
 * @return  Number of products
 */
uint64_t
down7_getNumProds(void);

/**
 * Returns the number of slots reserved in the product-queue for
 * not-yet-received data-products.
 *
 * @return  Number of reserved slots
 */
long
down7_getPqeCount(void);

/**
 * Processes a missed data-product from a remote LDM7 by attempting to add the
 * data-product to the product-queue. The data-product should have been
 * previously requested from the remote LDM7 because it was missed by the
 * multicast LDM receiver. Destroys the server-side RPC transport if the
 * data-product can't be inserted into the product-queue. Does not reply. Called
 * by the RPC dispatcher `ldmprog_7()`.
 *
 * @param[in] missedProd  Pointer to the missed data-product.
 * @param[in] rqstp       Pointer to the RPC service-request.
 * @retval    NULL        Always.
 */
void*
deliver_missed_product_7_svc(
    MissedProduct* const restrict  missedProd,
    struct svc_req* const restrict rqstp);

/**
 * Asynchronously accepts notification from the upstream LDM7 that a requested
 * data-product doesn't exist. Called by the RPC dispatch routine `ldmprog_7()`.
 *
 * @param[in] missingProd  Index of product
 * @param[in] rqstp        Pointer to the RPC service-request.
 */
void*
no_such_product_7_svc(
    FmtpProdIndex* const restrict  missingProd,
    struct svc_req* const restrict rqstp);

/**
 * Asynchronously processes a backlog data-product from a remote LDM7 by
 * attempting to add the data-product to the product-queue. The data-product
 * should have been previously requested from the remote LDM7 because it was
 * missed during the previous session. Destroys the server-side RPC transport if
 * the data-product can't be inserted into the product-queue. Does not reply.
 * Called by the RPC dispatcher `ldmprog_7()`.
 *
 * @param[in] prod        Pointer to the backlog data-product.
 * @param[in] rqstp       Pointer to the RPC service-request.
 * @retval    NULL        Always.
 */
void*
deliver_backlog_product_7_svc(
    product* const restrict        prod,
    struct svc_req* const restrict rqstp);

/**
 * Asynchronously accepts notification that the downstream LDM7 associated with
 * the current thread has received all backlog data-products from its upstream
 * LDM7. From now on, the current process may be terminated for a time period
 * that is less than the minimum residence time of the upstream LDM7's
 * product-queue without loss of data. Called by the RPC dispatcher
 * `ldmprog_7()`.
 *
 * @param[in] rqstp  Pointer to the RPC server-request.
 */
void*
end_backlog_7_svc(
    void* restrict                 noArg,
    struct svc_req* const restrict rqstp);

#ifdef __cplusplus
}
#endif

#endif
