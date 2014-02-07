/**
 * Copyright 2014 University Corporation for Atmospheric Research.
 * All rights reserved. See file COPYRIGHT in the top-level source-directory
 * for legal conditions.
 *
 * @file sock_funcs.c
 *
 * Utility module of socket functions.
 * @par
 * Examples:
 *      Error-handling is omitted from the examples for concision.
 *      @par
 *      Create a blocking socket for sending IPv4 multicast packets on the local
 *      subnet using port 388000 and the default multicast interface. The
 *      packets will not appear on the loopback interface and no other process
 *      will be able to send to that address.
 *      @code
 *          #include <arpa/inet.h>
 *          #include <sock_funcs.h>
 *          ...
 *          int sock = sf_create_multicast(inet_addr("224.1.1.1"), 38800);
 *          (void)sf_set_time_to_live(sock, 1);
 *      @endcode
 *      @par
 *      Open a non-blocking socket for receiving IPv4 multicast packets on port
 *      38800 on a specific interface:
 *      @code
 *           #include <arpa/inet.h>
 *           #include <sock_funcs.h>
 *           ...
 *           in_addr_t addr = inet_addr("224.1.1.1");
 *           int sock = sf_open_multicast(addr, 38800);
 *           (void)sf_set_nonblocking(
 *           (void)sf_add_multicast_group(sock, addr, inet_addr("128.117.156.30"));
 *      @endcode
 */

#include "config.h"

#include "log.h"
#include "sock_funcs.h"

#include <unistd.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

/**
 * Prints the formatted representation of a binary IPv4 address into a buffer.
 *
 * @param[out] buf      The output buffer for the formatted string. It should
 *                      be at least INET_ADDRSTRLEN bytes in size.
 * @param[in]  size     The size of the buffer in bytes.
 * @param[in]  addr     The IPv4 address in network byte order.
 * @return              Pointer to the buffer.
 */
static char* ipaddr_print(
    char* const         buf,
    const size_t        size,
    const in_addr_t     addr)
{
    struct in_addr in_addr;

    in_addr.s_addr = addr;
    (void)inet_ntop(AF_INET, &in_addr, buf, size);

    return buf;
}

/**
 * Returns the formatted representation of a binary IPv4 address.
 *
 * @param[in] addr      The IPv4 address in network byte order.
 * @retval    !NULL     Pointer to the string representation of the IPv4 address.
 *                      The client should free when it's no longer needed.
 * @retval    NULL      The address couldn't be formatted. "errno" will be
 *                      ENOMEM.
 */
static char* ipaddr_format(
    const in_addr_t addr)
{
    char* const buf = LOG_MALLOC(INET_ADDRSTRLEN, "IP address buffer");

    if (buf != NULL)
        (void)ipaddr_print(buf, INET_ADDRSTRLEN, addr);

    return buf;
}

/**
 * Returns the formatted representation of a binary IPv4 socket address (IP
 * address and port number).
 *
 * @param[in] sockaddr  IPv4 socket address.
 * @retval    !NULL     String representation of the socket address. The client
 *                      should free when it's no longer needed.
 * @retval    NULL      The socket address couldn't be formatted. "errno" will
 *                      be ENOMEM.
 */
static char* sockaddr_format(
    const struct sockaddr_in* const     sockaddr)
{
    const size_t        bufsize = INET_ADDRSTRLEN + 7;
    char* const         buf = LOG_MALLOC(bufsize, "socket address buffer");

    if (buf != NULL) {
        size_t          len;

        (void)ipaddr_print(buf, bufsize, sockaddr->sin_addr.s_addr);
        len = strlen(buf);
        (void)snprintf(buf+len, bufsize-len, ":%d", ntohs(sockaddr->sin_port));
    }

    return buf;
}

/**
 * Sets whether packets written to a multicast socket are received on the
 * loopback interface.
 *
 * @param[in] sock      The socket.
 * @param[in] loop      Whether or not packets should be received on the
 *                      loopback interface.
 * @retval    0         Success.
 * @retval    -1        Failure. @code{log_add()} called. "errno" will be one
 *                      of the following:
 *                          EACCES      The process does not have appropriate
 *                                      privileges.
 *                          ENOBUFS     Insufficient resources were available
 */
