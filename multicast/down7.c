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
#include "mcast_info.h"
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
 * @param[in] serverId      Identifier of server from which to obtain multicast
 *                          information. May be hostname or formatted IP
 *                          address.
 * @param[in] port          Number of port on server to which to connect.
 * @param[in] mcastName     Name of multicast group to receive.
 * @retval    0             Success. All desired data was received.
 * @retval    ENOMEM        Insufficient memory was available to fulfill the
 *                          request.
 * @retval    EINTR         Execution was interrupted by a signal.
 * @retval    ETIMEDOUT     Timeout occurred.
 * @retval    EAFNOSUPPORT  The address family of \c serverId isn't supported.
 * @retval    EMFILE        No more file descriptors are available for this
 *                          process.
 * @retval    ENFILE        No more file descriptors are available for the
 *                          system.
 * @retval    EACCES        The process does not have appropriate privileges.
 * @retval    ENOBUFS       Insufficient resources were available in the system
 *                          to perform the operation.
 * @retval    EADDRNOTAVAIL The specified address is not available from the
 *                          local machine.
 * @retval    ECONNREFUSED  The target address was not listening for
 *                          connections or refused the connection request.
 * @retval    ENETUNREACH   No route to the network is present.
 * @retval    ECONNRESET    Remote host reset the connection request.
 * @retval    EHOSTUNREACH  The destination host cannot be reached (probably
 *                          because the host is down or a remote router cannot
 *                          reach it).
 * @retval    ENETDOWN      The local network interface used to reach the
 *                          destination is down.
 * @retval    ENOBUFS       No buffer space is available.
 */
int
dl7_createAndExecute(
    const char* const    serverId,
    const unsigned short port,
    const char* const    mcastName)
{
    UpLdm7Proxy* ul7Proxy;
    int          status = ul7Proxy_new(&ul7Proxy, serverId, port);

    if (status == 0) {
        McastGroupInfo mcastInfo;

        status = ul7Proxy_subscribe(ul7Proxy, mcastName, &mcastInfo);

        if (status == 0) {
            status = execute(ul7Proxy, &mcastInfo, missedProdFunc);
            xdr_free(xdr_McastGroupInfo, (char*)&mcastInfo);
        } /* "mcastInfo" allocated */

        ul7Proxy_delete(ul7Proxy);
    } /* "ul7Proxy" allocated */

    return status;
}
