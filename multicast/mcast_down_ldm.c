/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mcast_down_ldm.c
 * @author: Steven R. Emmerson
 *
 * This file implements the downstream multicast LDM.
 */

#include "config.h"

#include "mcast_down_ldm.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "pq.h"
#include "vcmtp_c_api.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

/**
 * The multicast downstream LDM data-structure:
 */
struct mdl {
    pqueue*                     pq;             /* product-queue to use */
    unsigned char*              buf;            /* file-receiving buffer */
    mdl_missed_product_func     missed_product; /* missed-product callback function */
    VcmtpCReceiver*             receiver;       /* VCMTP C Receiver */
};

/**
 * Sets the response attribute of a file-entry in response to being notified by
 * the VCMTP layer about the beginning of a file
 *
 * @param[in,out]  obj          Pointer to the associated multicast downstream
 *                              LDM object.
 * @param[in]      file_entry   Metadata of the file in question.
 * @retval         0            Success.
 * @retval         ENOMEM       Out-of-memory. \c log_add() called.
 */
static int bof_func(
    void* const obj,
    void* const file_entry)
{
    if (!vcmtpFileEntry_isMemoryTransfer(file_entry)) {
        vcmtpFileEntry_setBofResponseToIgnore(file_entry);
        return 0;
    }

    Mdl* const      mdl = (Mdl*)obj;
    size_t          size = vcmtpFileEntry_getSize(file_entry);
    unsigned char*  buf = realloc(mdl->buf, size);

    if (vcmtpFileEntry_setMemoryBofResponse(file_entry, buf, size))
        return ENOMEM;

    mdl->buf = buf;

    return 0;
}

/**
 * Accepts notification from the VCMTP layer of the complete reception of a
 * file.
 *
 * @param[in,out]  obj          Pointer to the associated multicast downstream
 *                              LDM object.
 * @param[in]      file_entry   Metadata of the file in question.
 * @retval         0            Success.
 */
static int eof_func(
    void*               obj,
    const void* const   file_entry)
{
    Mdl* const  mdl = (Mdl*)obj;

    return 0;
}

/**
 * Initializes an LDM signature from a formatted string.
 *
 * @param[
 * @param metadata
 */

/**
 * Accepts notification from the VCMTP layer of the missed reception of a
 * file.
 *
 * @param[in,out]  obj          Pointer to the associated multicast downstream
 *                              LDM object.
 * @param[in]      metadata     Metadata of the file in question.
 */
static void missed_file_func(
    void*               obj,
    const void* const   file_entry)
{
    signaturet  signature;
    const char* name = vcmtpFileEntry_getName(file_entry);

    if (sigParse(name, &signature) == -1) {
        LOG_ADD1("Filename is not an LDM signature: \"%s\"", name);
    }
    else {
        ((Mdl*)obj)->missed_product(obj, &signature);
    }
}

/**
 * Initializes a multicast downstream LDM.
 *
 * @param[out] mdl            The multicast downstream LDM to initialize.
 * @param[in]  pq             The product-queue to use.
 * @param[in]  missed_product Missed-product callback function.
 * @param[in]  addr           Address of the multicast group.
 *                              224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                          purposes
 *                              224.0.1.0 - 238.255.255.255 User-defined
 *                                                          multicast addresses
 *                              239.0.0.0 - 239.255.255.255 Reserved for
 *                                                          administrative
 *                                                          scoping
 * @param[in] port            Port number of the multicast group.
 * @retval    0               Success.
 * @retval    EINVAL          if @code{!mdl || !pq || !missed_product}.
 *                            \c log_add() called.
 * @retval    ENOMEM          Out of memory. \c log_add() called.
 * @retval    -1              Other failure. \c log_add() called.
 */
