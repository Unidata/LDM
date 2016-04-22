/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_application.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the NOAAPort Broadcast System (NBS)
 * application-layer.
 *
 * NOTE: Because the NBS doesn't define the application-layer (see
 * <http://www.nws.noaa.gov/noaaport/html/n_format.shtml>), this module can do
 * anything it wants.
 */

#ifndef NOAAPORT_NBS_APPLICATION_H_
#define NOAAPORT_NBS_APPLICATION_H_

typedef struct nbsa nbsa_t;

#include "gini.h"
#include "nbs.h"
#include "nbs_presentation.h"
#include "pq.h"

#ifdef __cplusplus
    extern "C" {
#endif

nbs_status_t nbsa_new(
        nbsa_t** const restrict nbsa);

/**
 * Sets the product-queue for upward processing of data-products.
 *
 * @param[out] nbsa              NBS application-layer object
 * @param[in]  pq                LDM product-queue
 * @retval     0                 Success
 * @retval     NBS_STATUS_INVAL  `nbsa == NULL || pq == NULL`. log_add()
 *                               called.
 */
nbs_status_t nbsa_set_pq(
        nbsa_t* const restrict nbsa,
        pqueue* const restrict pq);

/**
 * Sets the NBS presentation-layer object of an NBS application-layer object for
 * downward processing of data-products.
 *
 * @param[out] nbsa              NBS application-layer object
 * @param[in]  nbsp              NBS presentation-layer object
 * @retval     0                 Success
 * @retval     NBS_STATUS_INVAL  `nbsa == NULL || nbsp == NULL`. log_add()
 *                               called.
 */
nbs_status_t nbsa_set_presentation_layer(
        nbsa_t* const restrict nbsa,
        nbsp_t* const restrict nbsp);

nbs_status_t nbsa_recv_gini(
        nbsa_t* const restrict       nbsa,
        const gini_t* const restrict gini);

void nbsa_free(
        nbsa_t* const nbsa);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_APPLICATION_H_ */
