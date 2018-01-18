/**
 * This file declares a manager of pools of IPv4 addresses.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *        File: InAddrMgr.h
 *  Created on: Jan 8, 2018
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___INADDRMGR_H_
#define MCAST_LIB_C___INADDRMGR_H_

#include "ldm.h"

#include <stdbool.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Adds a pool of addresses for a feed. Overwrites any previously-existing
 * pool for the same feed. The pool will be shared by all child processes and
 * deleted when the current process terminates normally or `inam_clear()` is
 * called.
 * @param[in] feed           LDM feed specification
 * @param[in] networkPrefix  Network prefix (e.g., `192.168.128.0`) in network
 *                           byte-order
 * @param[in] prefixLen      Number of bits in network prefix (e.g., 17 for the
 *                           example network prefix)
 * @retval    0              Success
 * @retval    EINVAL         `prefixLen >= 31`
 * @retval    EINVAL         `networkPrefix` and `prefixLen` are incompatible
 * @throw     ENOENT         Couldn't get user-name
 * @retval    ENOMEM         System error
 * @threadsafety             Compatible but not safe
 */
int inam_add(
        const feedtypet      feed,
        const struct in_addr networkPrefix,
        const unsigned       prefixLen);

/**
 * Reserves an address from the pool created by the previous call to `add()`.
 * The address will be unique and not previously reserved. The reservation will
 * be visible to all child processes.
 * @param[in]  feed     LDM feed specification
 * @param[out] addr     Available address in network byte-order
 * @retval     0        Success. `*addr` is set.
 * @retval     ENOENT   No address pool corresponding to `feed`
 * @retval     EMFILE   All addresses have been reserved
 * @threadsafety        Compatible but not safe
 */
int inam_reserve(
        const feedtypet feed,
        struct in_addr* addr);

/**
 * Releases an address in the pool created by the previous call to `add()` so
 * that it can be subsequently re-used. The release will be visible to all child
 * processes.
 * @param[in] feed    LDM feed specification
 * @param[in] addr    Address in network byte order to be returned and made
 *                    available
 * @retval    0       Success
 * @retval    ENOENT  No address pool corresponding to `feed`
 * @retval    ENOENT  `addr` wasn't reserved
 * @threadsafety      Compatible but not safe
 */
int inam_release(
        const feedtypet       feed,
        const struct in_addr* addr);

/**
 * Clears all address pools. Deletes all IPC objects if the current process is
 * the one that created them (i.e., made the calls to `inam_add()`). This
 * function is implicitly called when the current process terminates normally.
 */
void inam_clear();

#ifdef __cplusplus
}
#endif

#endif /* MCAST_LIB_C___INADDRMGR_H_ */
