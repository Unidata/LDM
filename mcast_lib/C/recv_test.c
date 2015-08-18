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
 * @param[in]  argc       Number of command-line arguments.
 * @param[in]  argv       Command-line arguments.
 * @param[out] groupAddr  IP address of multicast group.
 * @param[out] groupPort  Port number of multicast group in host byte-order.
 * @param[out] ifaceAddr  IP address of interface on which to receive packets or
 *                        `"0.0.0.0"` to use the default multicast interface.
 * @retval     `true`     if and only if success.
 */
static bool
get_context(
        int                         argc,
        char** const restrict       argv,
        const char** const restrict groupAddr,
        in_port_t* const restrict   groupPort,
        const char** const restrict ifaceAddr)
{
    char* iface = "0.0.0.0"; // use default multicast interface
    int   ch;
    bool  success = true;

    while (success && (ch = getopt(argc, argv, "i:")) != -1) {
        switch (ch) {
        case 'i': {
            if (inet_addr(optarg) == (in_addr_t)-1) {
                (void)fprintf(stderr, "Couldn't decode interface IP address\n");
                success = false;
            }
            else {
                iface = optarg;
            }
            break;
        }
        default:
            success = false;
        }
    }

    if (success) {
        *groupAddr = HELLO_GROUP;
        *groupPort = HELLO_PORT;
        *ifaceAddr = iface;
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
 * Configures a socket for receiving multicast packets.
 *
 * @param[in] sock       Socket.
 * @param[in] groupAddr  IP address of multicast group.
 * @param[in] groupPort  Port number of multicast group in host byte-order.
 * @param[in] ifaceAddr  IP address of interface on which to receive packets or
 *                       `"0.0.0.0"` to use the default multicast interface.
 * @retval    `true`     If and only if success.
 */
static bool
configure_socket(
        const int                  sock,
        const char* const restrict groupAddr,
        const in_port_t            groupPort,
        const char* const restrict ifaceAddr)
{
    struct sockaddr_in addr;
    bool               success;

    /*
     * Bind the socket to the port number of the multicast group and to an IP
     * address.
     */
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(groupPort);
    /*
     * Using `htonl(INADDR_ANY)` in the following will also work but the socket
     * will accept every packet destined to the port number regardless of IP
     * address.
     */
    addr.sin_addr.s_addr = inet_addr(groupAddr);

    success = bind(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0;
    if (!success) {
        perror("Couldn't bind socket to IP address and port number");
    }
    else {
        // Have the socket join a multicast group on a network adaptor.
        struct ip_mreq mreq;

        mreq.imr_multiaddr.s_addr = inet_addr(groupAddr);
        mreq.imr_interface.s_addr = inet_addr(ifaceAddr);

        success = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
                sizeof(mreq)) == 0;
        if (!success)
            perror("Couldn't join multicast group");
    }

    return success;
}

/**
 * Creates a socket for receiving multicast UDP packets.
 *
 * @param[in] sock       Socket.
 * @param[in] groupAddr  IP address of multicast group.
 * @param[in] groupPort  Port number of multicast group in host byte-order.
 * @param[in] ifaceAddr  IP address of interface on which to receive packets or
 *                       `"0.0.0.0"` to use the default multicast interface.
 * @retval    `true`     If and only if success.
 */
static bool
create_socket(
        int* const                 sock,
        const char* const restrict groupAddr,
        const in_port_t            groupPort,
        const char* const restrict ifaceAddr)
{
    int  fd;
    bool success = create_udp_socket(&fd);

    if (success) {
        success = configure_socket(fd, groupAddr, groupPort, ifaceAddr);

        if (!success) {
            close(fd);
        }
        else {
            *sock = fd;
        }
    }

    return success;
}

static bool
print_packets(
        const int sock)
{
    bool success;

    // Enter a receive-then-print loop
    for (;;) {
        struct sockaddr_in addr;
        socklen_t          addrlen = sizeof(addr);
        char               msgbuf[256];
        int                nbytes = recvfrom(sock, msgbuf, sizeof(msgbuf), 0,
                (struct sockaddr*)&addr, &addrlen);

        if (nbytes < 0) {
            perror("Couldn't receive packet");
            success = false;
            break;
        }

        (void)printf("%.*s\n", nbytes, msgbuf);
    }

    return success; // Eclipse wants to see a return
}

int
main(int argc, char *argv[])
{
    const char* ifaceAddr;
    const char* groupAddr;
    in_port_t   groupPort;
    bool        success = get_context(argc, argv, &groupAddr, &groupPort,
            &ifaceAddr);

    if (success) {
        int sock;

        success = create_socket(&sock, groupAddr, groupPort, ifaceAddr);

        if (success) {
            success = print_packets(sock);
            (void)close(sock);
        }
    }

    return success ? 0 : 1;
}
