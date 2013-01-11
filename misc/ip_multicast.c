/**
 * Copyright 2013 University Corporation for Atmospheric Research. All rights
 * reserved.
 * <p>
 * See file COPYRIGHT in the top-level source-directory for legal conditions.
 *
 * @file ip_multicast.c
 *
 * Module for IPv4 multicast.
 * <p>
 * Examples:
 *      Create a blocking socket for sending IP multicast packets on the local
 *      subnet using the default multicast interface (the packets will not
 *      appear on the loopback interface):
 *
 *          #include <arpa/inet.h>
 *          #include <ip_multicast.h>
 *          ...
 *          int sock = ipm_create(inet_addr("224.1.1.1"), 0, 1, 0, 0)
 *
 *      Open a non-blocking socket for receiving IP multicast packets on a
 *      specific interface:
 *
 *           #include <arpa/inet.h>
 *           #include <ip_multicast.h>
 *           ...
 *           int sock = ipm_open(1);
 *           ipm_add(sock, inet_addr("224.1.1.1"), inet_addr("128.117.156.30"));
 */

#include "config.h"

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "log.h"
#include "ip_multicast.h"

/**
 * Returns a socket configured for IP multicast.
 * <p>
 * log_add() is called for all errors.
 *
 * @param iface_addr    [in] Internet address of interface for multicast
 *                      packets in network byte order. 0 means the default
 *                      interface for multicast packets.
 * @param ttl           [in] Time-to-live of outgoing packets:
 *                           0       Restricted to same host. Won't be output
 *                                   by any interface.
 *                           1       Restricted to the same subnet. Won't be
 *                                   forwarded by a router.
 *                         <32       Restricted to the same site, organization
 *                                   or department.
 *                         <64       Restricted to the same region.
 *                        <128       Restricted to the same continent.
 *                        <255       Unrestricted in scope. Global.
 * @param loop          [in] Whether packets sent to the multicast group should
 *                      be received by the sending host via the loopback
 *                      interface.
 * @param nonblock      Whether or not the socket should be in non-blocking
 *                      mode.
 * @return              The configured socket.
 * @retval -1           Failure. "errno" will be one of the following:
 *                          EMFILE      No more file descriptors are available
 *                                      for this process.
 *                          ENFILE      No more file descriptors are available
 *                                      for the system.
 *                          EACCES      The process does not have appropriate
 *                                      privileges.
 *                          ENOBUFS     Insufficient resources were available
 *                                      in the system.
 *                          ENOMEM      Insufficient memory was available.
 */
static int ipm_new(
    const in_addr_t     iface_addr,
    const unsigned char ttl,
    const unsigned char loop,
    const int           nonblock)
{
    int success = 1;
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);

    if (sock == -1) {
        LOG_SERROR0("Couldn't create UDP socket");
        success = 0;
    }
    else {
        if (!loop
                && setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop,
                        sizeof(loop) == -1)) {
            LOG_SERROR0("Couldn't disable loopback");
            success = 0;
        }
        if (success && ttl != 1) {
            if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, &ttl,
                    sizeof(ttl)) == -1) {
                LOG_SERROR1("Couldn't set time-to-live to %u", ttl);
                success = 0;
            }
        }
        if (success && iface_addr != 0) {
            struct in_addr addr;

            addr.s_addr = iface_addr;

            if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &addr,
                    sizeof(addr)) == -1) {
                LOG_SERROR1("Couldn't set outgoing IP multicast interface "
                "to %s", inet_ntoa(addr));
                success = 0;
            }
        }
        if (success && nonblock) {
            int flags = fcntl(sock, F_GETFL);

            if (flags == -1) {
                LOG_SERROR1("Couldn't get status flags of socket %d", sock);
                success = 0;
            }
            else if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
                LOG_SERROR1("Couldn't set socket %d to non-blocking", sock);
                success = 0;
            }
        }

        if (!success)
            (void) close(sock);
    } /* "sock" allocated */

    return success ? sock : -1;
}

/**
 * Returns a socket configured for sending IP multicast packets to an IP
 * multicast group. The originator of packets to a multicast group would
 * typically call this function.
 * <p>
 * log_add() is called for all errors.
 *
 * @param mcast_addr    [in] Internet address of IP multicast group in network
 *                      byte order.
 * @param iface_addr    [in] Internet address of interface for outgoing
 *                      multicast packets in network byte order. 0 means the
 *                      default interface for multicast packets.
 * @param ttl           [in] Time-to-live of outgoing packets:
 *                           0       Restricted to same host. Won't be output
 *                                   by any interface.
 *                           1       Restricted to the same subnet. Won't be
 *                                   forwarded by a router.
 *                         <32       Restricted to the same site, organization
 *                                   or department.
 *                         <64       Restricted to the same region.
 *                        <128       Restricted to the same continent.
 *                        <255       Unrestricted in scope. Global.
 * @param loop          [in] Whether packets sent to the multicast group should
 *                      also be received by the sending host via the loopback
 *                      interface.
 * @param nonblock      Whether or not the socket should be in non-blocking
 *                      mode.
 * @retval 0            Success.
 * @retval -1           Failure. "errno" will be one of the following:
 *                          EMFILE      No more file descriptors are available
 *                                      for this process.
 *                          ENFILE      No more file descriptors are available
 *                                      for the system.
 *                          EACCES      The process does not have appropriate
 *                                      privileges.
 *                          ENOBUFS     Insufficient resources were available
 *                                      in the system.
 *                          ENOMEM      Insufficient memory was available.
 *                          EADDRNOTAVAIL   The specified address is not
 *                                          available from the local machine.
 *                          EAFNOSUPPORT    The specified address is not a
 *                                          valid IP multicast address.
 *                          ENETUNREACH No route to the network is present.
 *                          ENETDOWN    The local network interface used to
 *                                      reach the destination is down.
 *                          ENOBUFS     No buffer space is available.
 */
