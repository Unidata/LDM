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

#include "ldm.h"
#include "up_ldm.h"
#include "log.h"
#include "mcast_info.h"
#include "rpcutil.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <rpc.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

struct UpLdm7Proxy {
    CLIENT* clnt;
    int     sock;
};

/**
 * Sets a socket address to correspond to a TCP connection to a given port on
 * an Internet host
 *
 * @param[out] sockAddr      Pointer to the socket address object to be set.
 * @param[in]  useIPv6       Whether or not to use IPv6.
 * @param[in]  hostId        Identifier of the host. May be hostname or
 *                           formatted IP address.
 * @param[in]  port          Port number of the server on the host.
 * @retval     0             Success. \c *sockAddr is set.
 * @retval     LDM7_INVAL    Invalid port number or host identifier. \c
 *                           log_add() called.
 * @retval     LDM7_IPV6     IPv6 not supported. \c log_add() called.
 * @retval     LDM7_SYSTEM   System error. \c log_add() called.
 */
static int
getSockAddr(
    struct sockaddr* const restrict sockAddr,
    const int                       useIPv6,
    const char* const restrict      hostId,
    const unsigned short            port)
{
    int  status = 0; /* success */
    char servName[6];

    if (snprintf(servName, sizeof(servName), "%u", port) >= sizeof(servName)) {
        LOG_ADD1("Invalid port number: %u", port);
        status = LDM7_INVAL;
    }
    else {
        struct addrinfo  hints;
        struct addrinfo* addrInfo;

        (void)memset(&hints, 0, sizeof(hints));
        hints.ai_family = useIPv6 ? AF_INET6 : AF_INET;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICSERV | AI_ADDRCONFIG;

        status = getaddrinfo(hostId, servName, &hints, &addrInfo);

        if (status != 0) {
            /*
             * Possible values: EAI_FAMILY, EAI_AGAIN, EAI_FAIL, EAI_MEMORY,
             * EIA_NONAME, EAI_SYSTEM, EAI_OVERFLOW
             */
            LOG_ADD4("Couldn't get %s address for host \"%s\", port %u. "
                    "Status=%d", useIPv6 ? "IPv6" : "IPv4", hostId, port,
                    status);
            status = (useIPv6 && status == EAI_FAMILY)
                    ? LDM7_IPV6
                    : (status == EAI_NONAME)
                      ? LDM7_INVAL
                      : LDM7_SYSTEM;
        }
        else {
            *sockAddr = *addrInfo->ai_addr;
            freeaddrinfo(addrInfo);
        } /* "addrInfo" allocated */
    } /* valid port number */

    return status;
}

/**
 * Returns a socket that's connected to an Internet server via TCP.
 *
 * @param[in]  useIPv6        Whether or not to use IPv6.
 * @param[in]  hostId         Identifier of the host. May be hostname or
 *                            formatted IP address.
 * @param[in]  port           Port number of the server on the host.
 * @param[out] sock           Pointer to the socket to be set.
 * @param[out] sockAddr       Pointer to the socket address object to be set.
 * @retval     0              Success. \c *sock and \c *sockAddr are set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier. \c
 *                            log_add() called.
 * @retval     LDM7_IPV6      IPv6 not supported. \c log_add() called.
 * @retval     LDM7_REFUSED   Remote LDM-7 refused connection. \c log_add()
 *                            called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. \c log_add()
 *                            called.
 * @retval     LDM7_SYSTEM    System error. \c log_add() called.
 */
static int
getSocket(
    const int                       useIPv6,
    const char* const restrict      hostId,
    const unsigned short            port,
    int* const restrict             sock,
    struct sockaddr* const restrict sockAddr)
{
    struct sockaddr addr;
    int             status = getSockAddr(&addr, useIPv6, hostId, port);

    if (status == 0) {
        const char* const addrFamilyId = useIPv6 ? "IPv6" : "IPv4";
        const int         fd = socket(addr.sa_family, SOCK_STREAM, IPPROTO_TCP);

        if (fd == -1) {
            LOG_SERROR1("Couldn't create %s TCP socket", addrFamilyId);
            status = (useIPv6 && errno == EAFNOSUPPORT)
                    ? LDM7_IPV6
                    : LDM7_SYSTEM;
        }
        else {
            if (connect(fd, &addr, sizeof(addr))) {
                LOG_SERROR3("Couldn't connect %s TCP socket to host "
                        "\"%s\", port %u", addrFamilyId, hostId, port);
                (void)close(fd);
                status = (errno == ETIMEDOUT)
                        ? LDM7_TIMEDOUT
                        : (errno == ECONNREFUSED)
                          ? LDM7_REFUSED
                          : LDM7_SYSTEM;
            }
            else {
                *sock = fd;
                *sockAddr = addr;
            }
        } /* "fd" allocated */
    } /* "addr" is set */

    return status;
}

