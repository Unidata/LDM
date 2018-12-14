/**
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: UpMcastMgr.h
 * @author: Steven R. Emmerson
 *
 * This file declares the manager for multicasting from the upstream site.
 */

#ifndef UPMCASTMGR_H
#define UPMCASTMGR_H

#include "CidrAddr.h"
#include "fmtp.h"
#include "ldm.h"
#include "pq.h"
#include "VirtualCircuit.h"

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes this module. Creates an IPC resource. Shall be called only once
 * per LDM session.
 *
 * @retval 0            Success.
 * @retval LDM7_LOGIC   Module is already initialized. `log_add()` called.
 * @retval LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
umm_init(void);

/**
 * Destroys this module and, optionally, frees the IPC resource. Should be
 * called once in each process per LDM session.
 *
 * Idempotent.
 *
 * @param[in] final  Whether to free the IPC resource. Should be `true` only
 *                   once per LDM session.
 */
void
umm_destroy(const bool final);

/**
 * Indicates if this module has been initialized.
 *
 * @retval `true`   This module has been initialized
 * @retval `false`  This module has not been initialized
 */
bool
umm_isInited(void);

/**
 * Removes the entry corresponding to a process identifier.
 *
 * @param[in] pid          Process identifier.
 * @retval    0            Success
 * @retval    LDM7_LOGIC   Module is not initialized. `log_add()` called.
 * @retval    LDM7_NOENT   No entry corresponding to given process identifier.
 *                         Database is unchanged.
 */
Ldm7Status
umm_remove(const pid_t pid);

/**
 * Sets the FMTP retransmission timeout. Calls to `umm_subscribe()` will use
 * this value when creating a new upstream multicast LDM sender.
 * @param[in] minutes  FMTP retransmission timeout. A negative value obtains the
 *                     FMTP default.
 * @see                `umm_subscribe()`
 */
void
umm_setRetxTimeout(const float minutes);

/**
 * Sets the name of the AL2S workgroup. Only necessary if the multicast LDM
 * senders will use an AL2S multipoint VLAN.
 *
 * @param[in] name  Name of the AL2S workgroup. Caller must not free until done
 *                  with this module.
 */
void
umm_setWrkGrpName(const char* name);

/**
 * Adds a potential multicast LDM sender. The sender is not started. This
 * function should be called for all potential senders before any child
 * process is forked so that all child processes will have this information.
 *
 * @param[in] mcastIface   IPv4 address of interface to use for multicasting.
 *                         "0.0.0.0" obtains the system's default multicast
 *                         interface.
 * @param[in] mcastInfo    Information on the multicast group. The port number
 *                         of the FMTP TCP server is ignored (it will be chosen
 *                         by the operating system). Caller may free.
 * @param[in] ttl          Time-to-live for multicast packets:
 *                                0  Restricted to same host. Won't be output by
 *                                   any interface.
 *                                1  Restricted to same subnet. Won't be
 *                                   forwarded by a router.
 *                              <32  Restricted to same site, organization or
 *                                   department.
 *                              <64  Restricted to same region.
 *                             <128  Restricted to same continent.
 *                             <255  Unrestricted in scope. Global.
 * @param[in] vcEnd        Local virtual-circuit endpoint or `NULL`. Caller may
 *                         free.
 * @param[in] fmtpSubnet   Subnet for client FMTP TCP connections. Caller may
 *                         free.
 * @param[in] pqPathname   Pathname of product-queue. Caller may free.
 * @retval    0            Success.
 * @retval    LDM7_INVAL   Invalid argument. `log_add()` called.
 * @retval    LDM7_LOGIC   Module is not initialized. `log_add()` called.
 * @retval    LDM7_DUP     Multicast group information conflicts with earlier
 *                         addition. Manager not modified. `log_add()` called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
umm_addSndr(
    const struct in_addr               mcastIface,
    const SepMcastInfo* const restrict mcastInfo,
    const unsigned short               ttl,
    const VcEndPoint* const restrict   vcEnd,
    const CidrAddr* const restrict     fmtpSubnet,
    const char* const restrict         pqPathname);

/**
 * Subscribes to an LDM7 multicast:
 *   - Starts the multicast LDM process if necessary
 *   - Returns information on the multicast group
 *   - Returns the CIDR address for the FMTP client
 *
 * @param[in]  feed         Multicast group feed-type.
 * @param[in]  clntAddr     Address of client
 * @param[in]  rmtVcEnd     Remote virtual-circuit endpoint or `NULL`. Caller
 *                          may free.
 * @param[out] smi          Separated-out multicast information. Set only on
 *                          success.
 * @param[out] fmtpClntCidr CIDR address for the FMTP client. Set only on
 *                          success.
 * @retval     0            Success. The group is being multicast and
 *                          `*smi` and `*fmtpClntCidr` are set.
 * @retval     LDM7_LOGIC   Module is not initialized. `log_add()` called.
 * @retval     LDM7_NOENT   No corresponding potential sender was added via
 *                          `mlsm_addPotentialSender()`. `log_add() called`.
 * @retval     LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
umm_subscribe(
        const feedtypet                     feed,
        const in_addr_t                     clntAddr,
        const VcEndPoint* const restrict    rmtVcEnd,
        const SepMcastInfo** const restrict smi,
        CidrAddr* const restrict            fmtpClntCidr);

/**
 * Handles the termination of a multicast LDM sender process. This function
 * should be called by the top-level LDM server when it notices that a child
 * process has terminated.
 *
 * @param[in] pid          Process-ID of the terminated multicast LDM sender
 *                         process.
 * @retval    0            Success.
 * @retval    LDM7_LOGIC   Module is not already initialized. `log_add()`
 *                         called.
 * @retval    LDM7_NOENT   PID doesn't correspond to known process.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
umm_terminated(const pid_t pid);

/**
 * Returns the process identifier of the associated multicast LDM sender.
 * @retval 0      Multicast LDM sender doesn't exist
 * @return        PID of multicast LDM sender
 * @threadsafety  Safe
 */
pid_t
umm_getSndrPid(void);

/**
 * Releases the IP address reserved for the FMTP TCP connection in a downstream
 * LDM7.
 * @param[in] feed          LDM feed associated with `downFmtpAddr`
 * @param[in] fmtpClntAddr  Address of FMTP client
 * @retval    0             Success
 * @retval    LDM7_INVAL    No multicast LDM sender corresponds to `feed`.
 *                          `log_add()` called.
 * @retval    LDM7_LOGIC    Module is not initialized. `log_add()` called.
 * @retval    LDM7_NOENT    `downFmtpAddr` wasn't previously reserved.
 *                          `log_add()` called.
 * @retval    LDM7_SYSTEM   System failure. `log_add()` called.
 */
Ldm7Status umm_unsubscribe(
        const feedtypet feed,
        const in_addr_t fmtpClntAddr);

/**
 * Clears all entries -- freeing their resources. Doesn't destroy this module or
 * delete the IPC resource. Used for testing.
 */
void
umm_clear(void);

#ifdef __cplusplus
}
#endif

#endif