int ipm_create(
    const in_addr_t     mcast_addr,
    const in_addr_t     iface_addr,
    const unsigned char ttl,
    const unsigned char loop,
    const int           nonblock)
{
    int sock = ipm_new(iface_addr, ttl, loop, nonblock);

    if (sock != -1) {
        struct sockaddr_in addr;

        (void) memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = mcast_addr;

        if (connect(sock, (struct sockaddr*) &addr, sizeof(addr)) == -1) {
            LOG_SERROR1("Couldn't bind socket to IP multicast address %s",
                    inet_ntoa(addr.sin_addr));
            (void) close(sock);
            sock = -1;
        }
    }

    return sock;
}

/**
 * Returns a socket configured for receiving IP multicast packets. The socket
 * will not receive any multicast packets until the client calls "ipm_add()".
 * Receivers of multicast packets would typically call this function.
 * <p>
 * log_add() is called for all errors.
 *
 * @param nonblock      Whether or not the socket should be in non-blocking
 *                      mode.
 * @retval 0            Succes.
 * @retval -1           Failure. "errno" will be one of the following:
 *                          EMFILE      No more file descriptors are available
 *                                      for this process.
 *                          ENFILE      No more file descriptors are available
 *                                      for the system.
 *                          EACCES      The process does not have appropriate
 *                                      privileges.
 *                          ENOBUFS     Insufficient resources were available
 *                                      in the system.
 *                          ENOMEM      Insufficient memory was available.
 */
int ipm_open(
    const int nonblock)
{
    return ipm_new(0, 1, 0, nonblock);
}

/**
 * Adds an IP multicast group to the set of multicast groups that a socket
 * receives. Multiple groups may be added.
 * <p>
 * log_add() is called for all errors.
 *
 * @param sock          [in] The socket to be configured.
 * @param mcast_addr    [in] Internet address of IP multicast group in network
 *                      byte order:
 *                          224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                      purposes
 *                          224.0.1.0 - 238.255.255.255 User-defined multicast
 *                                                      addresses
 *                          239.0.0.0 - 239.255.255.255 Reserved for
 *                                                      administrative scoping
 * @param iface_addr    [in] Internet address of interface in network byte
 *                      order. 0 means the default interface for multicast
 *                      packets.
 * @retval 0            Success.
 * @retval -1           Failure. "errno" will be one of the following:
 *                          EBADF       The socket argument is not a valid file
 *                                      descriptor.
 *                          EINVAL      The socket has been shut down.
 *                          ENOTSOCK    The socket argument does not refer to a
 *                                      socket.
 *                          ENOMEM      There was insufficient memory available.
 *                          ENOBUFS     Insufficient resources are available in
 *                                      the system.
 */
int ipm_add(
    const int       sock,
    const in_addr_t mcast_addr,
    const in_addr_t iface_addr)
{
    int             status;
    struct ip_mreq  group;

    group.imr_multiaddr.s_addr = mcast_addr;
    group.imr_interface.s_addr = iface_addr == 0 ? INADDR_ANY : iface_addr;

    status = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group,
            sizeof(group));

    if (status) {
        char buf[INET_ADDRSTRLEN];
        struct in_addr addr;

        addr.s_addr = iface_addr;

        (void) inet_ntop(AF_INET, &mcast_addr, buf, sizeof(buf));
        LOG_SERROR2("Couldn't add IP multicast group %s to interface %s", buf,
                inet_ntoa(addr));
    }

    return status;
}

/**
 * Removes an IP multicast group from the set of multicast groups that a socket
 * receives.
 * <p>
 * log_add() is called for all errors.
 *
 * @param sock          [in] The socket to be configured.
 * @param mcast_addr    [in] Internet address of IP multicast group in network
 *                      byte order.
 * @param iface_addr    [in] Internet address of interface in network byte
 *                      order. 0 means the default interface for multicast
 *                      packets.
 * @retval 0            Success.
 * @retval -1           Failure. "errno" will be one of the following:
 *                          EBADF       The socket argument is not a valid file
 *                                      descriptor.
 *                          EINVAL      The socket has been shut down.
 *                          ENOTSOCK    The socket argument does not refer to a
 *                                      socket.
 *                          ENOMEM      There was insufficient memory available.
 *                          ENOBUFS     Insufficient resources are available in
 *                                      the system.
 */
int ipm_drop(
    const int       sock,
    const in_addr_t mcast_addr,
    const in_addr_t iface_addr)
{
    int             status;
    struct ip_mreq  group;

    group.imr_multiaddr.s_addr = mcast_addr;
    group.imr_interface.s_addr = iface_addr == 0 ? INADDR_ANY : iface_addr;

    status = setsockopt(sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &group,
            sizeof(group));

    if (status) {
        char buf[INET_ADDRSTRLEN];
        struct in_addr addr;

        addr.s_addr = iface_addr;

        (void) inet_ntop(AF_INET, &mcast_addr, buf, sizeof(buf));
        LOG_SERROR2("Couldn't drop IP multicast group %s from interface %s",
                buf, inet_ntoa(addr));
    }

    return status;
}
