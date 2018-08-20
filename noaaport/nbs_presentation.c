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

#include <search.h>
#include <stdint.h>

/******************************************************************************
 * Private:
 ******************************************************************************/

struct nbsp {
    gini_t*    gini;     ///< GINI image
    dynabuf_t* dynabuf;  ///< Dynamic buffer for accumulating product
    nbsa_t*    nbsa;     ///< NBS application-layer object
    nbst_t*    nbst;     ///< NBS transport-layer object
    enum {
        NBSP_TYPE_NONE = 0,//!< NBSP_TYPE_NONE
        NBSP_TYPE_GINI     //!< NBSP_TYPE_GINI
    } type;             ///< Type of product-in-progress
};

/**
 * Indicates if a presentation-layer object is ready for the start of a new
 * roduct.
 *
 * @param[in] nbsp           Presentation-layer object
 * @retval 0                 Ready
 * @retval NBS_STATUS_LOGIC  Logic error. log_add() called.
 */
static nbs_status_t nbsp_is_ready_for_start(
        const nbsp_t* const nbsp)
{
    int status = (nbsp->type == NBSP_TYPE_NONE) ? 0 : NBS_STATUS_LOGIC;
    if (status)
        log_add("nbsp_end_product() not called");
    return status;
}

/******************************************************************************
 * Public:
 ******************************************************************************/

/**
 *
 * @param[out] nbsp          NBS presentation-layer
 * @retval NBS_STATUS_INVAL  `nbsp == NULL || nbsa == NULL`. log_add() called.
 * @retval NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbsp_new(
        nbsp_t* restrict* const restrict nbsp)
{
    int status;
    if (nbsp == NULL) {
        log_add("NULL argument: nbsp=%p", nbsp);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsp_t* const obj = log_malloc(sizeof(nbsp_t),
                "NBS presentation-layer object");
        if (obj == NULL) {
            status = NBS_STATUS_NOMEM;
        }
        else {
            status = dynabuf_new(&obj->dynabuf, NBS_MAX_FRAME_SIZE);
            if (status) {
                log_add("Couldn't create dynamic buffer");
                status = NBS_STATUS_NOMEM;
            }
            else {
                status = gini_new(&obj->gini, obj->dynabuf);
                if (status) {
                    log_add("Couldn't create GINI object");
                    dynabuf_free(obj->dynabuf);
                    status = NBS_STATUS_NOMEM;
                }
                else {
                    obj->type = NBSP_TYPE_NONE;
                    obj->nbsa = NULL;
                    obj->nbst = NULL;
                    *nbsp = obj;
                }
            } // `obj->dynabuf` allocated
            if (status)
                free(obj);
        } // `obj` allocated
    }
    return status;
}

/**
 *
 * @param[out] nbsp          NBS presentation-layer
 * @param[in]  nbsa          NBS application-layer
 * @retval NBS_STATUS_INVAL  `nbsp == NULL || nbsa == NULL`. log_add() called.
 * @retval NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbsp_set_application_layer(
        nbsp_t* const restrict nbsp,
        nbsa_t* const restrict nbsa)
{
    int status;
    if (nbsp == NULL || nbsa == NULL) {
        log_add("NULL argument: nbsp=%p, nbsa=%p", nbsp, nbsa);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsp->nbsa = nbsa;
        status = 0;
    }
    return status;
}

/**
 *
 * @param[out] nbsp          NBS presentation-layer
 * @param[in]  nbsa          NBS application-layer
 * @retval NBS_STATUS_INVAL  `nbsp == NULL || nbsa == NULL`. log_add() called.
 * @retval NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbsp_set_transport_layer(
        nbsp_t* const restrict nbsp,
        nbst_t* const restrict nbst)
{
    int status;
    if (nbsp == NULL || nbst == NULL) {
        log_add("NULL argument: nbsp=%p, nbst=%p", nbsp, nbst);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsp->nbst = nbst;
        status = 0;
    }
    return status;
}

/**
 * Processes the start of a GINI image from the transport-layer towards the
 * application-layer.
 *
 * @pre       nbsp_end_product() called on previous product
 * @param[in] nbsp               NBS presentation-layer object
 * @param[in] buf                Serialized and possibly compressed data block
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
nbs_status_t nbsp_recv_gini_start(
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
            if (status) {
                log_add("Couldn't initialize GINI image");
            }
            else {
                nbsp->type = NBSP_TYPE_GINI;
            }
        }
    }
    return status;
}

/**
 * Processes a block of data (not the product-definition block) for a GINI image
 * from the transport-layer towards the application-layer.
 *
 * @pre       nbsp_nesdis_start() called
 * @param[in] nbsp               NBS presentation-layer object
 * @param[in] buf                Serialized and possibly compressed pixel-block
 *                               for a GINI image
 * @param[in] nbytes             Number of bytes in `buf`
 * @param[in] block_index        Index of block in `buf`. The first block of
 *                               actual image data is block 1.
 * @param[in] is_compressed      Is `buf` zlib(3)-compressed?
 * @retval    0                  Success
 * @retval    NBS_STATUS_INVAL   Invalid `buf`. log_add() called.
 * @retval    NBS_STATUS_LOGIC   Logic error. log_add() called.
 */