int sf_set_loopback_reception(
    const int   sock,
    const int   loop)
{
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop))) {
        LOG_SERROR2("Couldn't %s loopback reception of multicast packets sent "
                "on socket %d", loop ? "enable" : "disable", sock);
        return -1;
    }
    return 0;
}

/**
 * Sets the time-to-live for multicast packets written to a socket.
 *
 * @param[in] sock      The socket.
 * @param[in] ttl       Time-to-live of outgoing packets:
 *                           0       Restricted to same host. Won't be output
 *                                   by any interface.
 *                           1       Restricted to the same subnet. Won't be
 *                                   forwarded by a router.
 *                         <32       Restricted to the same site, organization
 *                                   or department.
 *                         <64       Restricted to the same region.
 *                        <128       Restricted to the same continent.
 *                        <255       Unrestricted in scope. Global.
 * @retval    0         Success.
 * @retval    -1        Failure. @code{log_add()} called. "errno" will be one
 *                      of the following:
 *                          EACCES      The process does not have appropriate
 *                                      privileges.
 *                          ENOBUFS     Insufficient resources were available
 */
int sf_set_time_to_live(
    const int           sock,
    const unsigned char ttl)
{
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl))) {
        LOG_SERROR2("Couldn't set time-to-live for multicast packets on socket "
                "%d to %u", sock, ttl);
        return -1;
    }
    return 0;
}

/**
 * Sets the interface that a socket uses.
 *
 * @param[in] sock       The socket.
 * @param[in] ifaceAddr  IPv4 address of interface in network byte order. 0
 *                       means the default interface.
 * @retval    0          Success.
 * @retval    -1         Failure. @code{log_add()} called. "errno" will be one
 *                       of the following:
 *                          EACCES      The process does not have appropriate
 *                                      privileges.
 *                          ENOBUFS     Insufficient resources were available
 */
int sf_set_interface(
    const int           sock,
    const in_addr_t     ifaceAddr)
{
    struct in_addr  addr;

    addr.s_addr = ifaceAddr == 0 ? htonl(INADDR_ANY) : ifaceAddr;

    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &addr, sizeof(addr))) {
        char    buf[INET_ADDRSTRLEN];

        LOG_SERROR2("Couldn't set outgoing IPv4 multicast interface "
                "to %s for socket %d",
                inet_ntop(AF_INET, &addr, buf, INET_ADDRSTRLEN), sock);
        return -1;
    }
    return 0;
}

/**
 * Sets the blocking-mode of a socket.
 *
 * @param[in] sock      The socket.
 * @param[in] nonblock  Whether or not the socket should be in non-blocking
 *                      mode.
 * @retval    0         Success.
 * @retval    -1        Failure. @code{log_add()} called. "errno" will be one
 *                      of the following:
 *                          EACCES      The process does not have appropriate
 *                                      privileges.
 *                          ENOBUFS     Insufficient resources were available
 */
int sf_set_nonblocking(
    const int           sock,
    const int           nonblock)
{
    int flags = fcntl(sock, F_GETFL);

    if (flags == -1) {
        LOG_SERROR1("Couldn't get status flags of socket %d", sock);
        return -1;
    }
    if (fcntl(sock, F_SETFL,
            nonblock ? (flags | O_NONBLOCK) : (flags | ~O_NONBLOCK))) {
        LOG_SERROR2("Couldn't set socket %d to %s", sock,
                nonblock ? "non-blocking" : "blocking");
        return -1;
    }
    return 0;
}

/**
 * Sets whether or not the multicast address of a socket can be used by other
 * processes.
 *
 * @param[in] sock       The socket.
 * @param[in] reuseAddr  Whether or not to reuse the multicast address (i.e.,
 *                       whether or not multiple processes on the same host can
 *                       receive packets from the same multicast group).
 * @retval    0          Success.
 * @retval    -1         Failure. @code{log_add()} called. "errno" will be one
 *                       of the following:
 *                          EACCES      The process does not have appropriate
 *                                      privileges.
 *                          ENOBUFS     Insufficient resources were available
 */
