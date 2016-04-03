/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: gini.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for GINI-formated products.
 */

#ifndef NOAAPORT_GINI_H_
#define NOAAPORT_GINI_H_

#include "dynabuf.h"

#include <stdbool.h>

typedef enum {
    GINI_STATUS_SUCCESS = 0,
    GINI_STATUS_INVAL,
    GINI_STATUS_NOMEM,
    GINI_STATUS_LOGIC,
    GINI_STATUS_SYSTEM
} gini_status_t;

typedef struct gini gini_t;

#ifdef __cplusplus
    extern "C" {
#endif

gini_status_t gini_new(
        gini_t** const restrict   gini,
        dynabuf_t* const restrict dynabuf);

gini_status_t gini_start(
        gini_t* const restrict        gini,
        const uint8_t* const restrict buf,
        const unsigned                nbytes,
        const unsigned                rec_len,
        const unsigned                recs_per_block,
        const bool                    is_compressed,
        const int                     prod_type);

gini_status_t gini_add_block(
        gini_t* const restrict        gini,
        const uint8_t* const restrict data,
        const unsigned                nbytes,
        const bool                    is_compressed);

gini_status_t gini_add_missing_blocks(
        gini_t* const  gini,
        const unsigned valid_block_index);

gini_status_t gini_finish(
        gini_t* const gini);

bool gini_is_compressed(
        const gini_t* const gini);

int gini_get_prod_type(
        const gini_t* const gini);

uint8_t gini_get_creating_entity(
        const gini_t* const gini);

uint8_t gini_get_sector(
        const gini_t* const gini);

uint8_t gini_get_physical_element(
        const gini_t* const gini);

int gini_get_year(
        const gini_t* const gini);

int gini_get_month(
        const gini_t* const gini);

int gini_get_day(
        const gini_t* const gini);

int gini_get_hour(
        const gini_t* const gini);

int gini_get_minute(
        const gini_t* const gini);

uint8_t gini_get_image_resolution(
        const gini_t* const gini);

const char* gini_get_wmo_header(
        const gini_t* const gini);

unsigned gini_get_serialized_size(
        const gini_t* const gini);

uint8_t* gini_get_serialized_image(
        const gini_t* const gini);

void gini_free(
        gini_t* gini);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_GINI_H_ */
