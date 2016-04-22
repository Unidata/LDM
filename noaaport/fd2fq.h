/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_link.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for an object that transfers NBS frames from a
 * file-descriptor to a frame-queue.
 */

#ifndef NOAAPORT_NBS_LINK_H_
#define NOAAPORT_NBS_LINK_H_

#include "frame_queue.h"
#include "nbs_status.h"

typedef struct fd2fq fd2fq_t;

#ifdef __cplusplus
    extern "C" {
#endif

nbs_status_t fd2fq_new(
        fd2fq_t** const restrict fd2fq,
        const int                fd,
        fq_t* const restrict     fq);

nbs_status_t fd2fq_execute(
        const int            fd,
        fq_t* const restrict fq);

void fd2fq_free(
        fd2fq_t* fd2fq);

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_LINK_H_ */
