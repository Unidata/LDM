/*
 *  recv_test.c -- joins a multicast group and echoes all data it receives from
 *                 the group to its stdout...
 *
 *  Antony Courtney,       25/11/94
 *  Modified by: Frédéric Bastien (25/03/04)
 *               to compile without warning and work correctly
 *  Modified by: Steve Emmerson (2015-04-03)
 *               to look prettier and include more headers
 */

#include "config.h"

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE     1
#define __EXTENSIONS__  1

#include "send_recv_test.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/**
 * Returns the context for running this program.
 *
 * @param[in]  argc        Number of command-line arguments
 * @param[in]  argv        Command-line arguments
 * @param[out] groupAddr   IP address of multicast group
 * @param[out] groupPort   Port number of multicast group in host byte-order
 * @param[out] ifaceAddr   IP address of interface on which to receive packets
 * @param[out] sourceAddr  IP address of source of multicast packets or
 *                         `NULL` to indicate non-source-specific multicast
 * @param[out] debug       Whether or not to log debugging messages
 * @retval `true`  Success
 * @retval `false` Failure
 */
static bool
get_context(
        int                                argc,
        char* const* restrict              argv,
        struct sockaddr_in* const restrict groupAddr,
        struct in_addr* const restrict     ifaceAddr,
        struct in_addr* const restrict     sourceAddr,
        bool* const restrict               debug)
{
    struct in_addr     ifAddr = {};
    struct in_addr     srcAddr = {};
    struct sockaddr_in grpAddr = {};
    bool        dbg = false;       // Not debug
    int         ch;
    bool        success = true;

    ifAddr.s_addr = INADDR_ANY;  // System default interface
    srcAddr.s_addr = INADDR_ANY; // Source-independent multicast
    grpAddr.sin_family = AF_INET;
    grpAddr.sin_addr.s_addr = inet_addr(HELLO_GROUP);
    grpAddr.sin_port = htons(HELLO_PORT);

    while (success && (ch = getopt(argc, argv, "i:s:v")) != -1) {
        switch (ch) {
        case 'i': {
            if (inet_pton(AF_INET, optarg, &ifAddr.s_addr) != 1) {
                perror("Couldn't parse interface IP address");
                success = false;
            }
            break;
        }
        case 's': {
            if (inet_pton(AF_INET, optarg, &srcAddr.s_addr) != 1) {
                perror("Couldn't parse source IP address");
                success = false;
            }
            break;
        }
        case 'v': {
            dbg = true;
            break;
        }
        default:
            success = false;
        }
    }

    if (success) {
        *ifaceAddr = ifAddr;
        *sourceAddr = srcAddr;
        *groupAddr = grpAddr;
        *debug = dbg;
    }

    return success;
}

static bool
create_udp_socket(
        int* const sock)
{
    bool success;

    // Create a UDP socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("Couldn't create socket");
        success = false;
    }
    else {
        // Allow multiple sockets to use the same port number
        const int yes = 1;

        success = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))
                == 0;
        if (!success) {
            perror("Couldn't reuse port number");
        }
        else {
            *sock = fd;
        }
    }

    return success;
}

/**
 * Joins a socket to a source-specific multicast group on a network interface.
 * @param[in] sock  UDP Socket
 * @param[in] groupAddr   IP address of multicast group
 * @param[in] ifaceAddr   IP address of interface on which to listen
 * @param[in] sourceAddr  IP address of source of multicast packets
 * @param[in] debug       Whether or not to log debugging messages
 * @retval `true`   Success
 * @retval `false`  Failure
 */
static bool
join_source_multicast(
        const int                            sock,
        const struct in_addr* const restrict groupAddr,
        const struct in_addr* const restrict ifaceAddr,
        const struct in_addr* const restrict sourceAddr,
        const bool                           debug)
{
    bool                  success;
    struct ip_mreq_source mreq; // NB: Different structure than join_multicast()

    mreq.imr_multiaddr = *groupAddr;
    mreq.imr_interface = *ifaceAddr;
    mreq.imr_sourceaddr = *sourceAddr;

    if (debug) {
        char groupAddrStr[80];
        inet_ntop(AF_INET, &mreq.imr_multiaddr.s_addr, groupAddrStr,
                sizeof(groupAddrStr));
        char ifaceAddrStr[80];
        inet_ntop(AF_INET, &mreq.imr_interface.s_addr, ifaceAddrStr,
                sizeof(ifaceAddrStr));
        char sourceAddrStr[80];
        inet_ntop(AF_INET, &mreq.imr_sourceaddr.s_addr, sourceAddrStr,
                sizeof(sourceAddrStr));
        (void)fprintf(stderr, "Joining multicast group %s on interface %s "
                "with source %s\n", groupAddrStr, ifaceAddrStr,
                sourceAddrStr);
    }
    success = setsockopt(sock, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, &mreq,
            sizeof(mreq)) == 0;
    if (!success)
        perror("Couldn't join source-specific multicast group");

    return success;
}

/**
 * Joins a socket to a multicast group on a network interface.
 */
