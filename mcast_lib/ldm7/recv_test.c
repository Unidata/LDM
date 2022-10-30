/*
 *  recv_test.c -- joins a multicast group and echoes all data it receives from
 *                 the group to its stdout...
 *
 *  Antony Courtney,       25/11/94
 *  Modified by: Frédéric Bastien (25/03/04)
 *               to compile without warning and work correctly
 *  Modified by: Steve Emmerson (2015-04-03)
 *               to look prettier and include more headers
 *  Modified by: Steve Emmerson  2019-02-11
 *               to have better logging
 */

#define _XOPEN_SOURCE 600

// For `struct ip_mreq_source`
#define _DEFAULT_SOURCE

#include "send_recv_test.h"

#include <arpa/inet.h>
#include <libgen.h>
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
    grpAddr.sin_addr.s_addr = inet_addr(MCAST_ADDR);
    grpAddr.sin_port = htons(MCAST_PORT);

    while (success && (ch = getopt(argc, argv, "i:g:s:v")) != -1) {
        switch (ch) {
        case 'i': {
            if (inet_pton(AF_INET, optarg, &ifAddr.s_addr) != 1) {
                perror("inet_pton() couldn't parse interface IP address");
                success = false;
            }
            break;
        }
        case 'g': {
            if (inet_pton(AF_INET, optarg, &grpAddr.sin_addr.s_addr) != 1) {
                perror("inet_pton() couldn't parse multicast group IP address");
                success = false;
            }
            break;
        }
        case 's': {
            if (inet_pton(AF_INET, optarg, &srcAddr.s_addr) != 1) {
                perror("inet_pton() couldn't parse source IP address");
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

static void
usage(const char* const progname)
{
    (void)fprintf(stderr,
"Usage:\n"
"    %s [-i <iface>] [-g <grpAddr>] [-s <srcAddr>] [-v]\n"
"where:\n"
"    -i <iface>   IPv4 address of interface to use. Default depends on <srcAddr>.\n"
"    -g <grpAddr> Multicast group IP address. Default is %s.\n"
"    -s <srcAddr> IPv4 address of source. Default is any-source multicast.\n"
"    -v           Verbose output\n",
    progname, MCAST_ADDR);
}

static bool
create_udp_socket(
        int* const sock)
{
    bool success;

    // Create a UDP socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket() couldn't create socket");
        success = false;
    }
    else {
        // Allow multiple sockets to use the same port number
        const int yes = 1;

        success = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))
                == 0;
        if (!success) {
            perror("setsockopt() couldn't reuse port number");
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
    char                  groupAddrStr[80];
    char                  ifaceAddrStr[80];
    char                  sourceAddrStr[80];

    mreq.imr_multiaddr = *groupAddr;
    mreq.imr_interface = *ifaceAddr;
    mreq.imr_sourceaddr = *sourceAddr;

    if (debug) {
        inet_ntop(AF_INET, &mreq.imr_multiaddr.s_addr, groupAddrStr,
                sizeof(groupAddrStr));
        inet_ntop(AF_INET, &mreq.imr_interface.s_addr, ifaceAddrStr,
                sizeof(ifaceAddrStr));
        inet_ntop(AF_INET, &mreq.imr_sourceaddr.s_addr, sourceAddrStr,
                sizeof(sourceAddrStr));
    }

    success = setsockopt(sock, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, &mreq,
            sizeof(mreq)) == 0;

    if (!success) {
        perror(NULL);
        (void)fprintf(stderr,
                "Couldn't join socket %d to source-specific multicast group %s "
                "on interface %s with source %s", sock,
                groupAddrStr, ifaceAddrStr, sourceAddrStr);
    }
    else if (debug) {
        (void)fprintf(stderr,
                "Joined socket %d to source-specific multicast group %s on "
                "interface %s with source %s\n", sock, groupAddrStr,
                ifaceAddrStr, sourceAddrStr);
    }

    return success;
}

/**
 * Joins a socket to an any-source multicast group on a network interface.
 */
static bool
join_any_multicast(
        const int                            sock,
        const struct in_addr* const restrict groupAddr,
        const struct in_addr* const restrict ifaceAddr,
        const bool                           debug)
{
    bool           success;
    struct ip_mreq mreq;
    char           groupAddrStr[80];
    char           ifaceAddrStr[80];

    mreq.imr_multiaddr = *groupAddr;
    mreq.imr_interface = *ifaceAddr;

    inet_ntop(AF_INET, &mreq.imr_multiaddr.s_addr, groupAddrStr,
            sizeof(groupAddrStr));
    inet_ntop(AF_INET, &mreq.imr_interface.s_addr, ifaceAddrStr,
            sizeof(ifaceAddrStr));

    success = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
            sizeof(mreq)) == 0;

    if (!success) {
        perror(NULL);
        (void)fprintf(stderr,
                "Couldn't join socket %d to any-source multicast group %s on "
                "interface %s\n", sock, groupAddrStr, ifaceAddrStr);
    }
    else if (debug) {
        (void)fprintf(stderr,
                "Joined socket %d to any-source multicast group %s on "
                "interface %s\n", sock, groupAddrStr, ifaceAddrStr);
    }

    return success;
}

bool isSourceSpecific(const struct in_addr* const addr)
{
	return (ntohl(addr->s_addr) >> 24) == 232;
}

/**
 * Configures a socket for receiving multicast packets.
 *
 * @param[in] sock        Socket
 * @param[in] groupAddr   Internet address of multicast group
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
    char groupSockAddrStr[80];

    if (debug)
    	snprintf(groupSockAddrStr, sizeof(groupSockAddrStr), "%s:%d",
                inet_ntoa(groupSockAddr->sin_addr),
                ntohs(groupSockAddr->sin_port));

    /*
     * Bind the local endpoint of the socket to the address and port number of
     * the multicast group.
     *
     * Using `htonl(INADDR_ANY)` in the following will also work but the socket
     * will receive every packet destined to the port number regardless of
     * destination IP address.
     */
    success = bind(sock, (struct sockaddr*)groupSockAddr,
            sizeof(*groupSockAddr)) == 0;

    if (!success) {
        perror(NULL);
        (void)fprintf(stderr,
                "Couldn't bind socket %d to multicast group address %s\n", sock,
                groupSockAddrStr);
    }
    else {
        if (debug)
            (void)fprintf(stderr,
                    "Bound socket %d to multicast group address %s\n", sock,
                    groupSockAddrStr);

        // Have the socket join the multicast group on a network interface.
        success = isSourceSpecific(&groupSockAddr->sin_addr)
                ? join_source_multicast(sock, &groupSockAddr->sin_addr,
                        ifaceAddr, sourceAddr, debug)
                : join_any_multicast(sock, &groupSockAddr->sin_addr, ifaceAddr,
                        debug);
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
        if (-1 == getsockname(sock, (struct sockaddr*)&sockAddrIn, &len)) {
            perror("getsockname()");
            return false;
        }
        char         buf[80];
		const struct sockaddr_in* const inAddr =
				(struct sockaddr_in*)&sockAddrIn;
    	if (snprintf(buf, sizeof(buf), "%s:%d",
    			inet_ntoa(inAddr->sin_addr), ntohs(inAddr->sin_port))
    			>= sizeof(buf))
			return false;
        (void)fprintf(stderr, "Receiving from socket bound to %s\n", buf);
    }

    // Enter a receive-then-print loop
    for (;;) {
        struct sockaddr_in addr;
        socklen_t          addrlen = sizeof(addr);
        char               msgbuf[1462]; // Maximum UDP over ethernet payload
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

    if (!success) {
        usage(basename(argv[0]));
    }
    else {
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
