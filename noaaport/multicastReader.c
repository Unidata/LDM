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
 * Initializes a NOAAPORT channel number from the IPv4 address of a NOAAPORT
 * multicast group.
 *
 * @param[out] channel    NOAAPORT channel number.
 * @param[in]  mcastSpec  IPv4 address of the multicast group.
 * @retval     0          Success. `*channel` is set.
 * @retval     1          Usage error. `log_start()` called.
 */
static int
initChannel(
    unsigned* const restrict   channel,
    const char* const restrict mcastSpec)
{
    /*
     * The following is *not* the DVB PID: it's the least significant byte of
     * the IPv4 multicast address specification (e.g., the "3" in "224.0.1.3").
     */
    unsigned chan;
    int      status;

    if (sscanf(mcastSpec, "%*3u.%*3u.%*3u.%3u", &chan) != 1) {
        LOG_START1("Invalid IPv4 address specification: \"%s\"", mcastSpec);
        status = 1;
    }
    else if ((chan < 1) || (chan > MAX_DVBS_PID)) {
        LOG_START1("Invalid NOAAPORT channel number: %u", chan);
        status = 1;
    }
    else {
        *channel = chan;
        status = 0;
    }

    return status;
}

/**
 * Initializes an IPv4 address from a string specification.
 *
 * @param[out] addr  The IPv4 address.
 * @param[in]  spec  The specification or NULL to obtain INADDR_ANY.
 * @retval     0     Success. `*addr` is set.
 * @retval     1     Usage error. `log_start()` called.
 */
static int
initAddr(
        in_addr_t* const restrict  addr,
        const char* const restrict spec)
{
    int       status;

    if (NULL == spec) {
        *addr = htonl(INADDR_ANY);
        status = 0;
    }
    else {
        in_addr_t a = inet_addr(spec);

        if ((in_addr_t)-1 == a) {
            LOG_START1("Invalid IPv4 address: \"%s\"", spec);
            status = 1;
        }
        else {
            *addr = a;
            status = 0;
        }
    }

    return status;
}

/**
 * Initializes a multicast IPv4 socket address.
 *
 * @param[out] mcastSockAddr  The multicast IPv4 socket address.
 * @param[in]  mcastSpec      The IPv4 address of the multicast group.
 * @param[in]  port           The port number of the multicast group in host
 *                            byte order.
 * @retval     0              Success. `*mcastSockAddr` is set.
 * @retval     1              Usage error. `log_start()` called.
 */
static int
initMcastSockAddr(
    struct sockaddr_in* const restrict mcastSockAddr,
    const char* const restrict         mcastSpec,
    const unsigned short               port)
{
    in_addr_t addr;
    int       status = initAddr(&addr, mcastSpec);

    if (0 == status) {
        if (!IN_MULTICAST(ntohl(addr))) {
            LOG_START1("Not a multicast address: \"%s\"", mcastSpec);
            status = 1;
        }
        else {
            (void)memset(mcastSockAddr, 0, sizeof(*mcastSockAddr));
            mcastSockAddr->sin_family = AF_INET;
            mcastSockAddr->sin_addr.s_addr = addr;
            mcastSockAddr->sin_port = htons(port);
            status = 0;
        }
    }

    return status;
}

/**
 * Initializes an IPv4 address from an IPv4 address specification.
 *
 * @param[out] inetAddr   The Internet address.
 * @param[in]  inetSpec   The IPv4 address specification. May be `NULL` to
 *                        obtain `INADDR_ANY`.
 * @retval     0          Success. `*inetAddr` is set.
 * @retval     1          Usage error. `log_start()` called.
 */
static int
initInetAddr(
    struct in_addr* const restrict inetAddr,
    const char* const restrict     inetSpec)
{
    in_addr_t addr;
    int       status = initAddr(&addr, inetSpec);

    if (0 == status) {
        (void)memset(inetAddr, 0, sizeof(*inetAddr));
        inetAddr->s_addr = addr;
        status = 0;
    }

    return status;
}

/**
 * Initializes a UDP multicast socket from the Internet socket address of a
 * multicast group.
 *
 * @param[out] sock           The socket.
 * @param[in]  mcastSockAddr  The Internet socket address of the multicast
 *                            group.
 * @retval     0              Success.
 * @retval     2              System failure. `log_start()` called.
 */
static int
initMcastSocket(
    int* const restrict                      sock,
    const struct sockaddr_in* const restrict mcastSockAddr)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int status;

    if (-1 == fd) {
        LOG_SERROR0("Couldn't create UDP socket");
        status = 2;
    }
    else {
        if (bind(fd, (struct sockaddr*)mcastSockAddr, sizeof(*mcastSockAddr))) {
            LOG_SERROR2("Couldn't bind UDP socket to %s:%d",
                    inet_ntoa(mcastSockAddr->sin_addr),
                    ntohs(mcastSockAddr->sin_port));
            (void)close(fd);
            status = 2;
        }
        else {
            *sock = fd;
            status = 0;
        }
    } // `fd` is open

    return status;
}

