/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_status.h
 * @author: Steven R. Emmerson
 *
 * This file declares the NOAAPort Broadcast System (NBS) status value.
 */

#ifndef NOAAPORT_NBS_STATUS_H_
#define NOAAPORT_NBS_STATUS_H_

typedef enum {
    NBS_STATUS_SUCCESS = 0,
    NBS_STATUS_INVAL,
    NBS_STATUS_NOMEM,
    NBS_STATUS_IO,
    NBS_STATUS_INIT,
    NBS_STATUS_UNSUPP,
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

#endif /* NOAAPORT_NBS_STATUS_H_ */
