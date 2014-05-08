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

#include <errno.h>
#include <rpc.h>

struct UpLdm7Proxy {
    CLIENT* clnt;
    int     sock;
};

/**
 * Returns an Internet TCP socket that's connected to a remote server.
 *
 * @param[in]  hostId        Identifier of the host. May be hostname or
 *                           formatted IP address.
 * @param[in]  port          Port number of the server on the host.
 * @param[out] socket        Pointer to the socket to be set.
 * @param[out] sockAddr      Pointer to the socket address object to be set.
 * @retval     0             Success. \c *socket and \c *sockAddr are set.
 */
static int
getSocket(
    const char* const         hostId,
    const unsigned short      port,
    int* const                socket,
    struct sockaddr_in* const sockAddr)
{
    // TODO
    return -1;
}

/**
 * Returns a client-side RPC handle to an upstream LDM-7. \c log_add() is called
 * for all errors.
 *
 * @param[out] client        Address of pointer to client-side handle. The
 *                           client should call \c clnt_destroy(*client) when it
 *                           is no longer needed.
 * @param[in]  hostId        Identifier of host from which to obtain multicast
 *                           information. May be hostname or formatted IP
 *                           address.
 * @param[in]  port          Port number of server on host to which to connect.
 * @param[out] socket        Pointer to the socket to be set. The client should
 *                           call \c close(*socket) when it's no longer needed.
 * @retval     0             Success. \c *client and \c *sock are set.
 * @retval     ENOMEM        Insufficient memory was available to fulfill the
 *                           request.
 * @retval     EINTR         Execution was interrupted by a signal.
 * @retval     ETIMEDOUT     Timeout occurred.
 * @retval     EAFNOSUPPORT  The address family of \c hostId isn't supported.
 * @retval     EMFILE        No more file descriptors are available for this
 *                           process.
 * @retval     ENFILE        No more file descriptors are available for the
 *                           system.
 * @retval     EACCES        The process does not have appropriate privileges.
 * @retval     ENOBUFS       Insufficient resources were available in the system
 *                           to perform the operation.
 * @retval     EADDRNOTAVAIL The specified address is not available from the
 *                           local machine.
 * @retval     ECONNREFUSED  The target address was not listening for
 *                           connections or refused the connection request.
 * @retval     ENETUNREACH   No route to the network is present.
 * @retval     ECONNRESET    Remote host reset the connection request.
 * @retval     EHOSTUNREACH  The destination host cannot be reached (probably
 *                           because the host is down or a remote router cannot
 *                           reach it).
 * @retval     ENETDOWN      The local network interface used to reach the
 *                           destination is down.
 * @retval     ENOBUFS       No buffer space is available.
 */
static int
newClient(
    CLIENT** const       client,
    const char* const    hostId,
    const unsigned short port,
    int* const           socket)
{
    int             sock;
    struct sockaddr sockAddr;
    int             status = getSocket(hostId, port, &sock, &sockAddr);

    if (status == 0) {
        CLIENT* const clnt = clnttcp_create(&sockAddr, LDMPROG, SEVEN,
                &sock, 0, 0);

        if (clnt == NULL) {
            LOG_SERROR3("Couldn't create RPC client for host \"%s\", "
                    "port %u: %s", hostId, port, clnt_spcreateerror(""));
            (void)close(sock);
            status = errno ? errno : -1; /* ensure non-zero return */
        }
        else {
            *client = clnt;
            *socket = sock;
            status = 0;
        }
    } /* "sock" allocated */

    return status;
}

/**
 * Returns a proxy for an upstream LDM-7. \c log_add() is called for all errors.
 *
 * @param[out] ul7Proxy      Address of pointer to proxy. The client should call
 *                           \c ul7Proxy_delete() when it is no longer needed.
 * @param[in]  hostId        Identifier of host from which to obtain multicast
 *                           information. May be hostname or formatted IP
 *                           address.
 * @param[in]  port          Port number of server on host to which to connect.
 * @retval     0             Success.
 * @retval     ENOMEM        Insufficient memory was available to fulfill the
 *                           request.
 * @retval     EINTR         Execution was interrupted by a signal.
 * @retval     ETIMEDOUT     Timeout occurred.
 * @retval     EAFNOSUPPORT  The address family of \c hostId isn't supported.
 * @retval     EMFILE        No more file descriptors are available for this
 *                           process.
 * @retval     ENFILE        No more file descriptors are available for the
 *                           system.
 * @retval     EACCES        The process does not have appropriate privileges.
 * @retval     ENOBUFS       Insufficient resources were available in the system
 *                           to perform the operation.
 * @retval     EADDRNOTAVAIL The specified address is not available from the
 *                           local machine.
 * @retval     ECONNREFUSED  The target address was not listening for
 *                           connections or refused the connection request.
 * @retval     ENETUNREACH   No route to the network is present.
 * @retval     ECONNRESET    Remote host reset the connection request.
 * @retval     EHOSTUNREACH  The destination host cannot be reached (probably
 *                           because the host is down or a remote router cannot
 *                           reach it).
 * @retval     ENETDOWN      The local network interface used to reach the
 *                           destination is down.
 * @retval     ENOBUFS       No buffer space is available.
 */
int
ul7Proxy_new(
    UpLdm7Proxy** const  ul7Proxy,
    const char* const    hostId,
    const unsigned short port)
{
    CLIENT* clnt;
    int     sock;
    int     status = newClient(&clnt, hostId, port, &sock);

    if (status == 0) {
        UpLdm7Proxy* const proxy = LOG_MALLOC(sizeof(UpLdm7Proxy),
                "upstream LDM-7 proxy");

        if (proxy) {
            proxy->clnt = clnt;
            proxy->sock = sock;
        }
        else {
            status = ENOMEM;
            clnt_destroy(clnt);
            (void)close(sock);
        }
    } /* "clnt" and "sock" allocated */

    return status;
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
    clnt_destroy(ul7Proxy->clnt);
    (void)close(ul7Proxy->sock);
    free(ul7Proxy);
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
