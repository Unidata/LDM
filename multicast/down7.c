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
#include "rpcutil.h"
#include "vcmtp_c_api.h"

#include <errno.h>
#include <unistd.h>

typedef struct {
    CLIENT* clnt;
    int     sock;
} Proxy;

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
 * @param[in] mdl     Pointer to the multicast downstream LDM that missed the
 *                    file.
 * @param[in] fileId  VCMTP file identifier of the missed file.
 */
static void missedProdFunc(
    Mdl* const        mdl,
    VcmtpFileId       fileId)
{
    rq_add(requestQueue, fileId);
}

/**
 * Sets a socket address to correspond to a TCP connection to a given port on
 * an Internet host
 *
 * @param[out] sockAddr      Pointer to the socket address object to be set.
 * @param[in]  useIPv6       Whether or not to use IPv6.
 * @param[in]  hostId        Identifier of the host. May be hostname or
 *                           formatted IP address.
 * @param[in]  port          Port number of the server on the host. Must not be
 *                           0.
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

    if (port == 0 || snprintf(servName, sizeof(servName), "%u", port) >=
            sizeof(servName)) {
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
 * @param[in]  port           Port number of the server on the host. Must not be
 *                            0.
 * @param[out] sock           Pointer to the socket to be set. The client should
 *                            call \c close(*sock) when it's no longer needed.
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
 * Returns a client-side RPC handle to a remote LDM-7.
 *
 * @param[out] client         Address of pointer to client-side handle. The
 *                            client should call \c clnt_destroy(*client) when
 *                            it is no longer needed.
 * @param[in]  hostId         Identifier of host from which to obtain multicast
 *                            information. May be hostname or formatted IP
 *                            address.
 * @param[in]  port           Port number of server on host to which to connect.
 *                            Must not be 0.
 * @param[out] socket         Pointer to the socket to be set. The client should
 *                            call \c close(*socket) when it's no longer needed.
 * @retval     0              Success. \c *client and \c *sock are set.
 * @retval     LDM7_INVAL     Invalid port number or host identifier. \c
 *                            log_add() called.
 * @retval     LDM7_REFUSED   Remote LDM-7 refused connection. \c log_add()
 *                            called.
 * @retval     LDM7_RPC       RPC error. \c log_add() called.
 * @retval     LDM7_SYSTEM    System error. \c log_add() called.
 * @retval     LDM7_TIMEDOUT  Connection attempt timed-out. \c log_add() called.
 * @retval     LDM7_UNAUTH    Not authorized. \c log_add() called.
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
         * a pointer to a "struct sockaddr_in6" object may be used if the socket
         * value is non-negative and the port field of the socket address
         * structure is non-zero. Both conditions are true at this point.
         */
        CLIENT* const clnt = clnttcp_create((struct sockaddr_in*)&sockAddr,
                LDMPROG, SEVEN, &sock, 0, 0);

        if (clnt == NULL) {
            LOG_SERROR3("Couldn't create RPC client for host \"%s\", "
                    "port %u: %s", hostId, port, clnt_spcreateerror(""));
            (void)close(sock);
            status = clntStatusToLdm7Status(rpc_createerr.cf_stat);
        }
        else {
            *client = clnt;
            *socket = sock;
        }
    } /* "sock" allocated */

    return status;
}

/**
 * Receives data.
 *
 * @param[in] sock             The socket connected to the remote LDM-7.
 * @param[in] tcpAddr          Address of the TCP server from which to retrieve
 *                             missed data-blocks. May be groupname or formatted
 *                             IP address.
 * @param[in] tcpPort          Port number of the TCP server.
 * @param[in] pq               Pointer to the product-queue.
 * @param[in] mcastInfo        Pointer to multicast information.
 * @param[in] missedProdfunc   Pointer to function for receiving notices about
 *                             missed data-products from the multicast
 *                             downstream LDM.
 * @retval    0                Success.
 * @retval    LDM7_INTR        Execution was interrupted by a signal.
 * @retval    LDM7_TIMEDOUT    Timeout occurred.
 */
static int execute(
    int const                            sock,
    const char* const                    tcpAddr,
    const unsigned short                 tcpPort,
    const McastGroupInfo* const restrict mcastInfo,
    const mdl_missed_product_func        missedProdFunc,
    pqueue* const restrict               pq)
{
    int status = mdl_createAndExecute(tcpAddr, tcpPort, pq, missedProdFunc,
            mcastInfo);
    // TODO
    return -1;
}

/**
 * Subscribes to a multicast group and receives the data.
 *
 * @param[in] clnt           Pointer to the client-side handle.
 * @param[in] sock           The socket connected to the remote LDM-7.
 * @param[in] mcastAddr      Address of the multicast group to receive.
 *                           May be groupname or formatted IP address.
 * @param[in] mcastPort      Port number of the multicast group.
 * @param[in] mcastName      The name of the multicast group to receive.
 * @param[in] pq             Pointer to the product-queue.
 * @retval    0              Success.
 * @retval    LDM7_TIMEDOUT  Timeout occurred. \c log_add() called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). \c
 *                           log_add() called.
 * @retval    LDM7_INVAL     Invalid multicast group name.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 * @retval    LDM7_SYSTEM    System error. \c log_add() called.
 */
static int
subscribeAndExecute(
    CLIENT* const restrict     clnt,
    int const                  sock,
    const char* const          tcpAddr,
    const unsigned short       tcpPort,
    const char* const restrict mcastName,
    pqueue* const restrict     pq)
{
    int                      status;
    SubscriptionReply* const reply = subscribe_7((char*)mcastName, clnt);

    if (reply == NULL) {
	LOG_ADD1("%s", clnt_errmsg(clnt));
	status = clntStatusToLdm7Status(clnt_stat(clnt));
    }
    else {
        if (reply->status == 0) {
            status = execute(sock, tcpAddr, tcpPort,
                    &reply->SubscriptionReply_u.groupInfo, missedProdFunc, pq);
        }
        xdr_free(xdr_SubscriptionReply, (char*)reply);
    } /* "reply" allocated */

    return status;
}

/**
 * Creates and executes a downstream LDM-7.
 *
 * @param[in] hostId         Identifier of host from which to obtain multicast
 *                           information. May be hostname or formatted IP
 *                           address.
 * @param[in] port           Port number of server on host to which to connect.
 * @param[in] mcastName      Name of multicast group to receive.
 * @param[in] pq             Pointer to the product-queue.
 * @retval    0              Success. All desired data was received.
 * @retval    LDM7_INVAL     Invalid port number or host identifier. \c
 *                           log_add() called.
 * @retval    LDM7_REFUSED   Remote LDM-7 refused connection. \c log_add()
 *                           called.
 * @retval    LDM7_TIMEDOUT  Connection attempt timed-out. \c log_add() called.
 * @retval    LDM7_SYSTEM    System error. \c log_add() called.
 * @retval    LDM7_RPC       RPC failure (including interrupt). \c log_add()
 *                           called.
 * @retval    LDM7_INVAL     Invalid multicast group name.
 * @retval    LDM7_UNAUTH    Not authorized to receive multicast group.
 */
int
dl7_createAndExecute(
    const char* const restrict hostId,
    const unsigned short       port,
    const char* const restrict mcastName,
    pqueue* const restrict     pq)
{
    CLIENT* clnt;
    int     sock;
    int     status = newClient(&clnt, hostId, port, &sock);

    if (status == 0) {
        status = subscribeAndExecute(clnt, sock, hostId, port, mcastName, pq);

        clnt_destroy(clnt);
        (void)close(sock);
    } /* "clnt" and "sock" allocated */

    return status;
}
