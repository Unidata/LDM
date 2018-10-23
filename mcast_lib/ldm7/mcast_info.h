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

#include "InetSockAddr.h"
#include "inetutil.h"
#include "ldm.h"

#ifdef __cplusplus
extern "C" {
#define restrict
#endif

/**
 * Initializes a multicast information object.
 *
 * @param[out] info       The multicast information object.
 * @param[in]  feed       The feedtype of the multicast group.
 * @param[in]  mcast      The Internet address of the multicast group. The
 *                        caller may free.
 * @param[in]  ucast      The Internet address of the unicast service for blocks
 *                        and files that are missed by the multicast receiver.
 *                        The caller may free.
 * @retval     true       Success. `info` is set.
 * @retval     false      Failure. \c log_add() called. The state of `info` is
 *                        indeterminate.
 */
bool
mi_init(
    McastInfo* const restrict  info,
    const feedtypet            feed,
    const char* const restrict mcast,
    const char* const restrict ucast);

/**
 * Returns a new multicast information object.
 *
 * @param[out] mcastInfo  Initialized multicast information object.
 * @param[in]  feed       The feedtype of the multicast group.
 * @param[in]  mcast      The Internet address of the multicast group. The
 *                        caller may free.
 * @param[in]  ucast      The Internet address of the unicast service for blocks
 *                        and files that are missed by the multicast receiver.
 *                        The caller may free.
 * @retval     0          Success. `*info` is set.
 * @retval     ENOMEM     Out-of-memory error. `log_add()` called.
 */
int
mi_new(
    McastInfo** const          mcastInfo,
    const feedtypet            feed,
    const char* const restrict mcast,
    const char* const restrict ucast);

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
 * @retval     LDM7_SYSTEM  System error. \c log_add() called.
 */
Ldm7Status
mi_copy(
    McastInfo* const restrict       to,
    const McastInfo* const restrict from);

/**
 * Clones a multicast information object.
 *
 * @param[in] info  Multicast information object to be cloned.
 * @retval    NULL  Failure. `log_add()` called.
 * @return          Clone of multicast information object. Caller should call
 *                  `mi_delete()` when the clone is no longer needed.
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
 * @retval        LDM7_SYSTEM  System failure. `log_add()` called.
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
 * @retval    NULL  Failure. `log_add()` called.
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
 * @retval    NULL  Failure. `log_add()` called.
 * @return          A string representation of `info`. Caller should free when
 *                  it's no longer needed.
 */
char*
mi_format(
    const McastInfo* const info);

typedef struct sepMcastInfo SepMcastInfo;

/**
 * Returns a new separated-out multicast information object.
 *
 * @param[in] feed         LDM7 feed
 * @param[in] mcastGrpStr  String representation of the multicast group address
 *                         in the form
 *                           - <name>[:<port>]
 *                           - <nnn.nnn.nnn.nnn>[:<port>]
 *                         The default port number is `LDM_PORT`. May be freed.
 * @param[in] fmtpSrvrStr  String representation of the FMTP server address in
 *                         the form
 *                           - <name>[:<port>]
 *                           - <nnn.nnn.nnn.nnn>[:<port>]
 *                         The default port number is 0. May be freed.
 * @retval    NULL         Failure. `log_add()` called.
 */
SepMcastInfo*
smi_newFromStr(
        const feedtypet     feed,
        const char* const   mcastGrpStr,
        const char* const   fmtpSrvrStr);

/**
 * Frees a separated-out multicast information object.
 *
 * @param[in,out] smi  Separated-out multicast information object
 */
void
smi_free(SepMcastInfo* const smi);

/**
 * Returns the string representation of a separated-out multicast information
 * object.
 *
 * @param[in] smi   Separated-out multicast information object
 * @retval    NULL  Failure. `log_add()` called.
 * @return          String representation. Caller should free when it's no
 *                  longer needed.
 */
char*
smi_toString(const SepMcastInfo* const smi);

/**
 * Sets the LDM7 feed of a separated-out multicast information object.
 *
 * @param[in,out] smi   Separated-out multicast information object
 * @param[in]     feed  LDM7 feed
 */
void
smi_setFeed(
        SepMcastInfo* const smi,
        const feedtypet     feed);

/**
 * Returns the LDM7 feed of a separated-out multicast information object.
 *
 * @param[in] smi   Separated-out multicast information object
 * @return          LDM7 feed
 */
feedtypet
smi_getFeed(const SepMcastInfo* const smi);

/**
 * Returns the Internet socket address of the multicast group of a separated-out
 * multicast information object. Returns a pointer to the actual object.
 *
 * @param[in] smi   Separated-out multicast information object
 * @return          Internet socket address of the multicast group
 */
InetSockAddr*
smi_getMcastGrp(const SepMcastInfo* const smi);

/**
 * Returns the Internet socket address of the FMTP server of a separated-out
 * multicast information object. Returns a pointer to the actual object.
 *
 * @param[in] smi   Separated-out multicast information object
 * @return          Internet socket address of the FMTP server
 */
InetSockAddr*
smi_getFmtpSrvr(const SepMcastInfo* const smi);

/**
 * Returns the Internet socket address of the multicast group of a separated-out
 * multicast information object.
 *
 * @param[in] smi   Separated-out multicast information object
 * @return          Internet socket address of the multicast group
 */
InetSockAddr*
smi_getMcastGrp(const SepMcastInfo* const smi);

#ifdef __cplusplus
}
#endif

#endif
