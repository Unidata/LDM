/*
 *  send_test.c -- multicasts "hello, world!" to a multicast group once a second
 *
 *  Created By:  Antony Courtney 25/11/94
 *  Modified By: Steve Emmerson  2015-04-03
 *               Steve Emmerson  2019-02-11
 */

#define _XOPEN_SOURCE 600

#include "send_recv_test.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int
get_context(
        int                             argc,
        char** const restrict           argv,
        struct in_addr* const restrict  groupAddr,
        in_port_t* const restrict       groupPort,
        struct in_addr* const restrict  ifaceAddr,
        unsigned char* const restrict   ttl,
        bool* const restrict            verbose)
{
    in_addr_t       tmpIface = htonl(INADDR_ANY); // use default multicast interface
    unsigned        tmpTtl = 1;                   // not forwarded by any router
    bool            verb = false;                 // Not verbose
    int             ch;
    int             status = 0;
    struct in_addr  grpAddr = {};

    grpAddr.s_addr = inet_addr(HELLO_GROUP);

    while (0 == status && (ch = getopt(argc, argv, "i:g:t:v")) != -1) {
        switch (ch) {
        case 'i': {
            tmpIface = inet_addr(optarg);
            if ((in_addr_t)-1 == tmpIface) {
                (void)fprintf(stderr, "Couldn't decode interface IP address\n");
                status = -1;
            }
            break;
        }
        case 'g': {
            if (inet_pton(AF_INET, optarg, &grpAddr.s_addr) != 1) {
                perror("inet_pton() couldn't parse multicast group IP address");
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
        case 'v': {
            verb = true;
            break;
        }
        default:
            status = -1;
        }
    }

    if (0 == status) {
        *groupAddr = grpAddr;
        *groupPort = HELLO_PORT;
        ifaceAddr->s_addr = tmpIface;
        *ttl = tmpTtl;
        *verbose = verb;
    }

    return status;
}

static void
usage(const char* const progname)
{
    (void)fprintf(stderr,
"Usage:\n"
"    %s [-i <iface>] [-g <grpAddr>] [-t <ttl>] [-v]\n"
"where:\n"
"    -i <iface>   IPv4 address of interface to use. Default is system default.\n"
"    -g <grpAddr> Multicast group IP address. Default is %s.\n"
"    -t <ttl>     Time-to-live for outgoing packets. Default is 1.\n"
"    -v           Verbose output\n",
    progname, HELLO_GROUP);
}

static bool sock_remoteString(
        const int    sd,
        char* const  buf,
        const size_t len)
{
    bool            success = false;
    struct sockaddr sockAddr;
    socklen_t       addrLen = sizeof(sockAddr);
    int             status = getpeername(sd, &sockAddr, &addrLen);
    if (status) {
        perror("getpeername()");
    }
    else {
		const struct sockaddr_in* const inAddr = (struct sockaddr_in*)&sockAddr;

    	success = snprintf(buf, len, "%s:%d",
    			inet_ntoa(inAddr->sin_addr), ntohs(inAddr->sin_port)) < len;
    }
    return success;
}

int
main(int argc, char *argv[])
{
    struct in_addr groupAddr;
    in_port_t      groupPort;
    struct in_addr ifaceAddr;
    unsigned char  ttl;
    bool           verbose;

    if (get_context(argc, argv, &groupAddr, &groupPort, &ifaceAddr, &ttl,
            &verbose)) {
        usage(basename(argv[0]));
        exit(1);
    }

    // Create a UDP socket
    int sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sd < 0) {
        perror("socket()");
        exit(1);
    }

    // Set the interface to use for sending packets
    if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_IF, &ifaceAddr,
            sizeof(ifaceAddr))) {
        perror("Couldn't set sending interface");
        exit(1);
    }

    // Set the time-to-live of the packets
    if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl))) {
        perror("Couldn't set time-to-live for multicast packets");
        exit(1);
    }

    // Enable loopback of multicast datagrams
    {
        unsigned char enable = 1;
        if (setsockopt(sd, IPPROTO_IP, IP_MULTICAST_LOOP, &enable,
                sizeof(enable))) {
            perror("Couldn't enable loopback of multicast datagrams");
            exit(1);
        }
    }

    // Set the IP address and port number of the multicast-group
    struct sockaddr_in addr;

    (void)memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(groupPort);
    addr.sin_addr = groupAddr;

    if (addr.sin_addr.s_addr == (in_addr_t)-1) {
        perror("inet_addr()");
        exit(1);
    }
    if (connect(sd, (struct sockaddr*)&addr, sizeof(addr))) {
        perror("connect()");
        exit(1);
    }

    if (verbose) {
        char rmtStr[80];
        if (!sock_remoteString(sd, rmtStr, sizeof(rmtStr)))
            exit(1);
        char ifaceStr[INET_ADDRSTRLEN];
        (void)strcpy(ifaceStr, inet_ntoa(ifaceAddr));
        (void)fprintf(stderr, "Sending to group %s using interface %s\n",
                rmtStr, ifaceStr);
    }

    // Enter a sending loop
    char msg[80] = {};
    for (unsigned i = 0; ; ++i) {
        (void)snprintf(msg, sizeof(msg), "%u", i);
#if 1
        if (send(sd, msg, strlen(msg), 0) < 0)
#else
        if (sendto(sd, msg, strlen(msg), 0, (struct sockaddr*)&addr,
                sizeof(addr)) < 0)
#endif
        {
             perror("sendto()");
             exit(1);
        }

        sleep(1);
    }
}
