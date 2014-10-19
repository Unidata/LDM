/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
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
 * @param[in] info         Information on the multicast group.
 * @retval    0            Success.
 * @retval    LDM7_DUP     Multicast group information conflicts with earlier
 *                         addition. Manager not modified. `log_add()` called.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
mlsm_addPotentialSender(
    const McastInfo* const restrict   info);

/**
 * Ensures that the multicast LDM sender process that's responsible for a
 * particular multicast group is running. Doesn't block.
 *
 * @param[in]  feedtype     Multicast group feed-type.
 * @param[out] mcastInfo    Information on corresponding multicast group.
 * @retval     0            Success. The group is being multicast and
 *                          `*mcastInfo` is set.
 * @retval     LDM7_NOENT   No corresponding potential sender was added via
 *                          `mlsm_addPotentialSender()`. `log_start() called`.
 * @retval     LDM7_SYSTEM  System error. `log_start()` called.
 */
Ldm7Status
mlsm_ensureRunning(
        const feedtypet   feedtype,
        McastInfo** const mcastInfo);

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

#ifdef __cplusplus
}
#endif

#endif