/**
 * Joins a socket to an Internet multicast group.
 *
 * @param[out] socket     The socket.
 * @param[in]  mcastAddr  IPv4 address of the multicast group.
 * @param[in]  ifaceAddr  IPv4 address of the interface on which to listen for
 *                        multicast UDP packets. May specify all available
 *                        interfaces.
 * @retval     0          Success.
 * @retval     2          O/S failure. `log_start()` called.
 */
static int
joinMcastGroup(
    const int                            socket,
    const struct in_addr* const restrict mcastAddr,
    const struct in_addr* const restrict ifaceAddr)
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
 * Initializes an IPv4 socket given an interface and an IPv4 multicast
 * group to join.
 *
 * @param[out] socket         The socket.
 * @param[in]  mcastSockAddr  IPv4 socket address of the multicast group.
 * @param[in]  ifaceAddr      IPv4 address of the interface.
 * @retval     0              Success.
 * @retval     1              Usage failure. `log_start()` called.
 * @retval     2              System failure. `log_start()` called.
 */
static int
initSocket(
    int* const restrict                      socket,
    const struct sockaddr_in* const restrict mcastSockAddr,
    const struct in_addr* const restrict     ifaceAddr)
{
    int sock;
    int status = initMcastSocket(&sock, mcastSockAddr);

    if (status) {
        LOG_ADD0("Couldn't initialize multicast socket");
    }
    else {
        status = joinMcastGroup(sock, &mcastSockAddr->sin_addr, ifaceAddr);
        if (status) {
            LOG_ADD0("Couldn't join multicast group");
            close(sock);
        }
        else {
            *socket = sock;
            status = 0;
        }
    } // `sock` is open

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
 * @param[in]  port       Port number of multicast group in host byte order.
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
    struct sockaddr_in mcastSockAddr;
    int                status = initMcastSockAddr(&mcastSockAddr, mcastSpec,
            port);

    if (status) {
        LOG_ADD0("Couldn't initialize socket address of multicast group");
    }
    else {
        struct in_addr ifaceAddr;

        status = initInetAddr(&ifaceAddr, ifaceSpec);
        if (status) {
            LOG_ADD0("Couldn't initialize address of interface");
        }
        else {
            status = initSocket(socket, &mcastSockAddr, &ifaceAddr);
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
    unsigned       channel;
    int            status = initChannel(&channel, mcastSpec);

    if (status) {
        LOG_ADD0("Couldn't initialize NOAAPORT channel");
    }
    else {
        status = initSocketFromSpecs(socket, ifaceSpec, mcastSpec,
                s_port[channel-1]);

        if (status)
            LOG_ADD0("Couldn't initialize socket");
    }

    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns a new reader of a NOAAPORT multicast channel.
 *
 * This function is thread-safe.
 *
 * @param[our] reader     The returned reader.
 * @param[in]  mcastSpec  IPv4 address of the NOAAPORT multicast group.
 * @param[in]  ifaceSpec  IPv4 address of the interface on which to listen for
 *                        multicast packets or NULL to listen on all available
 *                        interfaces.
 * @param[in]  fifo       Pointer to the FIFO into which to write data.
 * @retval     0          Success. `*reader` is set.
 * @retval     1          Usage failure. `log_start()` called.
 * @retval     2          System failure. `log_start()` called.
 */
int mcastReader_new(
    Reader** const      reader,
    const char* const   mcastSpec,
    const char* const   ifaceSpec,
    Fifo* const         fifo)
{
    int socket;
    int status = getSocket(&socket, ifaceSpec, mcastSpec);

    if (status) {
        LOG_START0("Couldn't create socket for NOAAPORT multicast");
    }
    else {
        /*
         * The maximum IPv4 UDP payload is 65507 bytes. The maximum observed UDP
         * payload, however, should be 5232 bytes, which is the maximum amount
         * of data in a NESDIS frame (5152 bytes) plus the overhead of the 3 SBN
         * protocol headers: frame level header (16 bytes) + product definition
         * header (16 bytes) + AWIPS product specific header (48 bytes). The
         * maximum size of an ethernet jumbo frame is around 9000 bytes.
         * Consequently, the maximum amount to read in a single call is
         * conservatively set to 10000 bytes. 2014-12-30.
         */
        status = readerNew(socket, fifo, 10000, reader);

        if (status) {
            LOG_ADD0("Couldn't create new reader object");
            close(socket);
        }
    } // `socket` set

    return status;
}
