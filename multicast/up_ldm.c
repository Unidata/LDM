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

#include <rpc.h>

struct UpLdm7Proxy {
    CLIENT* clnt;
};


/**
 * Returns a proxy for an upstream LDM-7.
 *
 * @param[out] ul7Proxy  Address of pointer to proxy.
 * @param[in]  serverId  Identifier of server from which to obtain multicast
 *                       information. May be hostname or formatted IP address.
 * @param[in]  port      Number of port on server to which to connect.
 * @retval     0         Success.
 * @retval    EINTR      Execution was interrupted by a signal.
 * @retval    ETIMEDOUT  Timeout occurred.
 */
int
ul7Proxy_new(
    UpLdm7Proxy** const  ul7Proxy,
    const char* const    serverId,
    const unsigned short port)
{
    // TODO
    return -1;
}

/**
 * Frees the resources of a proxy for an upstream LDM-7.
 *
 * @param[in] ul7Proxy  Pointer to the upstream LDM-7 proxy to have its
 *                      resources released.
 */
void
ul7Proxy_delete(
    UpLdm7Proxy* const ul7Proxy)
{
    // TODO
}

/**
 * Subscribes to a multicast group of an upstream LDM-7.
 *
 * @param[in,out] proxy      Pointer to the proxy for the upstream LDM-7.
 * @param[in]     mcastName  Name of the multicast group to receive.
 * @param[out]    mcastInfo  Multicast information obtained from server. Set
 *                           only upon success. The client should call \c
 *                           mcastInfo_delete(*mcastInfo) when it is no longer
 *                           needed.
 * @retval        0          Success.
 * @retval        EINTR      Execution was interrupted by a signal.
 * @retval        ETIMEDOUT  Timeout occurred.
 */
int ul7Proxy_subscribe(
    UpLdm7Proxy* const     proxy,
    const char* const      mcastName,
    McastGroupInfo** const mcastInfo)
{
    // TODO
    return -1;
}
