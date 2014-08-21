/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mldm_sender_pids.c
 * @author: Steven R. Emmerson
 *
 * This file implements a singleton mapping from feedtypes to process-IDs of
 * multicast LDM senders. The same mapping is accessible from multiple processes
 * and exists for the duration of the LDM session.
 */

#include "config.h"

#include "ldm.h"

#include <sys/types.h>

/**
 * Returns the process-ID associated with a feed-type.
 *
 * @param[in]  feedtype     Feed-type.
 * @param[out] pid          Associated feed-type.
 * @retval     0            Success. `*pid` is set.
 * @retval     LDM7_NOENT   No PID associated with feed-type.
 * @retval     LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
msp_get(
        const feedtypet feedtype,
        pid_t* const    pid)
{
    // TODO

    return LDM7_NOENT;
}

/**
 * Adds a mapping from a feed-type to a multicast LDM sender process-ID.
 *
 * @param[in] feedtype     Feed-type.
 * @param[in] pid          Multicast LDM sender process-ID.
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
msp_put(
        const feedtypet feedtype,
        const pid_t     pid)
{
    // TODO

    return LDM7_SYSTEM;
}

/**
 * Locks the map.
 *
 * @param[in] exclusive    Lock for exclusive access?
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
msp_lock(
        const bool exclusive)
{
    // TODO

    return LDM7_SYSTEM;
}

/**
 * Unlocks the map.
 *
 * @retval    0            Success.
 * @retval    LDM7_SYSTEM  System failure. `log_add()` called.
 */
Ldm7Status
msp_unlock(void)
{
    // TODO

    return 0;
}
