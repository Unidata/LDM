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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int
get_context(
        int                       argc,
        char** const restrict     argv,
        in_addr_t* const restrict groupAddr,
        in_port_t* const restrict groupPort,
        in_addr_t* const restrict ifaceAddr)
{
    in_addr_t iface = htonl(INADDR_ANY); // use default multicast interface
    int       ch;
    int       status = 0;

    while (0 == status && (ch = getopt(argc, argv, "i:")) != -1) {
        switch (ch) {
        case 'i': {
            iface = inet_addr(optarg);
            if ((in_addr_t)-1 == iface) {
                (void)fprintf(stderr, "Couldn't decode interface IP address\n");
                status = -1;
            }
            break;
        }
        default:
            status = -1;
        }
    }

    if (0 == status) {
        *groupAddr = inet_addr(HELLO_GROUP);
        *groupPort = HELLO_PORT;
        *ifaceAddr = iface;
    }

    return status;
}

static int
create_socket(
        int* const sock)
{
    int status;

    // Create a UDP socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("Couldn't create socket");
        status = -1;
    }
    else {
        // Allow multiple sockets to use the same port number
        const int yes = 1;
        status = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (status) {
            perror("Couldn't reuse port number");
        }
        else {
            *sock = fd;
        }
    }

    return status;
}

static int
join_group(
        const int       sock,
        const in_addr_t groupAddr,
        const in_addr_t ifaceAddr)
{
    struct ip_mreq mreq;
    int            status;

    mreq.imr_multiaddr.s_addr = groupAddr;
    mreq.imr_interface.s_addr = ifaceAddr;

    status = setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
            sizeof(mreq));
    if (status)
        perror("Couldn't join multicast group");

    return status;
}

static int
configure_socket(
        const int       sock,
        const in_addr_t groupAddr,
        const in_port_t groupPort,
        const in_addr_t ifaceAddr)
{
    struct sockaddr_in addr;

    /*
     * Bind the socket to the port number of the multicast group and to any IP
     * address.
     */
    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // N.B.: all IP addresses
    addr.sin_port = htons(groupPort);

    int status = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (status) {
        perror("Couldn't bind socket to IP address and port number");
    }
    else {
        status = join_group(sock, groupAddr, ifaceAddr);
    }

    return status;
}

static int
print_packets(
        const int sock)
{
    int status;

    // Enter a receive-then-print loop
    for (;;) {
        struct sockaddr_in addr;
        socklen_t          addrlen = sizeof(addr);
        char               msgbuf[256];
        int                nbytes = recvfrom(sock, msgbuf, sizeof(msgbuf), 0,
                (struct sockaddr*)&addr, &addrlen);

        if (nbytes < 0) {
            perror("Couldn't receive packet");
            status = -1;
            break;
        }

        (void)printf("%.*s\n", nbytes, msgbuf);
    }

    return status;
}

int
main(int argc, char *argv[])
{
    in_addr_t groupAddr, ifaceAddr;
    in_port_t groupPort;
    int       status =
            get_context(argc, argv, &groupAddr, &groupPort, &ifaceAddr);

    if (0 == status) {
        int sock;

        status = create_socket(&sock);
        if (0 == status) {
            status = configure_socket(sock, groupAddr, groupPort, ifaceAddr);

            if (0 == status)
                status = print_packets(sock);
        }
    }

    return status ? 1 : 0;
}