int sf_set_address_reuse(
    const int           sock,
    const int           reuseAddr)
{
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuseAddr,
            sizeof(reuseAddr))) {
        LOG_SERROR2("Couldn't %s reuse of multicast address on socket %d",
                reuseAddr ? "enable" : "disable", sock);
        return -1;
    }
    return 0;
}

/**
 * Returns a multicast socket.
 *
 * @param[in] mIpAddr    IPv4 address of multicast group in network byte order:
 *                          224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                      purposes
 *                          224.0.1.0 - 238.255.255.255 User-defined multicast
 *                                                      addresses
 *                          239.0.0.0 - 239.255.255.255 Reserved for
 *                                                      administrative scoping
 * @param[in] port       Port number of multicast group.
 * @param[in] create     Whether or not to create a new multicast group or
 *                       open a connection to an existing one.
 * @retval    >=0        The multicast socket.
 * @retval    -1         Failure. @code{log_add()} called. "errno" will be one
 *                       of the following:
 *                          EACCES        The process does not have appropriate
 *                                        privileges.
 *                          EACCES        Write access to the named socket is
 *                                        denied.
 *                          EADDRINUSE    Attempt to establish a connection that
 *                                        uses addresses that are already in
 *                                        use.
 *                          EADDRNOTAVAIL The specified address is not
 *                                        available from the local machine.
 *                          EAFNOSUPPORT  The specified address is not a valid
 *                                        address for the address family of the
 *                                        specified socket.
 *                          EINTR         The attempt to establish a connection
 *                                        was interrupted by delivery of a
 *                                        signal that was caught; the
 *                                        connection shall be established
 *                                        asynchronously.
 *                          EMFILE        No more file descriptors are available
 *                                        for this process.
 *                          ENETDOWN      The local network interface used to
 *                                        reach the destination is down.
 *                          ENETUNREACH   No route to the network is present.
 *                          ENFILE        No more file descriptors are available
 *                                        for the system.
 *                          ENOBUFS       Insufficient resources were available
 *                                        in the system.
 *                          ENOBUFS       No buffer space is available.
 *                          ENOMEM        Insufficient memory was available.
 * @see sf_set_loopback_reception(int sock, int loop)
 * @see sf_set_time_to_live(int sock, unsigned char ttl)
 * @see sf_set_interface(int sock, in_addr_t ifaceAddr)
 * @see sf_set_nonblocking(int sock, int nonblocking)
 * @see sf_set_address_reuse(int sock, int reuse)
 */
static int create_or_open_multicast(
    const in_addr_t             mIpAddr,
    const unsigned short        port,
    const int                   create)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (sock == -1) {
        LOG_SERROR0("Couldn't create UDP socket");
    }
    else {
        struct sockaddr_in      addr;

        (void)memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = mIpAddr;
        addr.sin_port = htons(port);

        if ((create ? connect : bind)(sock, (struct sockaddr*) &addr,
                sizeof(addr)) == -1) {
            char* const mcastAddrString = sockaddr_format(&addr);

            LOG_SERROR3("Couldn't %s socket %d to multicast group %s",
                    create ? "connect" : "bind", sock, mcastAddrString);
            free(mcastAddrString);
            (void)close(sock);
            sock = -1;
        }
    }

    return sock;
}

