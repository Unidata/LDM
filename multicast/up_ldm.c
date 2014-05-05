/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: up_ldm_7.c
 * @author: Steven R. Emmerson
 *
 * This file implements the upstream LDM-7.
 */

#include "config.h"

#include "up_ldm.h"
#include "log.h"
#include "mcast_info.h"

/**
 * Returns multicast information obtained from a remote server. This function
 * acts as a proxy for the remote server.
 *
 * @param[in]  serverId   Identifier of remote server from which to obtain
 *                        multicast information. May be hostname or formatted IP
 *                        address.
 * @param[in]  port       Number of port on server to which to connect.
 * @param[in]  mcastName  Name of the multicast group to receive.
 * @param[in]  timeout    Timeout parameter in seconds.
 * @param[out] mcastInfo  Multicast information obtained from server. Set only
 *                        upon success. The client should call \c
 *                        mcastInfo_free(*mcastInfo) when it is no longer
 *                        needed.
 * @retval     0          Success.
 * @retval     ETIMEDOUT  The timeout lapsed.
 */
int ul7_getMcastInfo(
    const char* const      serverId,
    const unsigned short   port,
    const char* const      mcastName,
    const unsigned         timeout,
    McastGroupInfo** const mcastInfo)
{
    // TODO
    return -1;
}
