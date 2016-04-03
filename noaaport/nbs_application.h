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

#include "gini.h"
#include "nbs_status.h"
#include "pq.h"

typedef struct nbsa nbsa_t;

#ifdef __cplusplus
    extern "C" {
#endif

nbs_status_t nbsa_new(
        nbsa_t** const restrict nbsa,
        pqueue* const restrict  pq);

nbs_status_t nbsa_process_gini(
        nbsa_t* const restrict       nbsa,
        const gini_t* const restrict gini);

void nbsa_free(
        nbsa_t* const nbsa);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_APPLICATION_H_ */
