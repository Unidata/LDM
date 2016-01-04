/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mcast_info.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for information on a multicast group.
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
 * @param[out] mcastInfo  Initialized multicast information object.
 * @param[in]  feed       The feedtype of the multicast group.
 * @param[in]  mcast      The Internet address of the multicast group. The caller
 *                        may free.
 * @param[in]  ucast      The Internet address of the unicast service for blocks
 *                        and files that are missed by the multicast receiver.
 *                        The caller may free.
 * @retval     0          Success. `*info` is set.
 * @retval     ENOMEM     Out-of-memory error. `mylog_add()` called.
 */
int
mi_new(
    McastInfo** const                 mcastInfo,
    const feedtypet                   feed,
    const ServiceAddr* const restrict mcast,
    const ServiceAddr* const restrict ucast);

/**
 * Destroys a multicast information object.
 *
 * @param[in] info  The multicast information object.
 */
void
mi_destroy(
    McastInfo* const info);

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
 * @retval     LDM7_SYSTEM  System error. \c mylog_add() called.
 */
Ldm7Status
mi_copy(
    McastInfo* const restrict       to,
    const McastInfo* const restrict from);

/**
 * Clones a multicast information object.
 *
 * @param[in] info  Multicast information object to be cloned.
 * @retval    NULL  Failure. `mylog_add()` called.
 * @return          Clone of multicast information object. Caller should call
 *                  `mi_free()` when the clone is no longer needed.
 */
McastInfo*
mi_clone(
    const McastInfo* const info);

/**
 * Replaces the Internet identifier of the TCP server.
 *
 * @param[in,out] info         Multicast information to be modified.
 * @param[in]     id           Replacement Internet identifier.
 * @retval        0            Success. `info->server.inetId` was freed and now
 *                             points to a copy of `id`.
 * @retval        LDM7_SYSTEM  System failure. `mylog_add()` called.
 */
Ldm7Status
mi_replaceServerId(
        McastInfo* const  info,
        const char* const id);

/**
 * Returns the feedtype of a multicast information object.
 *
 * @param[in] info  Multicast information object.
 * @return          Feedtype of the object.
 */
feedtypet
mi_getFeedtype(
        const McastInfo* const info);

/**
 * Compares the server information of two multicast information objects. Returns
 * a value less than, equal to, or greater than zero as the server information
 * in the first object is considered less than, equal to, or greater than the
 * server information in the second object, respectively. Server informations
 * are considered equal if their TCP server Internet identifiers and port
 * numbers are equal.
 *
 * @param[in] info1  First multicast information object.
 * @param[in] info2  Second multicast information object.
 * @return
 */
int
mi_compareServers(
    const McastInfo* const restrict info1,
    const McastInfo* const restrict info2);

/**
 * Compares the multicast group information of two multicast information
 * objects. Returns a value less than, equal to, or greater than zero as the
 * server information in the first object is considered less than, equal to, or
 * greater than the server information in the second object, respectively.
 * Multicast group informations are considered equal if their Internet
 * identifiers and port numbers are equal.
 *
 * @param[in] info1  First multicast group information object.
 * @param[in] info2  Second multicast group information object.
 * @retval    -1     First object is less than second.
 * @retval     0     Objects are equal.
 * @retval    +1     First object is greater than second.
 */
int
mi_compareGroups(
    const McastInfo* const restrict info1,
    const McastInfo* const restrict info2);

/**
 * Returns a formatted representation of a multicast information object that's
 * suitable for use as a filename.
 *
 * @param[in] info  The multicast information object.
 * @retval    NULL  Failure. `mylog_add()` called.
 * @return          A filename representation of `info`. Caller should free when
 *                  it's no longer needed.
 */
char*
mi_asFilename(
    const McastInfo* const info);

/**
 * Returns a formatted representation of a multicast information object.
 *
 * @param[in] info  The multicast information object.
 * @retval    NULL  Failure. `mylog_add()` called.
 * @return          A string representation of `info`. Caller should free when
 *                  it's no longer needed.
 */
char*
mi_format(
    const McastInfo* const info);

#ifdef __cplusplus
}
#endif

#endif
