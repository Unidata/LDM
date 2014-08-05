/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_receiver.c
 * @author: Steven R. Emmerson
 *
 * This file implements the downstream multicast LDM.
 */

#include "config.h"

#include "down7.h"
#include "mldm_receiver.h"
#include "ldm.h"
#include "LdmBofResponse.h"
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
 * The multicast downstream LDM data-structure:
 */
struct mdl {
    pqueue*         pq;       // product-queue to use */
    Down7*          down7;    // pointer to associated downstream LDM-7
    McastReceiver* receiver; // VCMTP C Receiver
};

/**
 * Locks the product-queue of a multicast downstream LDM.
 *
 * @param[in] mdl       Pointer to the multicast downstream LDM.
 * @retval    0         Success.
 * @retval    EAGAIN    The lock could not be acquired because the maximum
 *                      number of recursive calls has been exceeded.
 * @retval    EDEADLK   A deadlock condition was detected.
 */
static int
lockPq(
    Mdl* const mdl)
{
    int status = pq_lock(mdl->pq);

    if (status)
        LOG_ADD1("Couldn't lock product-queue: %s", strerror(status));

    return status;
}

/**
 * Unlocks the product-queue of a multicast downstream LDM.
 *
 * @param[in] mdl       Pointer to the multicast downstream LDM.
 * @retval    0         Success.
 * @retval    EPERM     The current thread does not own the lock.
 */
static int
unlockPq(
    Mdl* const mdl)
{
    int status = pq_unlock(mdl->pq);

    if (status)
        LOG_ADD1("Couldn't unlock product-queue: %s", strerror(status));

    return status;
}

/**
 * Allocates space in a product-queue for a VCMTP file if it's not a duplicate
 * and sets the beginning-of-file response in a VCMTP file-entry.
 *
 * @param[in] mdl         Pointer to the multicast downstream LDM.
 * @param[in] name        The name of the VCMTP file.
 * @param[in] size        Size of the XDR-encoded LDM data-product in bytes.
 * @param[in] signature   The MD5 checksum of the LDM data-product.
 * @param[in] file_entry  The corresponding VCMTP file-entry.
 * @retval    0           Success or the data-product is already in the LDM
 *                        product-queue. BOF-response set.
 * @retval    -1          Failure. \c log_add() called. BOF-response set to
 *                        ignore the file.
 */
static int
allocateSpaceAndSetBofResponse(
    Mdl* const restrict        mdl,
    const char* const restrict name,
    const size_t               size,
    const signaturet           signature,
    void* const                file_entry)
{
    int               status;
    char*             buf;
    pqe_index         index;

    if (lockPq(mdl)) {
        LOG_ADD1("Couldn't lock product-queue: %s", strerror(status));
        mcastFileEntry_setBofResponseToIgnore(file_entry);
        status = -1;
    }
    else {
        status = pqe_newDirect(mdl->pq, size, signature, &buf, &index);
        (void)unlockPq(mdl);

        if (status) {
            mcastFileEntry_setBofResponseToIgnore(file_entry);

            if (status == PQUEUE_DUP) {
                status = 0;
            }
            else {
                LOG_ADD2("Couldn't allocate region for %lu-byte file \"%s\"",
                        size, name);
                status = -1;
            }
        }
        else {
            (void)mcastFileEntry_setBofResponse(file_entry,
                    ldmBofResponse_new(buf, size, &index));
            status = 0;
        } /* region allocated in product-queue */
    } // product-queue locked

    return status;
}

/**
 * Sets the response attribute of a VCMTP file-entry in response to being
 * notified by the VCMTP layer about the beginning of a file. Allocates a
 * region in the LDM product-queue to receive the VCMTP file, which is an
 * XDR-encoded LDM data-product.
 *
 * @param[in,out]  obj          Pointer to the associated multicast downstream
 *                              LDM object.
 * @param[in]      file_entry   Metadata of the file in question.
 * @retval         0            Success, the transfer isn't to memory, or the
 *                              data-product is already in the LDM
 *                              product-queue.
 * @retval         -1           Failure. \c log_add() called.
 */
static int
bof_func(
    void* const    obj,
    void* const    file_entry)
{
    int            status;

    if (!mcastFileEntry_isMemoryTransfer(file_entry)) {
        mcastFileEntry_setBofResponseToIgnore(file_entry);
        status = 0;
    }
    else {
        signaturet        signature;
        const char* const name = mcastFileEntry_getFileName(file_entry);

        if (sigParse(name, &signature) < 0) {
            LOG_ADD1("Couldn't parse filename \"%s\" into data-product "
                    "signature", name);
            status = -1;
        }
        else {
            status = allocateSpaceAndSetBofResponse((Mdl*)obj, name,
                    mcastFileEntry_getSize(file_entry), signature, file_entry);
        } /* filename is data-product signature */
    } /* transfer is to memory */

    return status;
}

