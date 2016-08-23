/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: dynabuf.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the ...
 */

#ifndef NOAAPORT_DYNABUF_H_
#define NOAAPORT_DYNABUF_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
    extern "C" {
#endif

typedef enum {
    DYNABUF_STATUS_SUCCESS = 0,
    DYNABUF_STATUS_INVAL,
    DYNABUF_STATUS_NOMEM
} dynabuf_status_t;

typedef struct dynabuf dynabuf_t;

dynabuf_status_t dynabuf_new(
        dynabuf_t** const dynabuf,
        const size_t      nbytes);

void dynabuf_free(
        dynabuf_t* const dynabuf);

dynabuf_status_t dynabuf_reserve(
        dynabuf_t* const dynabuf,
        const size_t     nbytes);

dynabuf_status_t dynabuf_add(
        dynabuf_t* const restrict     dynabuf,
        const uint8_t* const restrict buf,
        const size_t                  nbytes);

dynabuf_status_t dynabuf_set(
        dynabuf_t* const restrict dynabuf,
        const int                 byte,
        const size_t              nbytes);

uint8_t* dynabuf_get_buf(
        dynabuf_t* const dynabuf);

size_t dynabuf_get_used(
        const dynabuf_t* const dynabuf);

void dynabuf_set_used(
        dynabuf_t* const dynabuf,
        const size_t     nbytes);

void dynabuf_clear(
        dynabuf_t* const dynabuf);

void dynabuf_fini(
        dynabuf_t* dynabuf);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_DYNABUF_H_ */
