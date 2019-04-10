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

#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "md5.h"
#include "nbs_application.h"
#include "pq.h"
#include "timestamp.h"

#include <string.h>

// These functions aren't declared in a header-file:
const char *platform_id(unsigned char satid);
const char *channel_id(unsigned char channel);
const char *sector_id(unsigned char sector);

/******************************************************************************
 * NBS Application-Layer Object:
 ******************************************************************************/

struct nbsa {
    pqueue*  pq;   ///< LDM product-queue
    product  prod; ///< LDM data-product
    MD5_CTX* md5;  ///< MD5 object
    nbsp_t*  nbsp; ///< NBS presentation-layer object
};

static char hostname[HOSTNAMESIZE]; ///< Local host name

static nbs_status_t nbsa_init(
        nbsa_t* const restrict nbsa)
{
    int status;
    if (hostname[0] == 0) {
        (void)strncpy(hostname, ghostname(), sizeof(hostname));
        hostname[sizeof(hostname)-1] = 0;
    }
    log_assert(nbsa);
    MD5_CTX* md5 = new_MD5_CTX();
    if (md5 == NULL) {
        log_add_syserr("Couldn't create new MD5 object");
        status = NBS_STATUS_SYSTEM;
    }
    else {
        nbsa->md5 = md5;
        nbsa->pq = NULL;
        nbsa->prod.data = NULL;
        nbsa->prod.info.origin = hostname;
        nbsa->prod.info.seqno = 0;
        status = 0;
    }
    return status;
}

/**
 * Returns a new NBS application-layer object.
 *
 * @param[out] nbsa              NBS application-layer object
 * @retval     0                 Success. `*nbsa` is set.
 * @retval     NBS_STATUS_INVAL  `nbsa == NULL || pq == NULL`. log_add()
 *                               called.
 * @retval     NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
nbs_status_t nbsa_new(
        nbsa_t** const restrict nbsa)
{
    int status;
    if (nbsa == NULL) {
        log_add("NULL argument: nbsa=%p", nbsa);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsa_t* obj = log_malloc(sizeof(nbsa_t),
                "NBS application-layer object");
        if (obj == NULL) {
            status = NBS_STATUS_NOMEM;
        }
        else {
            status = nbsa_init(obj);
            if (status) {
                log_add("Couldn't initialize NBS application-layer object");
                free(obj);
            }
            else {
                *nbsa = obj;
            }
        } // 'obj` allocated
    }
    return status;
}

/**
 * Sets the product-queue for receiving data-products.
 *
 * @param[out] nbsa              NBS application-layer object
 * @param[in]  pq                LDM product-queue
 * @retval     0                 Success
 * @retval     NBS_STATUS_INVAL  `nbsa == NULL || pq == NULL`. log_add()
 *                               called.
 */
nbs_status_t nbsa_set_pq(
        nbsa_t* const restrict nbsa,
        pqueue* const restrict pq)
{
    int status;
    if (nbsa == NULL || pq == NULL) {
        log_add("NULL argument: nbsa=%p, pq=%p", nbsa, pq);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsa->pq = pq;
        status = 0;
    }
    return status;
}

/**
 * Sets the NBS presentation-layer object of an NBS application-layer object for
 * sending data-products.
 *
 * @param[out] nbsa              NBS application-layer object
 * @param[in]  nbsp              NBS presentation-layer object
 * @retval     0                 Success
 * @retval     NBS_STATUS_INVAL  `nbsa == NULL || nbsp == NULL`. log_add()
 *                               called.
 */
nbs_status_t nbsa_set_presentation_layer(
        nbsa_t* const restrict nbsa,
        nbsp_t* const restrict nbsp)
{
    int status;
    if (nbsa == NULL || nbsp == NULL) {
        log_add("NULL argument: nbsa=%p, nbsp=%p", nbsa, nbsp);
        status = NBS_STATUS_INVAL;
    }
    else {
        nbsa->nbsp = nbsp;
        status = 0;
    }
    return status;
}

/**
 * Processes a GINI image. Converts the image into an LDM data-product and
 * inserts the product into the LDM product-queue.
 *
 * @param[in] gini           GINI image
 * @retval 0                 Success. Product was inserted, is duplicate, or was
 *                           too large for the queue. Continue.
 * @retval NBS_STATUS_INVAL  `gini` is invalid. log_add() called.
 * @retval NBS_STATUS_SYSTEM System failure. log_add() called.
 */
nbs_status_t nbsa_recv_gini(
        nbsa_t* const restrict       nbsa,
        const gini_t* const restrict gini)
{
    int            status;
    product* const prod = &nbsa->prod;
    prod_info*     info = &prod->info;
    char           ident[KEYSIZE+1];
    int            nbytes = snprintf(ident, sizeof(ident),
            "%s/ch%d/%s/%s/%04d%02d%02d %02d%02d/%s/%dkm/ %s",
            gini_is_compressed(gini) ? "satz" : "sat",
            gini_get_prod_type(gini),
            platform_id(gini_get_creating_entity(gini)),
            channel_id(gini_get_physical_element(gini)),
            gini_get_year(gini),
            gini_get_month(gini),
            gini_get_day(gini),
            gini_get_hour(gini),
            gini_get_minute(gini),
            sector_id(gini_get_sector(gini)),
            gini_get_image_resolution(gini),
            gini_get_wmo_header(gini));
    if (nbytes < 0) {
        log_add("Couldn't format LDM product-identifier");
        status = NBS_STATUS_INVAL;
    }
    else {
        ident[sizeof(ident)-1] = 0;
        status = set_timestamp(&info->arrival);
        if (status) {
            log_add_syserr("set_timestamp() failure");
            status = NBS_STATUS_SYSTEM;
        }
        else {
            info->ident = ident;
            info->feedtype = NIMAGE;
            info->sz = gini_get_serialized_size(gini);
            prod->data = gini_get_serialized_image(gini);
            MD5Init(nbsa->md5);
            MD5Update(nbsa->md5, prod->data, info->sz);
            MD5Final (info->signature, nbsa->md5);
            status = pq_insert(nbsa->pq, prod);
            switch (status) {
            case 0:
                log_info_q("Product inserted: %s", s_prod_info(NULL, 0, info,
                        0));
                break;
            case PQ_DUP:
                log_info_q("Duplicate product: %s", s_prod_info(NULL, 0, info,
                        0));
                status = 0;
                break;
            case PQ_BIG:
                log_warning_q("Product too big for queue: %s",
                        s_prod_info(NULL, 0, info, 0));
                status = 0;
                break;
            default:
                log_errno(status, "Couldn't insert product: %s",
                        s_prod_info(NULL, 0, info, 0));
                status = NBS_STATUS_SYSTEM;
                break;
            }
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
    if (nbsa) {
        free_MD5_CTX(nbsa->md5);
        free(nbsa);
    }
}