/**
 * Returns a socket for sending multicast packets.
 *
 * @param[in] mIpAddr    IPv4 address of multicast group in network byte order:
 *                          224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                      purposes
 *                          224.0.1.0 - 238.255.255.255 User-defined multicast
 *                                                      addresses
 *                          239.0.0.0 - 239.255.255.255 Reserved for
 *                                                      administrative scoping
 * @param[in] port       Port number used for the destination multicast group.
 * @retval    >=0        The created multicast socket.
 * @retval    -1         Failure. @code{log_add()} called. "errno" will be one
 *                       of the following:
 *                          EACCES        The process does not have appropriate
 *                                        privileges.
 *                          EACCES        Write access to the named socket is
 *                                        denied.
 *                          EADDRINUSE    Attempt to establish a connection that
 *                                        uses addresses that are already in
 *                                        use.
 *                          EADDRNOTAVAIL The specified address is not
 *                                        available from the local machine.
 *                          EAFNOSUPPORT  The specified address is not a valid
 *                                        address for the address family of the
 *                                        specified socket.
 *                          EINTR         The attempt to establish a connection
 *                                        was interrupted by delivery of a
 *                                        signal that was caught; the
 *                                        connection shall be established
 *                                        asynchronously.
 *                          EMFILE        No more file descriptors are available
 *                                        for this process.
 *                          ENETDOWN      The local network interface used to
 *                                        reach the destination is down.
 *                          ENETUNREACH   No route to the network is present.
 *                          ENFILE        No more file descriptors are available
 *                                        for the system.
 *                          ENOBUFS       Insufficient resources were available
 *                                        in the system.
 *                          ENOBUFS       No buffer space is available.
 *                          ENOMEM        Insufficient memory was available.
 * @see sf_set_loopback_reception(int sock, int loop)
 * @see sf_set_time_to_live(int sock, unsigned char ttl)
 * @see sf_set_interface(int sock, in_addr_t ifaceAddr)
 * @see sf_set_nonblocking(int sock, int nonblocking)
 * @see sf_set_address_reuse(int sock, int reuse)
 */
int sf_create_multicast(
    const in_addr_t             mIpAddr,
    const unsigned short        port)
{
    return create_or_open_multicast(mIpAddr, port, 1);
}

/**
 * Returns a socket for receiving multicast packets. The socket will not receive
 * any multicast packets until the client calls "sf_add_multicast_group()".
 *
 * @param[in] mIpAddr    IPv4 address of multicast group in network byte order:
 *                          224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                      purposes
 *                          224.0.1.0 - 238.255.255.255 User-defined multicast
 *                                                      addresses
 *                          239.0.0.0 - 239.255.255.255 Reserved for
 *                                                      administrative scoping
 * @param[in] port       Port number of multicast group.
 * @retval    >=0        The socket.
 * @retval    -1         Failure. @code{log_add()} called. "errno" will be one
 *                       of the following:
 *                          EACCES        The process does not have appropriate
 *                                        privileges.
 *                          EADDRINUSE    Attempt to establish a connection that
 *                                        uses addresses that are already in
 *                                        use.
 *                          EADDRNOTAVAIL The specified address is not
 *                                        available from the local machine.
 *                          EAFNOSUPPORT  The specified address is not a valid
 *                                        address for the address family of the
 *                                        specified socket.
 *                          EINTR         The attempt to establish a connection
 *                                        was interrupted by delivery of a
 *                                        signal that was caught; the
 *                                        connection shall be established
 *                                        asynchronously.
 *                          EMFILE        No more file descriptors are available
 *                                        for this process.
 *                          ENETDOWN      The local network interface used to
 *                                        reach the destination is down.
 *                          ENETUNREACH   No route to the network is present.
 *                          ENFILE        No more file descriptors are available
 *                                        for the system.
 *                          ENOBUFS       Insufficient resources were available
 *                                        in the system.
 *                          ENOBUFS       No buffer space is available.
 *                          ENOMEM        Insufficient memory was available.
 * @see sf_set_interface(int sock, in_addr_t ifaceAddr)
 * @see sf_set_nonblocking(int sock, int nonblocking)
 * @see sf_set_address_reuse(int sock, int reuse)
 */
int sf_open_multicast(
    const in_addr_t             mIpAddr,
    const unsigned short        port)
{
    return create_or_open_multicast(mIpAddr, port, 0);
}

