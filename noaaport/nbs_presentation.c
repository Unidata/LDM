/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_presentation.c
 * @author: Steven R. Emmerson
 *
 * This file implements the NOAAPort Broadcast System (NBS) presentation layer.
 */

#include "config.h"

#include "decode.h"
#include "dynabuf.h"
#include "gini.h"
#include "log.h"
#include "nbs_presentation.h"
#include "nport.h"

#include <stdint.h>

/******************************************************************************
 * NBS Presentation-Layer:
 ******************************************************************************/

struct nbsp {
    gini_t*    gini;     ///< GINI image
    dynabuf_t* dynabuf;  ///< Dynamic buffer for accumulating product
    nbsa_t*    nbsa;     ///< NBS application-layer object
    enum {
        NBSP_TYPE_NONE = 0,
        NBSP_TYPE_GINI
    } type;             ///< Type of product-in-progress
};

/**
 *
 * @param nbsp
 * @retval 0                 Ready
 * @retval NBS_STATUS_LOGIC  Logic error. log_add() called.
 */
static nbs_status_t nbsp_is_ready_for_start(
        nbsp_t* const nbsp)
{
    int status = (nbsp->type == NBSP_TYPE_NONE) ? 0 : NBS_STATUS_LOGIC;
    if (status)
        log_add("nbsp_end_product() not called");
    return status;
}

/**
 *
 * @param nbsp
 * @param nbsa
 * @retval NBS_STATUS_INVAL  `nbsa == NULL || nbsa == NULL`. log_add() called.
 * @retval NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbsp_new(
        nbsp_t* restrict* const restrict nbsp,
        nbsa_t* const restrict           nbsa)
{
    int status;
    if (nbsp == NULL || nbsa == NULL) {
        log_add("NULL argument: nbsp=%p, nbsa=%p", nbsp, nbsa);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsp_t* const ptr = log_malloc(sizeof(nbsp),
                "NBS presentation-layer object");
        if (ptr == NULL) {
            status = NBS_STATUS_NOMEM;
        }
        else {
            status = dynabuf_new(&ptr->dynabuf, 10000);
            if (status) {
                log_add("Couldn't create dynamic buffer");
                status = NBS_STATUS_NOMEM;
            }
            else {
                status = gini_new(&ptr->gini, ptr->dynabuf);
                if (status) {
                    log_add("Couldn't create GINI object");
                    status = NBS_STATUS_NOMEM;
                }
                else {
                    ptr->type = NBSP_TYPE_NONE;
                    ptr->nbsa = nbsa;
                    *nbsp = ptr;
                }
            }
        }
    }
    return status;
}

/**
 * Processes the start of an encoded GINI image.
 *
 * @pre       nbsp_end_product() called on previous product
 * @param[in] nbsp               NBS presentation-layer object
 * @param[in] buf                Encoded and possibly compressed data block
 * @param[in] nbytes             Number of bytes in `buf`
 * @param[in] rec_len            Number of bytes in a record
 * @param[in] recs_per_block     Number of records in data block
 * @param[in] is_compressed      Is `buf` zlib(3)-compressed?
 * @param[in] prod_type          NBS transport-layer product-specific header
 *                               product-type
 * @param[in] size_estimate      Size estimate of product in bytes
 * @retval    0                  Success
 * @retval    NBS_STATUS_INVAL   Invalid `buf` or `recs_per_block == 0`.
 *                               log_add() called.
 * @retval    NBS_STATUS_NOMEM   Out of memory. log_add() called.
 * @retval    NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval    NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
nbs_status_t nbsp_gini_start(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                rec_len,
        const unsigned                recs_per_block,
        const bool                    is_compressed,
        const int                     prod_type,
        size_t                        size_estimate)
{
    int status = nbsp_is_ready_for_start(nbsp);
    if (status == 0) {
        dynabuf_t* const dynabuf = nbsp->dynabuf;
        dynabuf_clear(dynabuf);
        status = dynabuf_reserve(dynabuf, size_estimate);
        if (status == 0) {
            status = gini_start(nbsp->gini, buf, nbytes, rec_len,
                    recs_per_block, is_compressed, prod_type);
            if (status)
                log_add("Couldn't start GINI image");
        }
    }
    return status;
}

/**
 * Processes a block of data for a GINI image.
 *
 * @pre       nbsp_nesdis_start() called
 * @param[in] nbsp               NBS presentation-layer object
 * @param[in] buf                Encoded and possibly compressed block of data
 *                               for a GINI image
 * @param[in] nbytes             Number of bytes in `buf`
 * @param[in] block_index        Index of block in `buf`. The first block of
 *                               actual image data is block 1.
 * @param[in] is_compressed      Is `buf` zlib(3)-compressed?
 * @retval    0                  Success
 * @retval    NBS_STATUS_INVAL  Invalid `buf`. log_add() called.
 * @retval    NBS_STATUS_LOGIC  Logic error. log_add() called.
 */
nbs_status_t nbsp_gini_block(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                block_index,
        const bool                    is_compressed)
{
    gini_t* const gini = nbsp->gini;
    int           status = gini_add_missing_blocks(gini, block_index);
    if (status) {
        log_add("Couldn't add missing GINI records (i.e., scan lines)");
    }
    else {
        status = gini_add_block(gini, buf, nbytes, is_compressed);
        if (status)
            log_add("Couldn't add data-block %u to GINI image", block_index);
    }
    return status;
}

nbs_status_t nbsp_nongoes(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end,
        const bool                    is_compressed)
{
    return -1;
}

nbs_status_t nbsp_nwstg(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end)
{
    return -1;
}

nbs_status_t nbsp_nexrad(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const bool                    is_start,
        const bool                    is_end)
{
    return -1;
}

/**
 * Finishes processing the current product. Idempotent: does nothing if there's
 * no current product.
 *
 * @retval NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval NBS_STATUS_NOMEM   Out of memory. log_add() called.
 * @retval NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
nbs_status_t nbsp_end_product(
        nbsp_t* const nbsp)
{
    int status;
    switch (nbsp->type) {
        case NBSP_TYPE_NONE:
            status = 0;
            break;
        case NBSP_TYPE_GINI:
            status = gini_finish(nbsp->gini);
            if (status) {
                log_add("Couldn't finish GINI image");
            }
            else {
                status = nbsa_process_gini(nbsp->nbsa, nbsp->gini);
                if (status)
                    log_add("NBS application-layer couldn't process GINI image");
            }
            nbsp->type = NBSP_TYPE_NONE;
            break;
        default:
            log_add("Unknown product type: %d", nbsp->type);
            status = NBS_STATUS_LOGIC;
    }
    return status;
}

void nbsp_free(
        nbsp_t* const nbsp)
{
    if (nbsp) {
        dynabuf_free(nbsp->dynabuf);
        free(nbsp);
    }
}
