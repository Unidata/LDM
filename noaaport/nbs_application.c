/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_application.c
 * @author: Steven R. Emmerson
 *
 * This file implements the NOAAPort Broadcast System (NBS) application-layer.
 *
 * This particular module adds NBS products to an LDM product-queue.
 */

#include "config.h"

#include "ldm.h"
#include "log.h"
#include "nbs_application.h"
#include "pq.h"

/******************************************************************************
 * NBS Application-Layer Object:
 ******************************************************************************/

struct nbsa {
    pqueue* pq;   ///< LDM product-queue
    product prod; ///< LDM data-product
};

static void nbsa_init(
        nbsa_t* const restrict nbsa,
        pqueue* const restrict pq)
{
    log_assert(nbsa);
    log_assert(pq);
    nbsa->pq = pq;
    nbsa->prod.data = NULL;
}

/**
 * Returns a new NBS application-layer object.
 *
 * @param[out] nbsa               NBS application-layer object
 * @param[in]  pq                 LDM product-queue
 * @retval     0                  Success. `*nbsa` is set.
 * @retval     NBSA_STATUS_INVAL  `nbsa == NULL || pq == NULL`. log_add()
 *                                called.
 * @retval     NBSA_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbsa_status_t nbsa_new(
        nbsa_t** const restrict nbsa,
        pqueue* const restrict  pq)
{
    int status;
    if (pq == NULL) {
        log_add("NULL argument: nbsa=%p, pq=%p", nbsa, pq);
        status = NBSA_STATUS_INVAL;
    }
    else {
        nbsa_t* obj = log_malloc(sizeof(nbsa_t),
                "NBS application-layer object");
        if (obj == NULL) {
            status = NBSA_STATUS_NOMEM;
        }
        else {
            nbsa_init(obj, pq);
            *nbsa = obj;
            status = 0;
        }
    }
    return status;
}

/**
 * Frees an NBS application-layer object.
 *
 * @param[in] nbsa  NBS application-layer object or `NULL`
 */
void nbsa_free(
        nbsa_t* const nbsa)
{
    free(nbsa);
}