static int init(
    Mdl* const                          mdl,
    pqueue* const                       pq,
    const mdl_missed_product_func       missed_product,
    const char* const                   addr,
    const unsigned short                port)
{
    int                 status;
    VcmtpCReceiver*     receiver;

    if (mdl == NULL) {
        LOG_ADD0("NULL multicast-downstream-LDM argument");
        return EINVAL;
    }
    if (pq == NULL) {
        LOG_ADD0("NULL product-queue argument");
        return EINVAL;
    }
    if (missed_product == NULL) {
        LOG_ADD0("NULL missed-product-function argument");
        return EINVAL;
    }

    status = vcmtpReceiver_new(&receiver, bof_func, eof_func, missed_file_func,
            addr, port, mdl);
    if (status) {
        LOG_ADD0("Couldn't create VCMTP receiver");
        return status;
    }

    mdl->receiver = receiver;
    mdl->pq = pq;
    mdl->missed_product = missed_product;
    mdl->buf = NULL;

    return 0;
}

/**
 * Returns a new multicast downstream LDM object.
 *
 * @param[out] mdl            The pointer to be set to a new instance.
 * @param[in]  pq             The product-queue to use.
 * @param[in]  missed_product Missed-product callback function.
 * @param[in]  addr           Address of the multicast group.
 *                              224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                          purposes
 *                              224.0.1.0 - 238.255.255.255 User-defined
 *                                                          multicast addresses
 *                              239.0.0.0 - 239.255.255.255 Reserved for
 *                                                          administrative
 *                                                          scoping
 * @param[in]  port           Port number of the multicast group.
 * @retval     0              Success.
 * @retval     ENOMEM         Out of memory. \c log_add() called.
 * @retval     EINVAL         @code{pq == NULL || missed_product == NULL ||
 *                            addr == NULL}.
 *                            \c log_add() called.
 */
static int mdl_new(
    Mdl** const                         mdl,
    pqueue* const                       pq,
    const mdl_missed_product_func       missed_product,
    const char* const                   addr,
    const unsigned short                port)
{
    int             status;
    Mdl* const      obj = LOG_MALLOC(sizeof(Mdl),
            "multicast downstream LDM object");

    if (NULL == obj) {
        status = ENOMEM;
    }
    else {
        if (status = init(obj, pq, missed_product, addr, port)) {
            free(obj);
        }
        else {
            *mdl = obj;
        }
    }

    return status;
}

/**
 * Frees the resources of a multicast downstream LDM object.
 *
 * @param[in,out] mdl   The multicast downstream LDM object.
 */
static void mdl_free(
    Mdl* const  mdl)
{
    vcmtpReceiver_free(mdl->receiver);
    free(mdl);
}

/**
 * Executes a multicast downstream LDM. Doesn't return until the multicast
 * downstream LDM terminates.
 *
 * @param[in,out] mdl   The multicast downstream LDM to execute.
 * @retval 0            Success. The multicast downstream LDM terminated.
 * @retval EINVAL       @code{mdl == NULL}. \c log_add() called.
 * @retval -1           Failure. \c log_add() called.
 */
static int execute(
    Mdl* const  mdl)
{
    if (NULL == mdl) {
        LOG_ADD0("NULL multicast-downstream-LDM argument");
        return EINVAL;
    }

    return vcmtpReceiver_execute(mdl->receiver);
}

/**
 * Creates and executes a multicast downstream LDM for an indefinite amount of
 * time. Will not return until the multicast downstream LDM terminates.
 *
 * @param[in] pq             The product-queue to use.
 * @param[in] missed_product Missed-product callback function.
 * @param[in] addr           Address of the multicast group.
 *                              224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                          purposes
 *                              224.0.1.0 - 238.255.255.255 User-defined
 *                                                          multicast addresses
 *                              239.0.0.0 - 239.255.255.255 Reserved for
 *                                                          administrative
 *                                                          scoping
 * @param[in] port           Port number of the multicast group.
 * @retval 0                 The multicast downstream LDM terminated
 *                           successfully.
 * @retval ENOMEM            Out of memory. \c log_add() called.
 * @retval EINVAL            @code{pq == NULL || missed_product == NULL ||
 *                           addr == NULL}. \c log_add() called.
 * @retval -1                Failure. \c log_add() called.
 */
int mdl_createAndExecute(
    pqueue* const               pq,
    mdl_missed_product_func     missed_product,
    const char* const           addr,
    const unsigned short        port)
{
    Mdl*        mdl;
    int         status = mdl_new(&mdl, pq, missed_product, addr, port);

    if (0 == status) {
        status = execute(mdl);
        mdl_free(mdl);
    }

    return status;
}
