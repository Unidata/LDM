/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_transport.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the transport layer of the NOAAPort Broadcast
 * System (NBS).
 */

#ifndef NOAAPORT_NBS_TRANSPORT_H_
#define NOAAPORT_NBS_TRANSPORT_H_

#include "nbs_presentation.h"
#include "nbs_status.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct nbst nbst_t;

#ifdef __cplusplus
    extern "C" {
#endif

nbs_status_t nbst_execute(
        fq_t* const restrict   fq,
        nbsp_t* const restrict nbsp);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_TRANSPORT_H_ */
