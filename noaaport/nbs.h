/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs.h
 * @author: Steven R. Emmerson
 *
 * This file implements ...
 */

#ifndef NOAAPORT_NBS_H_
#define NOAAPORT_NBS_H_

#define NBS_MAX_FRAME_SIZE 10000

typedef enum {
    NBS_STATUS_SUCCESS = 0,
    NBS_STATUS_INVAL,
    NBS_STATUS_NOMEM,
    NBS_STATUS_IO,
    NBS_STATUS_INIT,
    NBS_STATUS_UNSUPP,
    NBS_STATUS_NOSTART,
    NBS_STATUS_END,
    NBS_STATUS_SYSTEM,
    NBS_STATUS_LOGIC
} nbs_status_t;

#ifdef __cplusplus
    extern "C" {
#endif

#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_NBS_H_ */
