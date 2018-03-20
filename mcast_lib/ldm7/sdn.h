/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: sdn.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for configuring a Software Defined Network (SDN).
 */

#ifndef MCAST_LIB_LDM7_SDN_H_
#define MCAST_LIB_LDM7_SDN_H_

#include "ldm.h"

#include <stddef.h>

#ifdef __cplusplus
    extern "C" {
#endif

typedef enum {
    SDN_STATUS_OK = 0,
    SDN_STATUS_NETWORK,
    SDN_STATUS_NOMEM
} sdn_status_t;

/**
 * Sets the maximum bit rate for a particular feed. Adjusts the network as
 * necessary to accommodate the new bit rate.
 *
 * @param[in] feed                The feed.
 * @param[in] rate                The maximum bit rate in 1/s.
 * @retval    0                   Success.
 * @retval    SDN_STATUS_NETWORK  Problem configuring the network. log_add()
 *                                called.
 */
sdn_status_t sdn_set_bit_rate(
        const feedtypet feed,
        const uint64_t  rate);

/**
 * Returns the maximum bit rate associated with a particular feed.
 *
 * @param[in]  feed  The feed.
 * @param[out] rate  The maximum bit rate in 1/s.
 * @retval     0     Success. `*rate` is set.
 */
sdn_status_t sdn_get_bit_rate(
        const feedtypet feed,
        uint64_t* const rate);

/**
 * Configures the network to enable a particular remote to receive a feed.
 *
 * @param[in] remote              Specification of the remote.
 * @param[in] feed                The feed.
 * @retval    0                   Success.
 * @retval    SDN_STATUS_NETWORK  Problem configuring the network. log_add()
 *                                called.
 */
sdn_status_t sdn_enable(
        const char* const remote,
        const feedtypet   feed);

/**
 * Returns the remotes that are receiving a particular feed.
 *
 * @param[in]  feed              The feed
 * @param[out] remotes           The list of remotes receiving the feed
 * @param[out] num_remotes       The number of remote.
 * @retval     0                 Success. `*remotes` and `num_remotes` are set.
 *                               Call `sdn_free_remotes(*remotes, *num_remotes)`
 *                               when they're no longer needed.
 * @retval     SDN_STATUS_NOMEM  Out of memory. log_add() called.
 */
sdn_status_t sdn_get_remotes(
        const feedtypet   feed,
        char*** const     remotes,
        size_t* const     num_remotes);

/**
 * Frees a list of remotes.
 *
 * @param[in] remotes      The list of remotes.
 * @param[in] num_remotes  The number of remotes.
 */
void sdn_free_remotes(
        char* const* const remotes,
        const size_t       num_remotes);

/**
 * Configures the network to disallow a particular remote from receiving a feed.
 *
 * @param[in] remote              Specification of the remote.
 * @param[in] feed                The feed.
 * @retval    0                   Success.
 * @retval    SDN_STATUS_NETWORK  Problem configuring the network. log_add()
 *                                called.
 */
sdn_status_t sdn_disable(
        const char* const remote,
        const feedtypet   feed);

#ifdef __cplusplus
    }
#endif

#endif /* MCAST_LIB_LDM7_SDN_H_ */