nbs_status_t nbsp_recv_gini_block(
        nbsp_t* const restrict        nbsp,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                block_index,
        const bool                    is_compressed)
{
    gini_t* const gini = nbsp->gini;
    int status = gini_add_block(gini, block_index, buf, nbytes, is_compressed);
    if (status)
        log_add("Couldn't add data-block %u to GINI image", block_index);
    return status;
}

/**
 * Transfers a GINI image from the application-layer to the transport-layer.
 *
 * @param[in] nbsp            NBS presentation-layer
 * @param[in] gini            GINI image
 * @retval 0                  Success
 * @retval NBS_STATUS_LOGIC   Logic error. log_add() called.
 * @retval NBS_STATUS_NOMEM   Out of memory. log_add() called.
 * @retval NBS_STATUS_SYSTEM  System failure. log_add() called.
 */
nbs_status_t nbsp_send_gini(
        nbsp_t* const restrict       nbsp,
        const gini_t* const restrict gini)
{
    const int      prod_type = gini_get_prod_type(gini);
    const unsigned recs_per_block = gini_get_recs_per_block(gini);
    const unsigned bytes_per_record = gini_get_rec_len(gini);
    const unsigned num_blocks = gini_get_num_blocks(gini);
    const bool     is_compressed = gini_is_compressed(gini);
    nbst_send_start(nbsp->nbst, recs_per_block, bytes_per_record, prod_type,
            num_blocks, is_compressed);
    const uint8_t* block;
    unsigned       nbytes;
    int            status;
    gini_iter_t    iter;
    gini_iter_init(&iter, gini);
    for (unsigned iblock = 0; ; iblock++) {
        status = gini_iter_next(&iter, &block, &nbytes);
        if (status) {
            if (status == GINI_STATUS_END) {
                status = 0;
            }
            else {
                log_add("Couldn't get block %u of GINI image", iblock);
            }
            break;
        }
        log_debug("Sending %u-byte block %u", nbytes, iblock);
        status = nbst_send_block(nbsp->nbst, block, nbytes);
        if (status) {
            log_add("Couldn't send %u-byte block %u of GINI image", nbytes,
                    iblock);
            break;
        }
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
nbs_status_t nbsp_recv_end(
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
                status = (status == GINI_STATUS_NOMEM)
                        ? NBS_STATUS_NOMEM
                        : (status == GINI_STATUS_SYSTEM)
                          ? NBS_STATUS_SYSTEM
                          : NBS_STATUS_LOGIC;
            }
            else {
                status = nbsa_recv_gini(nbsp->nbsa, nbsp->gini);
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
        gini_free(nbsp->gini);
        dynabuf_free(nbsp->dynabuf);
        free(nbsp);
    }
}
