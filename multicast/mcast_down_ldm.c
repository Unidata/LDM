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
    mdl_missed_product_func     missed_product; /* missed-product callback function */
    vcmtp_receiver*             receiver;       /* VCMTP receiver */
};

/**
 * Accepts notification of the beginning of a file from the VCMTP layer and
 * indicates if the file should be received
 *
 * @param[in,out]  extra_arg    Pointer to the associated multicast downstream
 *                              LDM object.
 * @param[in]      metadata     Metadata of the file in question.
 * @retval         false        if and only if the file should not be received.
 */
static bool bof_func(
    void*                       extra_arg,
    const file_metadata*        metadata)
{
    Mdl* const  mdl = (Mdl*)extra_arg;

    return false;
}

/**
 * Accepts notification from the VCMTP layer of the complete reception of a
 * file.
 *
 * @param[in,out]  extra_arg    Pointer to the associated multicast downstream
 *                              LDM object.
 * @param[in]      metadata     Metadata of the file in question.
 */
static void eof_func(
    void*                       extra_arg,
    const file_metadata*        metadata)
{
    Mdl* const  mdl = (Mdl*)extra_arg;
}

/**
 * Accepts notification from the VCMTP layer of the missed reception of a
 * file.
 *
 * @param[in,out]  extra_arg    Pointer to the associated multicast downstream
 *                              LDM object.
 * @param[in]      metadata     Metadata of the file in question.
 */
static void missed_file_func(
    void*                       extra_arg,
    const file_metadata*        metadata)
{
    Mdl* const  mdl = (Mdl*)extra_arg;
}

/**
 * Initializes a multicast downstream LDM.
 *
 * @param[out] mdl              The multicast downstream LDM to initialize.
 * @param[in]  pq               The product-queue to use.
 * @param[in]  missed_product   Missed-product callback function.
 * @retval     0                Success.
 * @retval     EINVAL           @code{pq == NULL || missed_product == NULL}.
 *                              \c log_add() called.
 */
static int init(
    Mdl* const                          mdl,
    pqueue* const                       pq,
    const mdl_missed_product_func       missed_product)
{
    if (pq == NULL || missed_product == NULL) {
        LOG_ADD2("NULL argument: pq=%p, missed_product=%p", pq, missed_product);
        return EINVAL;
    }

    mdl->pq = pq;
    mdl->missed_product = missed_product;
    mdl->receiver =
            vcmtp_receiver_new(bof_func, eof_func, missed_file_func, mdl);

    return 0;
}

/**
 * Returns a new multicast downstream LDM object.
 *
 * @param[out]  mdl             The pointer to be set to a new instance.
 * @param[in]   pq              The product-queue to use.
 * @param[in]   missed_product  Missed-product callback function.
 * @retval      0               Success.
 * @retval      ENOMEM          Out of memory. \c log_add() called.
 * @retval      EINVAL          @code{pq == NULL || missed_product == NULL}.
 *                              \c log_add() called.
 */
static int mdl_new(
    Mdl** const                         mdl,
    pqueue* const                       pq,
    const mdl_missed_product_func       missed_product)
{
    int             status;
    Mdl* const      obj = LOG_MALLOC(sizeof(Mdl),
            "multicast downstream LDM object");

    if (NULL == obj) {
        status = ENOMEM;
    }
    else {
        if (status = init(obj, pq, missed_product)) {
            free(obj);
        }
        else {
            *mdl = obj;
        }
    }

    return status;
}

/**
 * Executes a multicast downstream LDM. Doesn't return until the multicast
 * downstream LDM terminates.
 *
 * @param[in,out] mdl   The multicast downstream LDM to execute.
 * @retval 0            Success. The multicast downstream LDM terminated
 *                      successfully.
 */
static int execute(
    Mdl* const  mdl)
{
    return 0;
}

/**
 * Creates and executes a multicast downstream LDM for an indefinite amount of
 * time. Will not return until the multicast downstream LDM terminates.
 *
 * @param[in] pq                The product-queue to use.
 * @param[in] missed_product    Missed-product callback function.
 * @retval 0                    The multicast downstream LDM terminated
 *                              successfully.
 * @retval ENOMEM               Out of memory. \c log_add() called.
 * @retval EINVAL               @code{pq == NULL || missed_product == NULL}.
 *                              \c log_add() called.
 */
int mdl_create_and_execute(
    pqueue* const               pq,
    mdl_missed_product_func     missed_product)
{
    Mdl*    mdl;
    int         status = mdl_new(&mdl, pq, missed_product);

    if (0 == status) {
        status = execute(mdl);
        free(mdl);
    }

    return status;
}