/**
 * Finishes inserting a data-product into the allocated product-queue region
 * associated with a multicast downstream LDM or discards the region.
 *
 * @param[in] mdl    Pointer to the multicast downstream LDM.
 * @param[in] index  Pointer to the allocated region.
 * @retval    0      Success.
 * @retval    -1     Error. `log_add()` called.
 */
static int
insertOrDiscard(
    Mdl* const restrict             mdl,
    const pqe_index* const restrict index)
{
    int status;

    lockPq(mdl);
    if ((status = pqe_insert(mdl->pq, *index)) != 0)
        (void)pqe_discard(mdl->pq, *index);
    unlockPq(mdl);

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
 * @param[in] mdl   Pointer to the multicast downstream LDM.
 * @param[in] info  Pointer to the metadata of the last data-product to be
 *                  successfully received. Caller may free when it's no longer
 *                  needed.
 */
inline static void
lastReceived(
    Mdl* const restrict             mdl,
    const prod_info* const restrict info)
{
    dl7_lastReceived(mdl->down7, info);
}

/**
 * Finishes inserting a received VCMTP file into an LDM product-queue as an LDM
 * data-product.
 *
 * @param[in] mdl          Pointer to the multicast downstream LDM.
 * @param[in] index        Index of the allocated region in the product-queue.
 * @param[in] info         LDM data-product metadata. Caller may free when it
 *                         is no longer needed.
 * @param[in] dataSize     Actual number of bytes received.
 * @retval    0            Success.
 * @retval    -1           Error. \c log_add() called. The allocated region in
 *                         the product-queue was released.
 */
static int
finishInsertion(
    Mdl* const restrict             mdl,
    const pqe_index* const restrict index,
    const prod_info* const restrict info,
    const size_t                    dataSize)
{
    int status;

    if (info->sz > dataSize) {
        LOG_ADD3("Size of LDM data-product > actual amount of data in \"%s\": "
                "LDM size=%u bytes; actual data=%lu bytes", info->ident,
                info->sz, (unsigned long)dataSize);
        status = -1;
        lockPq(mdl);
        (void)pqe_discard(mdl->pq, *index);
        unlockPq(mdl);
    }
    else {
        status = insertOrDiscard(mdl, index);

        if (status) {
            LOG_ADD("Couldn't finish inserting %u-byte data-product \"%s\"",
                    info->sz, info->ident);
        }
        else {
            lastReceived(mdl, info);
        }
    }
    return status;
}

/**
 * Accepts notification from the VCMTP layer of the complete reception of a
 * file. Finishes inserting the VCMTP file (which is an XDR-encoded
 * data-product) into the associated LDM product-queue.
 *
 * @param[in,out]  obj          Pointer to the associated multicast downstream
 *                              LDM object.
 * @param[in]      file_entry   Metadata of the VCMTP file in question.
 * @retval         0            Success, the file-transfer wasn't to memory,
 *                              or the data wasn't wanted.
 * @retval         -1           Error. \c log_add() called. The allocated space
 *                              in the LDM product-queue was released.
 */
static int
eof_func(
    void*               obj,
    const void* const   file_entry)
{
    int                 status;

    if (!mcastFileEntry_isWanted(file_entry) ||
            !mcastFileEntry_isMemoryTransfer(file_entry)) {
        status = 0;
    }
    else {
        prod_info              info;
        XDR                    xdrs;
        const size_t           fileSize = mcastFileEntry_getSize(file_entry);
        const void* const      bofResponse = mcastFileEntry_getBofResponse(file_entry);
        const pqe_index* const index = ldmBofResponse_getIndex(bofResponse);
        Mdl* const             mdl = (Mdl*)obj;
        pqueue* const          pq = mdl->pq;

        xdrmem_create(&xdrs, (char*)ldmBofResponse_getBuf(bofResponse),
                fileSize, XDR_DECODE); /* (char*) is safe because decoding */

        if (!xdr_prod_info(&xdrs, &info)) {
            LOG_SERROR2("Couldn't decode LDM product-metadata from %lu-byte "
                    "VCMTP file \"%s\"", fileSize,
                    mcastFileEntry_getFileName(file_entry));
            status = -1;
            lockPq(mdl);
            pqe_discard(pq, *index);
            unlockPq(mdl);
        }
        else {
            status = finishInsertion(mdl, index, &info,
                    fileSize-(xdrs.x_private-xdrs.x_base));
            xdr_free(xdr_prod_info, (char*)&info);
        } /* "info" allocated */
    } /* region in product-queue was allocated */

    return status;
}

/**
 * Accepts notification from the VCMTP layer of the missed reception of a
 * file. Queues the file for reception by other means. This function must and
 * does return immediately.
 *
 * @param[in,out]  obj          Pointer to the associated multicast downstream
 *                              LDM object.
 * @param[in]      fileId       Identifier of the VCMTP file that was missed.
 */
static void
missed_file_func(
    void*               obj,
    const McastFileId   fileId)
{
    dl7_missedProduct(((Mdl*)obj)->down7, fileId);
}

/**
 * Initializes a multicast downstream LDM.
 *
 * @param[out] mdl            The multicast downstream LDM to initialize.
 * @param[in]  pq             The product-queue to use.
 * @param[in]  mcastInfo      Pointer to information on the multicast group.
 * @param[in]  down7          Pointer to the associated downstream LDM-7 object.
 * @retval     0              Success.
 * @retval     LDM7_SYSTEM    System error. \c log_add() called.
 * @retval     LDM7_INVAL     @code{pq == NULL || missed_product == NULL ||
 *                            mcastInfo == NULL}. \c log_add() called.
 * @retval     LDM7_VCMTP     VCMTP error. \c log_add() called.
 */
static int
init(
    Mdl* const restrict                  mdl,
    pqueue* const restrict               pq,
    const McastInfo* const restrict mcastInfo,
    Down7* const restrict                down7)
{
    int                 status;
    McastReceiver*     receiver;

    if (mdl == NULL) {
        LOG_ADD0("NULL multicast-downstream-LDM argument");
        return LDM7_INVAL;
    }
    if (pq == NULL) {
        LOG_ADD0("NULL product-queue argument");
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
            mcastInfo->server.port, bof_func, eof_func, missed_file_func,
            mcastInfo->group.inetId, mcastInfo->group.port, mdl);
    if (status) {
        LOG_ADD0("Couldn't create FMTP receiver");
        return LDM7_MCAST;
    }

    mdl->receiver = receiver;
    mdl->pq = pq;
    mdl->down7 = down7;

    return 0;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new multicast downstream LDM object.
 *
 * @param[in]  pq             The product-queue to use.
 * @param[in]  mcastInfo      Pointer to information on the multicast group.
 * @param[in]  down7          Pointer to the associated downstream LDM-7 object.
 * @retval     NULL           Failure. `log_add()` called.
 * @return                    Pointer to a new multicast downstream LDM object.
 *                            The caller should call `ldm_free()` when it's no
 *                            longer needed.
 */
Mdl*
mdl_new(
    pqueue* const restrict               pq,
    const McastInfo* const restrict mcastInfo,
    Down7* const restrict                down7)
{
    Mdl* mdl = LOG_MALLOC(sizeof(Mdl), "multicast downstream LDM object");

    if (mdl) {
        if (init(mdl, pq, mcastInfo, down7)) {
            LOG_ADD0("Couldn't initialize multicast downstream LDM");
            free(mdl);
            mdl = NULL;
        }
    }

    return mdl;
}

/**
 * Frees the resources of a multicast downstream LDM object.
 *
 * @param[in,out] mdl   The multicast downstream LDM object.
 */
void
mdl_free(
    Mdl* const  mdl)
{
    mcastReceiver_free(mdl->receiver);
    free(mdl);
}

/**
 * Executes a multicast downstream LDM. Blocks until the multicast
 * downstream LDM is stopped.
 *
 * @param[in] mdl            The multicast downstream LDM to execute.
 * @retval    LDM7_CANCELED  The multicast downstream LDM was stopped.
 * @retval    LDM7_INVAL     @code{mdl == NULL}. \c log_add() called.
 * @retval    LDM7_VCMTP     VCMTP error. \c log_add() called.
 */
int
mdl_start(
    Mdl* const  mdl)
{
    int         status;

    if (NULL == mdl) {
        LOG_ADD0("NULL multicast-downstream-LDM argument");
        status = LDM7_INVAL;
    }
    else if ((status = mcastReceiver_execute(mdl->receiver)) != 0) {
        LOG_ADD0("Failure executing multicast downstream LDM");
        status = LDM7_MCAST;
    }
    else {
        status = LDM7_SHUTDOWN;
    }

    return status;
}

/**
 * Cleanly stops an executing multicast downstream LDM. Undefined behavior
 * results if called from a signal handler.
 *
 * @param[in] mdl  Pointer to the multicast downstream LDM to stop.
 */
void
mdl_stop(
    Mdl* const mdl)
{
    mcastReceiver_stop(mdl->receiver);
}
