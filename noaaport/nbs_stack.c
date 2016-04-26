/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_stack.c
 * @author: Steven R. Emmerson
 *
 * This file implements a protocol stack for the NOAAPort Broadcast System
 * (NBS).
 */

#include "config.h"

#include "log.h"
#include "nbs_stack.h"
#include "nbs_presentation.h"
#include "nbs_transport.h"

struct nbss {
    nbsa_t* nbsa; ///< Application-layer
    nbsp_t* nbsp; ///< Presentation-layer
    nbst_t* nbst; ///< Transport-layer
    nbsl_t* nbsl; ///< Link-layer
};

/**
 * Initializes an NBS stack for receiving NBS products.
 *
 * @param[in,out] stack      NBS stack
 * @param[in]     nbsa       NBS application-layer
 * @param[in,out] nbsl       NBS link-layer
 * @retval 0                 Success. `*stack` is initialized.
 * @retval NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbss_recv_init(
        nbss_t* const restrict stack,
        nbsa_t* const restrict nbsa,
        nbsl_t* const restrict nbsl)
{
    log_assert(stack);
    log_assert(nbsa);
    log_assert(nbsl);
    nbsp_t* nbsp;
    int     status = nbsp_new(&nbsp);
    if (status) {
        log_add("Couldn't create NBS presentation-layer");
    }
    else {
        status = nbsp_set_application_layer(nbsp, nbsa);
        if (status) {
            log_add("Couldn't couple NBS presentation-layer to NBS "
                    "application-layer");
        }
        else {
            nbst_t* nbst;
            status = nbst_new(&nbst);
            if (status) {
                log_add("Couldn't create NBS transport-layer");
            }
            else {
                status = nbst_set_presentation_layer(nbst, nbsp);
                if (status) {
                    log_add("Couldn't couple NBS transport-layer to NBS "
                            "presentation-layer");
                }
                else {
                    status = nbsl_set_transport_layer(nbsl, nbst);
                    if (status) {
                        log_add("Couldn't couple NBS link-layer to NBS "
                                "transport-layer");
                    }
                    else {
                        stack->nbsa = nbsa;
                        stack->nbsp = nbsp;
                        stack->nbst = nbst;
                        stack->nbsl = nbsl;
                    }
                } // `nbsp` set in `nbst`
                if (status)
                    nbst_free(nbst);
            } // `nbst` created
        } // `nbsa` set in `nbsp`
        if (status)
            nbsp_free(nbsp);
    } // `nbsp` created
    return status;
}

/**
 * Finalizes an NBS stack.
 *
 * @param[in,out] stack      NBS stack
 */
void nbss_fini(
        nbss_t* const restrict       stack)
{
    log_assert(stack);
    nbst_free(stack->nbst);
    nbsp_free(stack->nbsp);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

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
        nbsl_t* const restrict           nbsl)
{
    int status;
    if (stack == NULL || nbsa == NULL || nbsl == NULL) {
        log_add("NULL argument: stack=%p, nbsa=%p, nbsl=%p", stack, nbsa, nbsl);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbss_t* nbss = log_malloc(sizeof(nbss_t), "NBS protocol stack");
        if (nbss == NULL) {
            status = NBS_STATUS_NOMEM;
        }
        else {
            status = nbss_recv_init(nbss, nbsa, nbsl);
            if (status) {
                log_add("Couldn't initialize receiving NBS protocol stack");
            }
            else {
                *stack = nbss;
            }
            if (status)
                free(nbss);
        } // `nbss` allocated
    } // Valid arguments
    return status;
}

/**
 * Frees an NBS stack. Doesn't free the link-layer or application-layer.
 * 
 * @param[in,out] stack  NBS stack
 */
void nbss_free(
        nbss_t* const stack)
{
    if (stack) {
        nbss_fini(stack);
        free(stack);
    }
}

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
        nbss_t* const stack)
{
    int status = nbsl_execute(stack->nbsl);
    if (status == NBS_STATUS_INVAL) {
        log_add("NBS stack not configured for reception");
        status = NBS_STATUS_LOGIC;
    }
    return status;
}