/**
 * Returns a client-side RPC handle to an upstream LDM-7.
 *
 * @param[out] client         Address of pointer to client-side handle. The
 *                            client should call \c clnt_destroy(*client) when
 *                            it is no longer needed.
 * @param[in]  hostId         Identifier of host from which to obtain multicast
 *                            information. May be hostname or formatted IP
 *                            address.
 * @param[in]  port           Port number of server on host to which to connect.
 * @param[out] socket         Pointer to the socket to be set. The client should
 *                            call \c close(*socket) when it's no longer needed.
 * @retval     0              Success. \c *client and \c *sock are set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier. \c
 *                            log_add() called.
 * @retval     LDM7_REFUSED   Remote LDM-7 refused connection. \c log_add()
 *                            called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. \c log_add()
 *                            called.
 * @retval     LDM7_SYSTEM    System error. \c log_add() called.
 */
static int
newClient(
    CLIENT** const restrict    client,
    const char* const restrict hostId,
    const unsigned short       port,
    int* const restrict        socket)
{
    int             sock;
    struct sockaddr sockAddr;
    /* Try IPv6 first */
    int             status = getSocket(1, hostId, port, &sock, &sockAddr);

    if (status == LDM7_IPV6) {
        /* Try IPv4 */
        status = getSocket(0, hostId, port, &sock, &sockAddr);
    }

    if (status == 0) {
        /*
         * "clnttcp_create()" expects a pointer to a "struct sockaddr_in", but
         * if the "sock" argument is non-negative and the port field of the
         * socket address structure is non-zero, then a "struct sockaddr_in6"
         * object may be passed-in.
         */
        CLIENT* const clnt = clnttcp_create((struct sockaddr_in*)&sockAddr,
                LDMPROG, SEVEN, &sock, 0, 0);

        if (clnt == NULL) {
            LOG_SERROR3("Couldn't create RPC client for host \"%s\", "
                    "port %u: %s", hostId, port, clnt_spcreateerror(""));
            (void)close(sock);
            status = (errno == ETIMEDOUT)
                    ? LDM7_TIMEDOUT
                    : (errno == ECONNREFUSED)
                      ? LDM7_REFUSED
                      : LDM7_SYSTEM;
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
 * Returns a proxy for an upstream LDM-7.
 *
 * @param[out] ul7Proxy       Address of pointer to proxy. The client should
 *                            call \c ul7Proxy_delete() when it's no longer
 *                            needed.
 * @param[in]  hostId         Identifier of host from which to obtain multicast
 *                            information. May be hostname or formatted IP
 *                            address.
 * @param[in]  port           Port number of server on host to which to connect.
 * @retval     0              Success.
 * @retval     LDM7_INVAL     Invalid port number or host identifier. \c
 *                            log_add() called.
 * @retval     LDM7_REFUSED   Remote LDM-7 refused connection. \c log_add()
 *                            called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. \c log_add()
 *                            called.
 * @retval     LDM7_SYSTEM    System error. \c log_add() called.
 */
int
ul7Proxy_new(
    UpLdm7Proxy** const restrict ul7Proxy,
    const char* const restrict   hostId,
    const unsigned short         port)
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
            status = LDM7_SYSTEM;
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
 * @param[in]  proxy             Pointer to the proxy for the upstream LDM-7.
 * @param[in]  mcastName         Name of the multicast group to receive.
 * @param[out] mcastInfo         Multicast information obtained from server. Set
 *                               only upon success. The client should call
 *                               @code{xdr_free(xdr_McastGroupInfo, mcastInfo)}
 *                               when it is no longer needed.
 * @retval     0                 Success.
 * @retval     UP_LDM7_TIMEDOUT  Timeout occurred. \c log_add() called.
 * @retval     UP_LDM7_RPC       RPC failure (including interrupt). \c
 *                               log_add() called.
 * @retval     UP_LDM7_INVAL     Invalid multicast group name.
 * @retval     UP_LDM7_UNAUTH    Not authorized to receive multicast group.
 * @retval     LDM7_SYSTEM       System error. \c log_add() called.
 */
int ul7Proxy_subscribe(
    UpLdm7Proxy* const restrict     proxy,
    const char* const restrict      mcastName,
    McastGroupInfo* const restrict  mcastInfo)
{
    int                      status;
    SubscriptionReply* const reply = subscribe_7((char*)mcastName, proxy->clnt);

    if (reply == NULL) {
	LOG_ADD1("%s", clnt_errmsg(proxy->clnt));
        status = clnt_stat(proxy->clnt);
	status = (status == RPC_TIMEDOUT)
            ? LDM7_TIMEDOUT
            : (status == RPC_SYSTEMERROR)
                ? LDM7_SYSTEM
                : LDM7_RPC;
    }
    else {
        if (reply->status == 0) {
            status = mcastInfo_copy(mcastInfo,
                    &reply->SubscriptionReply_u.groupInfo);
        }
        xdr_free(xdr_SubscriptionReply, (char*)reply);
    } /* "reply" allocated */

    return status;
}
