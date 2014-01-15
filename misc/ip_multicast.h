/*
 * Copyright 2012 University Corporation for Atmospheric Research. All rights
 * reserved.
 *
 * See file COPYRIGHT in the top-level source-directory for legal conditions.
 */

#ifndef IP_MULTICAST_H
#define IP_MULTICAST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns a socket configured for exclusive sending IPv4 multicast packets to
 * an IPv4 multicast group. The originator of packets to a multicast group would
 * typically call this function.
 * <p>
 * log_add() is called for all errors.
 *
 * @param mcast_addr    [in] IPV4 address of multicast group in network byte
 *                      order.
 * @param port_num      [in] Port number used for the destination multicast
                        group.
 * @param iface_addr    [in] IPv4 address of interface for outgoing
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
 *                          EADDRNOTAVAIL   The specified address is not
 *                                          available from the local machine.
 *                          EAFNOSUPPORT    The specified address is not a
 *                                          valid IPv4 multicast address.
 *                          ENETUNREACH No route to the network is present.
 *                          ENETDOWN    The local network interface used to
 *                                      reach the destination is down.
 *                          ENOBUFS     No buffer space is available.
 */
int ipm_create(
    const in_addr_t     mcast_addr,
    const int           port_num,
    const in_addr_t     iface_addr,
    const unsigned char ttl,
    const unsigned char loop,
    const int           nonblock);

/**
 * Returns a socket configured for non-exclusive reception of IPv4 multicast
 * packets. The socket will not receive any multicast packets until the client
 * calls "ipm_add()". Receivers of multicast packets would typically call this
 * function.
 * <p>
 * log_add() is called for all errors.
 *
 * @param nonblock      Whether or not the socket should be in non-blocking
 *                      mode.
 * @return              The configured socked.
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
    const int nonblock);

/**
 * Adds an IPv4 multicast group to the set of multicast groups that a socket
 * receives. Multiple groups may be added.
 * <p>
 * log_add() is called for all errors.
 *
 * @param sock          [in] The socket to be configured.
 * @param mcast_addr    [in] Internet address of IPv4 multicast group in network
 *                      byte order:
 *                          224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                      purposes
 *                          224.0.1.0 - 238.255.255.255 User-defined multicast
 *                                                      addresses
 *                          239.0.0.0 - 239.255.255.255 Reserved for
 *                                                      administrative scoping
 * @param port_num      [in] Port number used for the destination multicast
                        group.
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
    const int       port_num,
    const in_addr_t iface_addr);

/**
 * Removes an IPv4 multicast group from the set of multicast groups that a
 * socket receives.
 * <p>
 * log_add() is called for all errors.
 *
 * @param sock          [in] The socket to be configured.
 * @param mcast_addr    [in] Internet address of IPv4 multicast group in network
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
    const in_addr_t iface_addr);

#ifdef __cplusplus
}
#endif

#endif
