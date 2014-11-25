/*
 *   Copyright 2011, University Corporation for Atmospheric Research.
 *
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __USE_MISC
    #define __USE_MISC          /* To get "struct ip_mreq" on Linux. Don't move! */
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include "inetutil.h"
#include "log.h"
#include "dvbs.h"
#include "fifo.h"
#include "multicastReader.h" /* Eat own dog food */

/**
 * Returns a new UDP-reader.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Usage failure. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
 */
int multicastReaderNew(
    const char* const   mcastSpec,  /**< [in] Multicast group in IPv4 dotted-
                                      *  quad format */
    const char* const   interface,  /**< [in] IPv4 address of interface on which
                                      *  to listen for multicast UDP packets
                                      *  in IPv4 dotted-quad format or NULL to
                                      *  listen on all available interfaces */
    Fifo* const         fifo,       /**< [in] Pointer to FIFO into which to
                                      *  write data */
    Reader** const      reader)     /**< [out] Pointer to pointer to address of
                                      *  returned reader */
{
    int status = 0;                 /* default success */
    /*
     * The following is *not* the DVB PID: it's the least significant byte of
     * the IPv4 multicast address (e.g., the "3" in "224.0.1.3").
     */
    int pidChannel;

    if (sscanf(mcastSpec, "%*3d.%*3d.%*3d.3%d", &pidChannel) != 1) {
        LOG_START1("Couldn't decode multicast specification \"%s\"", mcastSpec);
        status = 1;
    }
    else if ((pidChannel < 1) || (pidChannel > MAX_DVBS_PID)) {
        LOG_START1("Invalid NOAAPORT PID channel: %d", pidChannel);
        status = 1;
    }
    else {
        struct hostent* hostEntry = gethostbyname(mcastSpec);

        if (NULL == hostEntry) {
            LOG_START1("Unknown multicast group \"%s\"", mcastSpec);
            status = 1;
        }
        else {
            struct in_addr  mcastAddr;

            (void)memcpy(&mcastAddr, hostEntry->h_addr_list[0],
                    hostEntry->h_length);

            if (!IN_MULTICAST(ntohl(mcastAddr.s_addr))) {
                LOG_START1("Not a multicast address: \"%s\"", mcastSpec);
                status = 1;
            }
            else {
                int sock = socket(AF_INET, SOCK_DGRAM, 0);

                if (-1 == sock) {
                    LOG_SERROR0("Couldn't create socket");
                    status = 2;
                }
                else {
                    short               port = s_port[pidChannel - 1];
                    struct sockaddr_in  sockAddr;

                    sockAddr.sin_family = AF_INET;
                    sockAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                    sockAddr.sin_port = htons(port);
                    
                    if (bind(sock, (struct sockaddr*)&sockAddr,
                                sizeof(sockAddr)) < 0) {
                        LOG_SERROR2("Couldn't bind to %s:%d", mcastSpec, port);
                        status = 2;
                    }
                    else {
                        struct ip_mreq  mreq;

                        mreq.imr_multiaddr.s_addr = mcastAddr.s_addr;
                        mreq.imr_interface.s_addr = (interface == NULL )
                            ? htonl(INADDR_ANY)
                            : inet_addr(interface);

                        if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                    (void*)&mreq, sizeof(mreq)) == -1) {
                            LOG_SERROR2("Couldn't join multicast group "
                                    "\"%s\" on interface \"%s\"",
                                    mcastSpec,
                                    NULL == interface ? "ANY" : interface);
                        }
                        else {
                            if ((status = readerNew(sock, fifo, 10000,
                                            reader)) != 0) {
                                LOG_ADD0("Couldn't create new reader object");
                            }           /* "*reader" set */
                        }               /* joined multicast group */
                    }                   /* socket bound */

                    if (0 != status)
                        (void)close(sock);
                }                       /* "sock" open */
            }                           /* valid multicast address */
        }                               /* known multicast group */
    }                                   /* valid "pid_channel" */

    return status;
}
