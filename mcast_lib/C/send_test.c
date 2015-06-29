/*
 *  send_test.c -- multicasts "hello, world!" to a multicast group once a second
 *
 *  Created By:  Antony Courtney 25/11/94
 *  Modified By: Steve Emmerson  2015-04-03
 */

#define _XOPEN_SOURCE 600
#define _GNU_SOURCE 1

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
        in_addr_t* const restrict ifaceAddr,
        unsigned* const restrict  ttl)
{
    in_addr_t tmpIface = htonl(INADDR_ANY); // use default multicast interface
    unsigned  tmpTtl = 1; // not forwarded by any router
    int       ch;
    int       status = 0;

    while (0 == status && (ch = getopt(argc, argv, "i:t:")) != -1) {
        switch (ch) {
        case 'i': {
            tmpIface = inet_addr(optarg);
            if ((in_addr_t)-1 == tmpIface) {
                (void)fprintf(stderr, "Couldn't decode interface IP address\n");
                status = -1;
            }
            break;
        }
        case 't': {
            int      nbytes;
            if (1 != sscanf(optarg, "%3u %n", &tmpTtl, &nbytes) ||
                    0 != optarg[nbytes]) {
                (void)fprintf(stderr,
                        "Couldn't decode time-to-live option argument \"%s\"\n",
                        optarg);
                status = -1;
            }
            else if (0 != optarg[nbytes] || 255 <= tmpTtl) {
                (void)fprintf(stderr, "Invalid time-to-live option\n");
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
        *ifaceAddr = tmpIface;
        *ttl = tmpTtl;
    }

    return status;
}

int
main(int argc, char *argv[])
{
    in_addr_t groupAddr, ifaceAddr;
    in_port_t groupPort;
    unsigned  ttl;

    if (get_context(argc, argv, &groupAddr, &groupPort, &ifaceAddr, &ttl))
        exit(1);

    // Create a UDP socket
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket()");
        exit(1);
    }

    // Set the time-to-live of the packets
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl))) {
        perror("Couldn't set time-to-live for multicast packets");
        exit(1);
    }

    // Set the interface to use for sending packets
    struct in_addr iface;
    iface.s_addr = ifaceAddr;
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface))) {
        perror("Couldn't set sending interface");
        exit(1);
    }

    // Set the IP address and port number of the multicast-group
    struct sockaddr_in addr;

    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(groupPort);
    addr.sin_addr.s_addr = groupAddr;

    if (addr.sin_addr.s_addr == (in_addr_t)-1) {
        perror("inet_addr()");
        exit(1);
    }
    if (connect(fd, &addr, sizeof(addr))) {
        perror("connect()");
        exit(1);
    }

    // Enter a sending loop
    const char msg[] = "Hello, World!";
    for (;;) {
#if 1
        if (write(fd, msg, sizeof(msg)) < 0)
#else
        if (sendto(fd, msg, sizeof(msg), 0, (struct sockaddr*)&addr,
                sizeof(addr)) < 0)
#endif
        {
             perror("sendto()");
             exit(1);
        }

        sleep(1);
    }
}
