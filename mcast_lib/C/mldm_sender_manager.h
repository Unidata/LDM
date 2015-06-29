/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_sender.hin
 * @author: Steven R. Emmerson
 *
 * This file specifies the API for the multicast upstream LDM.
 */

#ifndef MLDM_SENDER_MANAGER_H
#define MLDM_SENDER_MANAGER_H

#include "ldm.h"
#include "pq.h"
#include "mcast.h"

#include <sys/types.h>

typedef struct mul               Mul;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Adds a potential multicast LDM sender. The sender is not started. This
 * function should be called for all potential senders before any child
 * process is forked so that all child processes will have this information.
 *
 * @param[in] info         Information on the multicast group. Caller may free.
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
 * @param[in] mcastIf      IP address of the interface from which multicast
 *                         packets should be sent or NULL to have them sent from
 *                         the system's default multicast interface. Caller may
 *                         free.
 * @param[in] pqPathname   Pathname of product-queue. Caller may free.
 * @retval    0            Success.
 * @retval    LDM7_INVAL   Invalid argument. `log_add()` called.
 * @retval    LDM7_DUP     Multicast group information conflicts with earlier
 *                         addition. Manager not modified. `log_add()` called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
mlsm_addPotentialSender(
    const McastInfo* const restrict   info,
    const unsigned short              ttl,
    const char* const restrict        mcastIf,
    const char* const restrict        pqPathname);

/**
 * Ensures that the multicast LDM sender process that's responsible for a
 * particular multicast group is running. Doesn't block.
 *
 * @param[in]  feedtype     Multicast group feed-type.
 * @param[out] mcastInfo    Information on corresponding multicast group.
 * @param[out] pid          Process ID of the multicast LDM sender.
 * @retval     0            Success. The group is being multicast and
 *                          `*mcastInfo` is set.
 * @retval     LDM7_NOENT   No corresponding potential sender was added via
 *                          `mlsm_addPotentialSender()`. `log_start() called`.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
Ldm7Status
mlsm_ensureRunning(
        const feedtypet         feedtype,
        const McastInfo** const mcastInfo,
        pid_t* const            pid);

/**
 * Handles the termination of a multicast LDM sender process. This function
 * should be called by the top-level LDM server when it notices that a child
 * process has terminated.
 *
 * @param[in] pid          Process-ID of the terminated multicast LDM sender
 *                         process.
 * @retval    0            Success.
 * @retval    LDM7_NOENT   PID doesn't correspond to known process.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
mlsm_terminated(
        const pid_t pid);

/**
 * Clears all entries.
 *
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System error. `log_add()` called.
 */
Ldm7Status
mlsm_clear(void);

#ifdef __cplusplus
}
#endif

#endif
