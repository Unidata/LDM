/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: file_id_queue.hin
 * @author: Steven R. Emmerson
 *
 * This file declares the API for the multicast information returned by a
 * server.
 */

#ifndef MCAST_INFO_H
#define MCAST_INFO_H

#include "inetutil.h"
#include "ldm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns a new multicast information object.
 *
 * @param[in] name       The name of the multicast group. The caller may free.
 * @param[in] mcast      The Internet address of the multicast group. The caller
 *                       may free.
 * @param[in] ucast      The Internet address of the unicast service for blocks
 *                       and files that are missed by the multicast receiver.
 *                       The caller may free.
 * @retval    NULL       Failure. `log_start()` called.
 * @return               The new, initialized multicast information object.
 */
McastInfo*
mi_new(
    const char* const restrict        name,
    const ServiceAddr* const restrict mcast,
    const ServiceAddr* const restrict ucast);

/**
 * Frees multicast information.
 *
 * @param[in,out] mcastInfo  Pointer to multicast information to be freed or
 *                           NULL. If non-NULL, then it must have been returned
 *                           by `mgi_new()`.
 */
void
mi_free(
    McastInfo* const mcastInfo);

/**
 * Copies multicast information. Performs a deep copy.
 *
 * @param[out] to           Destination.
 * @param[in]  from         Source. The caller may free.
 * @retval     0            Success.
 * @retval     LDM7_SYSTEM  System error. \c log_add() called.
 */
int
mi_copy(
    McastInfo* const restrict       to,
    const McastInfo* const restrict from);

/**
 * Returns a formatted representation of a multicast information object that's
 * suitable as a filename.
 *
 * @param[in] info  The multicast information object.
 * @retval    NULL  Failure. `log_add()` called.
 * @return          A filename representation of `info`.
 */
const char*
mi_asFilename(
    const McastInfo* const info);

#ifdef __cplusplus
}
#endif

#endif