/**
 * Adds to or drops from an interface an IPv4 multicast group.
 *
 * @param[in] sock       The associated multicast socket.
 * @param[in] mIpAddr    IPv4 address of multicast group in network byte order:
 *                          224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                      purposes
 *                          224.0.1.0 - 238.255.255.255 User-defined multicast
 *                                                      addresses
 *                          239.0.0.0 - 239.255.255.255 Reserved for
 *                                                      administrative scoping
 * @param[in] ifaceAddr  IPv4 address of interface in network byte
 *                       order. 0 means the default interface for multicast
 *                       packets.
 * @param[in] add        Whether to add or drop the multicast group (0 => drop).
 * @retval    0          Success.
 * @retval    -1         Failure. @code{log_add()} called. "errno" will be one
 *                       of the following:
 *                          EBADF       The socket argument is not a valid file
 *                                      descriptor.
 *                          EINVAL      The socket has been shut down.
 *                          ENOTSOCK    The socket argument does not refer to a
 *                                      socket.
 *                          ENOMEM      There was insufficient memory available.
 *                          ENOBUFS     Insufficient resources are available in
 *                                      the system.
 */
static int add_or_drop_multicast_group(
    const int           sock,
    const in_addr_t     mIpAddr,
    const in_addr_t     ifaceAddr,
    const int           add)
{
    int                 status;
    struct ip_mreq      group;

    group.imr_multiaddr.s_addr = mIpAddr;
    group.imr_interface.s_addr = ifaceAddr == 0 ? INADDR_ANY : ifaceAddr;

    status = setsockopt(sock, IPPROTO_IP,
            add ? IP_ADD_MEMBERSHIP : IP_DROP_MEMBERSHIP, &group,
            sizeof(group));

    if (status) {
        char* const mcastAddrString = ipaddr_format(mIpAddr);
        char* const ifaceAddrString = ipaddr_format(ifaceAddr);

        LOG_SERROR5("Couldn't %s IPv4 multicast group %s %s interface %s "
                "for socket %d", add ? "add" : "drop", mcastAddrString,
                add ? "to" : "from", ifaceAddrString, sock);
        free(mcastAddrString);
        free(ifaceAddrString);
    }

    return status;
}

/**
 * Adds a multicast group to the set of multicast groups whose packets a socket
 * receives. Multiple groups may be added. A group may be associated with a
 * particular interface.
 *
 * @param[in] sock       The multicast socket to be configured.
 * @param[in] mIpAddr    IPv4 address of multicast group in network byte order:
 *                          224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                      purposes
 *                          224.0.1.0 - 238.255.255.255 User-defined multicast
 *                                                      addresses
 *                          239.0.0.0 - 239.255.255.255 Reserved for
 *                                                      administrative scoping
 * @param[in] ifaceAddr  IPv4 address of interface in network byte order. 0
 *                       means the default interface for multicast packets.
 * @retval    0          Success.
 * @retval    -1         Failure. @code{log_add()} called. "errno" will be one
 *                       of the following:
 *                          EBADF       The socket argument is not a valid file
 *                                      descriptor.
 *                          EINVAL      The socket has been shut down.
 *                          ENOTSOCK    The socket argument does not refer to a
 *                                      socket.
 *                          ENOMEM      There was insufficient memory available.
 *                          ENOBUFS     Insufficient resources are available in
 *                                      the system.
 */
int sf_add_multicast_group(
    const int       sock,
    const in_addr_t mIpAddr,
    const in_addr_t ifaceAddr )
{
    return add_or_drop_multicast_group(sock, mIpAddr, ifaceAddr , 1);
}

/**
 * Removes a multicast group from the set of multicast groups whose packets a
 * socket receives.
 *
 * @param[in] sock       The socket to be configured.
 * @param[in] mIpAddr    IPv4 address of multicast group in network byte order.
 * @param[in] ifaceAddr  IPv4 address of interface in network byte order. 0
 *                       means the default interface for multicast packets.
 * @retval    0          Success.
 * @retval    -1         Failure. @code{log_add()} called. "errno" will be one
 *                       of the following:
 *                          EBADF       The socket argument is not a valid file
 *                                      descriptor.
 *                          EINVAL      The socket has been shut down.
 *                          ENOTSOCK    The socket argument does not refer to a
 *                                      socket.
 *                          ENOMEM      There was insufficient memory available.
 *                          ENOBUFS     Insufficient resources are available in
 *                                      the system.
 */
int sf_drop_multicast_group(
    const int       sock,
    const in_addr_t mIpAddr,
    const in_addr_t ifaceAddr)
{
    return add_or_drop_multicast_group(sock, mIpAddr, ifaceAddr, 0);
}
