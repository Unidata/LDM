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
 * Initializes a NOAAPORT PID channel from the IPv4 address of a NOAAPORT
 * multicast group.
 *
 * @param[out] pidChannel  NOAAPORT PID channel.
 * @param[in]  mcastSpec   IPv4 address of the multicast group.
 * @retval     0           Success. `*pidChannel` is set.
 * @retval     1           Usage error. `log_start()` called.
 */
static int
initPidChannel(
    unsigned* const restrict   pidChannel,
    const char* const restrict mcastSpec)
{
    /*
     * The following is *not* the DVB PID: it's the least significant byte of
     * the IPv4 multicast address specification (e.g., the "3" in "224.0.1.3").
     */
    unsigned channel;
    int      status;

    if (sscanf(mcastSpec, "%*3u.%*3u.%*3u.%3u", &channel) != 1) {
        LOG_START1("Invalid IPv4 address specification: \"%s\"", mcastSpec);
        status = 1;
    }
    else if ((channel < 1) || (channel > MAX_DVBS_PID)) {
        LOG_START1("Invalid NOAAPORT PID channel: %u", channel);
        status = 1;
    }
    else {
        *pidChannel = channel;
        status = 0;
    }

    return status;
}

/**
 * Initializes an IPv4 address from a string specification.
 *
 * @param[out] addr  The IPv4 address.
 * @param[in]  spec  The specification.
 * @retval     0     Success. `*addr` is set.
 * @retval     1     Usage error. `log_start()` called.
 */
static int
initAddr(
        in_addr_t* const restrict  addr,
        const char* const restrict spec)
{
    in_addr_t a = inet_addr(spec);
    int       status;

    if ((in_addr_t)-1 == a) {
        LOG_START1("Invalid IPv4 address: \"%s\"", spec);
        status = 1;
    }
    else {
        *addr = a;
        status = 0;
    }

    return status;
}

/**
 * Initializes an Internet address from the IPv4 address of a multicast group.
 *
 * @param[out] mcastAddr  The Internet address.
 * @param[in]  mcastSpec  The IPv4 address of the multicast group.
 * @retval     0          Success. `*mcastAddr` is set.
 * @retval     1          Usage error. `log_start()` called.
 */
static int
initMcastAddr(
    struct in_addr* const restrict mcastAddr,
    const char* const restrict     mcastSpec)
{
    in_addr_t addr;
    int       status = initAddr(&addr, mcastSpec);

    if (0 == status) {
        if (!IN_MULTICAST(ntohl(addr))) {
            LOG_START1("Not a multicast address: \"%s\"", mcastSpec);
            status = 1;
        }
        else {
            (void)memset(mcastAddr, 0, sizeof(*mcastAddr));
            mcastAddr->s_addr = addr;
            status = 0;
        }
    }

    return status;
}

/**
 * Initializes the Internet socket address of an interface.
 *
 * @param[out] sockAddr   The Internet socket address of the interface.
 * @param[in]  ifaceSpec  The IPv4 address of the interface or NULL to obtain
 *                        all interfaces (i.e., INADDR_ANY).
 * @param[in]  port       Internet port number in host byte order.
 * @retval     0          Success. `*sockAddr` is set.
 * @retval     1          Usage error. `log_start()` called.
 */
static int
initIfaceSockAddr(
    struct sockaddr_in* const restrict sockAddr,
    const char* const restrict         ifaceSpec,
    const unsigned short               port)
{
    in_addr_t addr;
    int       status;

    if (ifaceSpec) {
        status = initAddr(&addr, ifaceSpec);
    }
    else {
        addr = htonl(INADDR_ANY);
        status = 0;
    }

    if (0 == status) {
        (void)memset(sockAddr, 0, sizeof(*sockAddr));
        sockAddr->sin_family = AF_INET;
        sockAddr->sin_addr.s_addr = addr;
        sockAddr->sin_port = htons(port);
    }

    return status;
}

/**
 * Initializes a UDP socket given the Internet socket address of an interface.
 *
 * @param[out] sock           The socket.
 * @param[in]  ifaceSockAddr  The Internet socket address of the interface.
 * @retval     0              Success.
 * @retval     2              System failure. `log_start()` called.
 */
static int
initIfaceSocket(
    int* const restrict                      sock,
    const struct sockaddr_in* const restrict ifaceSockAddr)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int status;

    if (-1 == fd) {
        LOG_SERROR0("Couldn't create UDP socket");
        status = 2;
    }
    else {
        if (bind(fd, (struct sockaddr*)ifaceSockAddr, sizeof(*ifaceSockAddr))) {
            LOG_SERROR2("Couldn't bind UDP socket to %s:%d",
                    inet_ntoa(ifaceSockAddr->sin_addr), ntohs(ifaceSockAddr->sin_port));
            (void)close(fd);
            status = 2;
        }
        else {
            *sock = fd;
            status = 0;
        }
    } // `sock` is open

    return status;
}

/**
 * Joins a socket to an Internet multicast group.
 *
 * @param[out] socket     The socket.
 * @param[in]  ifaceAddr  IPv4 address of the interface on which to listen for
 *                        multicast UDP packets. May specify all available
 *                        interfaces.
 * @param[in]  mcastSpec  IPv4 address of the multicast group.
 * @retval     0          Success.
 * @retval     2          O/S failure. `log_start()` called.
 */
