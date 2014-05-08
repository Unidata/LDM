/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down7.c
 * @author: Steven R. Emmerson
 *
 * This file implements the downstream LDM-7.
 */

#include "config.h"

#include "down7.h"
#include "ldm.h"
#include "log.h"
#include "mcast_down.h"
#include "request_queue.h"
#include "up_ldm.h"

#include <errno.h>
#include <unistd.h>

/**
 * The queue of requests for files (i.e., data-products) missed by the VCMTP
 * layer.
 */
static RequestQueue* requestQueue;

/**
 * Callback-function for a file (i.e., LDM data-product) that was missed by a
 * multicast downstream LDM. The file is queued for reception by other means.
 * This function returns immediately.
 *
 * @param[in] mdl     Pointer to the multicast downstream LDM that missed the file.
 * @param[in] fileId  VCMTP file identifier of the missed file.
 */
static void missedProdFunc(
    Mdl* const        mdl,
    VcmtpFileId       fileId)
{
    rq_add(requestQueue, fileId);
}

/**
 * Receives data.
 *
 * @param[in] ul7Proxy         Pointer to upstream LDM-7 proxy.
 * @param[in] mcastInfo        Pointer to multicast information.
 * @param[in] missedProdfunc   Pointer to function for receiving notices about
 *                             missed data-products from the multicast
 *                             downstream LDM.
 * @retval    0                Success.
 * @retval    EINTR            Execution was interrupted by a signal.
 * @retval    ETIMEDOUT        Timeout occurred.
 */
static int execute(
    UpLdm7Proxy* const            ul7Proxy,
    const McastGroupInfo* const   mcastInfo,
    const mdl_missed_product_func missedProdFunc)
{
    // TODO
    return -1;
}

/**
 * Creates and executes a downstream LDM-7.
 *
 * @param[in] serverId    Identifier of server from which to obtain multicast
 *                        information. May be hostname or formatted IP address.
 * @param[in] port        Number of port on server to which to connect.
 * @param[in] mcastName   Name of multicast group to receive.
 * @retval    0           Success. All desired data was received.
 * @retval    EINTR       Execution was interrupted by a signal.
 * @retval    ETIMEDOUT   Timeout occurred.
 */
int
dl7_createAndExecute(
    const char* const    serverId,
    const unsigned short port,
    const char* const    mcastName)
{
    UpLdm7Proxy* ul7Proxy;
    int          status = ul7Proxy_new(&ul7Proxy, serverId, port);

    if (!status) {
        McastGroupInfo* mcastInfo;

        status = ul7Proxy_subscribe(ul7Proxy, mcastName, &mcastInfo);

        if (!status) {
            status = execute(ul7Proxy, mcastInfo, missedProdFunc);
            mcastInfo_delete(mcastInfo);
        } /* "mcastInfo" allocated */

        ul7Proxy_delete(ul7Proxy);
    } /* "ul7Proxy" allocated */

    return status;
}
