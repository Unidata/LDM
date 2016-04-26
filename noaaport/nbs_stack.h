/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_stack.h
 * @author: Steven R. Emmerson
 *
 * This file implements ...
 */

#ifndef NOAAPORT_NBS_STACK_H_
#define NOAAPORT_NBS_STACK_H_

#include "nbs.h"
#include "nbs_application.h"
#include "nbs_link.h"

typedef struct nbss nbss_t;

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Returns a new NBS stack for receiving NBS products.
 *
 * @param[out]    stack      NBS stack
 * @param[in]     nbsa       NBS application-layer
 * @param[in,out] nbsl       NBS link-layer
 * @retval 0                 Success
 * @retval NBS_STATUS_INVAL  `stack == NULL || nbsa == NULL || nbsl == NULL`.
 *                           log_add() called.
 * @retval NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbss_recv_new(
        nbss_t* restrict* const restrict stack,
        nbsa_t* const restrict           nbsa,
        nbsl_t* const restrict           nbsl);

/**
 * Frees an NBS stack. Doesn't free the link-layer or application-layer.
 *
 * @param[in,out] stack  NBS stack
 */
void nbss_free(
        nbss_t* const stack);

/**
 * Receives NBS packets and processes them through an NBS protocol stack.
 * Doesn't return unless the input is shut down or an unrecoverable error
 * occurs.
 *
 * @retval 0                   Input was shut down
 * @retval NBS_STATUS_LOGIC    Logic error. log_add() called.
 * @retval NBS_STATUS_SYSTEM   System failure. log_add() called.
 */
nbs_status_t nbss_receive(
        nbss_t* const stack);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_STACK_H_ */