static int
joinMcastGroup(
    const int                            socket,
    const struct in_addr* const restrict ifaceAddr,
    const struct in_addr* const restrict mcastAddr)
{
    struct ip_mreq  mreq;
    int             status;

    mreq.imr_multiaddr = *mcastAddr;
    mreq.imr_interface = *ifaceAddr;

    if (setsockopt(socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void*)&mreq,
            sizeof(mreq)) == -1) {
        LOG_SERROR2("Couldn't join multicast group \"%s\" on interface \"%s\"",
                inet_ntoa(*mcastAddr), inet_ntoa(*ifaceAddr));
        status = 2;
    }
    else {
        status = 0;
    }

    return status;
}

/**
 * Initializes an Internet socket given an interface and an Internet multicast
 * group to join.
 *
 * @param[out] socket         The socket.
 * @param[in]  ifaceSockAddr  Internet socket address of the interface.
 * @param[in]  mcastAddr      IPv4 address of the multicast group.
 * @retval     0              Success.
 * @retval     1              Usage failure. `log_start()` called.
 * @retval     2              System failure. `log_start()` called.
 */
static int
initSocket(
    int* const restrict                      socket,
    const struct sockaddr_in* const restrict ifaceSockAddr,
    const struct in_addr* const restrict     mcastAddr)
{
    int sock;
    int status = initIfaceSocket(&sock, ifaceSockAddr);

    if (status) {
        LOG_ADD0("Couldn't initialize interface socket");
    }
    else {
        status = joinMcastGroup(sock, &ifaceSockAddr->sin_addr, mcastAddr);
        if (status) {
            LOG_ADD0("Couldn't join multicast group");
            close(sock);
        }
        else {
            *socket = sock;
            status = 0;
        }
    }

    return status;
}

/**
 * Initializes an Internet socket given an interface and an Internet multicast
 * group to join.
 *
 * @param[out] socket     The socket.
 * @param[in]  ifaceSpec  IPv4 address of interface on which to listen for
 *                        multicast UDP packets or NULL to listen on all
 *                        available interfaces.
 * @param[in]  mcastSpec  IPv4 address of multicast group.
 * @param[in]  port       Port number of multicast group.
 * @retval     0          Success.
 * @retval     1          Usage failure. `log_start()` called.
 * @retval     2          System failure. `log_start()` called.
 */
static int
initSocketFromSpecs(
    int* const restrict        socket,
    const char* const restrict ifaceSpec,
    const char* const restrict mcastSpec,
    const unsigned short       port)
{
    struct in_addr mcastAddr;
    int            status = initMcastAddr(&mcastAddr, mcastSpec);

    if (status) {
        LOG_ADD0("Couldn't initialize Internet address of multicast group");
    }
    else {
        struct sockaddr_in ifaceSockAddr;

        status = initIfaceSockAddr(&ifaceSockAddr, ifaceSpec, port);
        if (status) {
            LOG_ADD0("Couldn't initialize Internet socket address of interface");
        }
        else {
            status = initSocket(socket, &ifaceSockAddr, &mcastAddr);
        }
    }

    return status;
}

/**
 * Returns a socket suitable for listening for multicast NOAAPORT packets.
 *
 * @param[out] socket     The socket.
 * @param[in]  ifaceSpec  IPv4 address of interface on which to listen for
 *                        multicast UDP packets or NULL to listen on all
 *                        available interfaces.
 * @param[in]  mcastSpec  IPv4 address of multicast group.
 * @retval     0          Success. `*socket` is set.
 * @retval     1          Usage failure. \c log_start() called.
 * @retval     2          O/S failure. \c log_start() called.
 */
static int
getSocket(
    int* const restrict        socket,
    const char* const restrict ifaceSpec,
    const char* const restrict mcastSpec)
{
    unsigned       pidChannel;
    int            status = initPidChannel(&pidChannel, mcastSpec);

    if (status) {
        LOG_ADD0("Couldn't get NOAAPORT PID channel");
    }
    else {
        status = initSocketFromSpecs(socket, ifaceSpec, mcastSpec, s_port[pidChannel-1]);

        if (status)
            LOG_ADD0("Couldn't initialize socket");
    }

    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new UDP-reader.
 *
 * This function is thread-safe.
 *
 * @param[in] mcastSpec  IPv4 address of multicast group.
 * @param[in] ifaceSpec  IPv4 address of interface on which to listen for
 *                       multicast UDP packets or NULL to listen on all
 *                       available interfaces.
 * @param[in] fifo       Pointer to FIFO into which to write data.
 * @param[in] reader     Pointer to pointer to address of returned reader.
 * @retval    0          Success.
 * @retval    1          Usage failure. `log_start()` called.
 * @retval    2          System failure. `log_start()` called.
 */
int multicastReaderNew(
    const char* const   mcastSpec,
    const char* const   ifaceSpec,
    Fifo* const         fifo,
    Reader** const      reader)
{
    int socket;
    int status = getSocket(&socket, ifaceSpec, mcastSpec);

    if (status) {
        LOG_START0("Couldn't create socket for NOAAPORT multicast");
    }
    else {
        // Maximum UDP payload is 65507 bytes
        status = readerNew(socket, fifo, 65507, reader);

        if (status) {
            LOG_ADD0("Couldn't create new reader object");
            close(socket);
        }
    } // `socket` set

    return status;
}
