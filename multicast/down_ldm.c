/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: down_ldm_7.c
 * @author: Steven R. Emmerson
 *
 * This file implements the downstream LDM-7.
 */

#include "config.h"

#include "down_ldm.h"
#include "ldm.h"
#include "log.h"
#include "mcast_down_ldm.h"
#include "request_queue.h"

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
    VcmtpFileId            fileId)
{
    rq_add(requestQueue, fileId);
}

/**
 * Returns multicast information obtained from a server. This is a potentially
 * slow operation.
 *
 * @param[in]  serverId   Identifier of server from which to obtain multicast
 *                        information. May be hostname or IP address.
 * @param[in]  port       Number of port on server to which to connect.
 * @param[in]  mcastName  Name of the multicast group to receive.
 * @param[out] mcastInfo  Multicast group  information obtained from server. Set
 *                        only upon success. The client should call \c
 *                        mcastInfo_free(*mcastInfo) when it is no longer
 *                        needed.
 * @retval     0          Success.
 * @retval     EINTR      A signal was delivered.
 */
static int getMcastInfo(
    const char* const      serverId,
    const unsigned short   port,
    const char* const      mcastName,
    McastGroupInfo** const mcastInfo)
{
    int                   status;
    static const unsigned timeout = 30;

    while (ETIMEDOUT == (status = ul7_getMcastInfo(serverId, port, mcastName,
            timeout, mcastInfo))) {
        if (sleep(timeout))
            return EINTR;
    }
    return status;
}

/**
 * Receives data.
 *
 * @param[in] mcastInfo        Pointer to multicast information.
 * @param[in] missedProdfunc   Pointer to function for receiving notices about
 *                             missed data-products from the multicast
 *                             downstream LDM.
 * @retval    0                Success.
 */
static int execute(
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
 */
int dl7_createAndExecute(
    const char* const    serverId,
    const unsigned short port,
    const char* const    mcastName)
{
    McastGroupInfo* mcastInfo;
    int             status = getMcastInfo(serverId, port, mcastName,
            &mcastInfo);

    if (status == 0) {
        status = execute(mcastInfo, missedProdFunc);
        mcastInfo_free(mcastInfo);
    }

    return status;
}