static bool
join_multicast(
        const int                            sock,
        const struct in_addr* const restrict groupAddr,
        const struct in_addr* const restrict ifaceAddr,
        const bool                           debug)
{
    bool           success;
    struct ip_mreq mreq;

    mreq.imr_multiaddr = *groupAddr;
    mreq.imr_interface = *ifaceAddr;

    if (debug) {
        char groupAddrStr[80];
        inet_ntop(AF_INET, &mreq.imr_multiaddr.s_addr, groupAddrStr,
                sizeof(groupAddrStr));
        char ifaceAddrStr[80];
        inet_ntop(AF_INET, &mreq.imr_interface.s_addr, ifaceAddrStr,
                sizeof(ifaceAddrStr));
        (void)fprintf(stderr, "Joining multicast group %s on interface %s\n",
                groupAddrStr, ifaceAddrStr);
    }
    success = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
            sizeof(mreq)) == 0;
    if (!success)
        perror("Couldn't join multicast group");

    return success;
}

/**
 * Configures a socket for receiving multicast packets.
 *
 * @param[in] sock        Socket
 * @param[in] groupAddr   IP address of multicast group
 * @param[in] ifaceAddr   IP address of interface on which to receive packets
 * @param[in] sourceAddr  IP address of source of multicast packets or `NULL`
 *                        to indicate non-source-specific multicast
 * @param[in] debug       Whether or not to log debugging messages
 * @retval    `true`      If and only if success
 */
static bool
configure_socket(
        const int                                sock,
        const struct sockaddr_in* const restrict groupSockAddr,
        const struct in_addr* const restrict     ifaceAddr,
        const struct in_addr* const restrict     sourceAddr,
        const bool                               debug)
{
    bool success;

    if (debug) {
        char groupSockAddrStr[80];
        (void)sockAddrIn_format(groupSockAddr, groupSockAddrStr,
                sizeof(groupSockAddrStr));
        (void)fprintf(stderr, "Binding socket to %s\n", groupSockAddrStr);
    }
    /*
     * Bind the local endpoint of the socket to the address and port number of
     * the multicast group.
     *
     * Using `htonl(INADDR_ANY)` in the following will work but the socket will
     * accept every packet destined to the port number regardless of destination
     * IP address.
     */
    success = bind(sock, groupSockAddr, sizeof(*groupSockAddr)) == 0;
    if (!success) {
        perror("Couldn't bind socket to multicast group address\n");
    }
    else {
        // Have the socket join the multicast group on a network interface.
        success = (sourceAddr->s_addr == INADDR_ANY)
                ? join_multicast(sock, &groupSockAddr->sin_addr, ifaceAddr,
                        debug)
                : join_source_multicast(sock, &groupSockAddr->sin_addr,
                        ifaceAddr, sourceAddr, debug);
    }

    return success;
}

/**
 * Creates a socket for receiving multicast UDP packets.
 *
 * @param[in] sock        Socket
 * @param[in] groupAddr   IP address of multicast group
 * @param[in] ifaceAddr   IP address of interface on which to receive packets or
 *                       `"0.0.0.0"` to use the default multicast interface
 * @param[in] sourceAddr  IP address of source of multicast packets or `NULL`
 *                        to indicate non-source-specific multicast
 * @param[in] debug       Whether or not to log debugging messages
 * @retval    `true`      If and only if success.
 */
static bool
create_socket(
        int* const                         sock,
        struct sockaddr_in* const restrict groupSockAddr,
        struct in_addr* const restrict     ifaceAddr,
        struct in_addr* const restrict     sourceAddr,
        const bool                         debug)
{
    int  sd;
    bool success = create_udp_socket(&sd);

    if (success) {
        success = configure_socket(sd, groupSockAddr, ifaceAddr, sourceAddr,
                debug);

        if (!success) {
            close(sd);
        }
        else {
            *sock = sd;
        }
    }

    return success;
}

static bool
print_packets(
        const int  sock,
        const bool debug)
{
    if (debug) {
        struct sockaddr_in sockAddrIn;
        socklen_t          len = sizeof(sockAddrIn);
        if (-1 == getsockname(sock, &sockAddrIn, &len)) {
            perror("getsockname()");
            return false;
        }
        char buf[80];
        if (!sockAddrIn_format(&sockAddrIn, buf, sizeof(buf)))
            return false;
        (void)fprintf(stderr, "Receiving from socket bound to %s\n", buf);
    }

    // Enter a receive-then-print loop
    for (;;) {
        struct sockaddr_in addr;
        socklen_t          addrlen = sizeof(addr);
        char               msgbuf[256];
        int                nbytes = recvfrom(sock, msgbuf, sizeof(msgbuf), 0,
                (struct sockaddr*)&addr, &addrlen);

        if (nbytes < 0) {
            perror("Couldn't receive packet");
            return false;
        }

        (void)printf("%.*s\n", nbytes, msgbuf);
    }

    return true;
}

int
main(int argc, char *argv[])
{
    struct in_addr     ifaceAddr;
    struct sockaddr_in groupAddr;
    struct in_addr     sourceAddr;
    bool               debug;
    bool               success = get_context(argc, argv, &groupAddr,
            &ifaceAddr, &sourceAddr, &debug);

    if (success) {
        int sock;

        success = create_socket(&sock, &groupAddr, &ifaceAddr, &sourceAddr,
                debug);

        if (success) {
            success = print_packets(sock, debug);
            (void)close(sock);
        }
    }

    return success ? 0 : 1;
}
