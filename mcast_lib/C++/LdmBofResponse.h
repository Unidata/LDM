/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file LdmBofResponse.h
 *
 * This file declares the API for the response by the LDM to a beginning-of-file
 * notice from the FMTP layer.
 *
 * @author: Steven R. Emmerson
 */

#ifndef LDM_BOF_RESPONSE_H
#define LDM_BOF_RESPONSE_H

#include "pq.h"

#ifdef __cplusplus
extern "C" {
#endif

void*
ldmBofResponse_new(
    char* const            buf,
    const size_t           size,
    const pqe_index* const index);

const pqe_index*
ldmBofResponse_getIndex(
    const void* ldmBofResponse);

const char* ldmBofResponse_getBuf(
    const void* ldmBofResponse);

#ifdef __cplusplus
}
#endif

#endif
