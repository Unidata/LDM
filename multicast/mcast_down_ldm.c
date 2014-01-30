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
#include <stddef.h>
#include <stdlib.h>

/*
 * The multicast downstream LDM data-structure:
 */
struct mdl_struct {
    pqueue*             pq;             /* product-queue to use */
    mdl_missed_product  missed_product; /* missed-product callback function */
    vcmtp_receiver*     receiver;       /* the VCMTP receiver */
};

/**
 * Initializes a multicast downstream LDM.
 *
 * @param mdl                   The multicast downstream LDM to initialize.
 * @param pq                    The product-queue to use.
 * @param missed_product        Missed-product callback function.
 * @retval 0                    Success.
 * @retval EINVAL               @code{pq == NULL || missed_product == NULL}.
 *                              \c log_add() called.
 */
static int init(
    mdl_obj* const              mdl,
    pqueue* const               pq,
    const mdl_missed_product    missed_product)
{
    if (pq == NULL || missed_product == NULL) {
        LOG_ADD2("NULL argument: pq=%p, missed_product=%p", pq, missed_product);
        return EINVAL;
    }

    /* VcmtpReceiverParams */

    mdl->pq = pq;
    mdl->missed_product = missed_product;
    mdl->receiver = vcmtp_receiver_new();

    return 0;
}

/**
 * Returns a new multicast downstream LDM object.
 *
 * @param mdl                   The pointer to be set.
 * @param pq                    The product-queue to use.
 * @param missed_product        Missed-product callback function.
 * @retval 0                    Success.
 * @retval ENOMEM               Out of memory. \c log_add() called.
 * @retval EINVAL               @code{pq == NULL || missed_product == NULL}.
 *                              \c log_add() called.
 */
static int mdl_new(
    mdl_obj** const             mdl,
    pqueue* const               pq,
    const mdl_missed_product    missed_product)
{
    int                 status;
    mdl_obj* const      obj = LOG_MALLOC(sizeof(mdl_obj),
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
 * @param mdl           The multicast downstream LDM to execute.
 * @retval 0            Success. The multicast downstream LDM terminated
 *                      successfully.
 */
static int execute(
    mdl_obj* const      mdl)
{
    return 0;
}

/**
 * Creates and executes a multicast downstream LDM for an indefinite amount of
 * time. Will not return until the multicast downstream LDM terminates.
 *
 * @param pq                    The product-queue to use.
 * @param missed_product        Missed-product callback function.
 * @retval 0                    The multicast downstream LDM terminated
 *                              successfully.
 * @retval ENOMEM               Out of memory. \c log_add() called.
 * @retval EINVAL               @code{pq == NULL}. \c log_add() called.
 */
int mdl_create_and_execute(
    pqueue* const       pq,
    mdl_missed_product  missed_product)
{
    mdl_obj*    mdl;
    int         status = mdl_new(&mdl, pq, missed_product);

    if (0 == status) {
        status = execute(mdl);
        free(mdl);
    }

    return status;
}
