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
#include "log.h"
#include "mcast_down_ldm.h"
#include "multicast_info.h"
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
 * @param[in] mdl  Pointer to the multicast downstream LDM that missed the file.
 * @param[in] sig  LDM signature (i.e., MD5 checksum) of the missed file.
 */
static void missedProdFunc(
    Mdl* const        mdl,
    signaturet* const sig)
{
    rq_add(requestQueue, sig);
}

/**
 * Returns multicast information obtained from a server.
 *
 * @param[in]  serverId   Identifier of server from which to obtain multicast
 *                        information. May be hostname or IP address.
 * @param[in]  port       Number of port on server to which to connect.
 * @param[in]  feedPat    Feedtype pattern of desired data.
 * @param[out] mcastInfo  Multicast information obtained from server. Set only
 *                        upon success. The client should call \c
 *                        mcastInfo_free(*mcastInfo) when it is no longer
 *                        needed.
 * @retval     0          Success.
 * @retval     EINTR      A signal was delivered.
 */
static int getMulticastInfo(
    const char* const     serverId,
    const unsigned short  port,
    const feedtypet       feedPat,
    MulticastInfo** const mcastInfo)
{
    int                   status;
    static const unsigned timeout = 30;

    while (EAGAIN == (status = ul7_getMulticastInfo(serverId, port, feedPat,
            mcastInfo, timeout))) {
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
    const MulticastInfo* const    mcastInfo,
    const mdl_missed_product_func missedProdFunc)
{
    // TODO
    return -1;
}

/**
 * Creates and executes a downstream LDM-7.
 *
 * @param[in] serverId    Identifier of server from which to obtain multicast
 *                        information. May be hostname or IP address.
 * @param[in] port        Number of port on server to which to connect.
 * @param[in] feedPat     Feedtype pattern of desired data.
 * @retval    0           Success. All desired data was received.
 */
int dl7_createAndExecute(
    const char* const    serverId,
    const unsigned short port,
    const feedtypet      feedPat)
{
    MulticastInfo* mcastInfo;
    int            status = getMulticastInfo(serverId, port, feedPat,
            &mcastInfo);

    if (status == 0) {
        status = execute(mcastInfo, missedProdFunc);
        mcastInfo_free(mcastInfo);
    }

    return status;
}
