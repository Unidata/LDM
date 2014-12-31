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
 * Initializes an IPv4 address from a string specification.
 *
 * @param[out] addr  The IPv4 address in network byte order.
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
 * Initializes a NOAAPORT channel number from the IPv4 address of a NOAAPORT
 * multicast group. The channel number is the least significant byte of the
 * multicast address (e.g., the "3" in "224.0.1.3").
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
    in_addr_t addr;
    int       status = initAddr(&addr, mcastSpec);

    if (0 == status) {
        unsigned  chan = ntohl(addr) & 0xFF;

        if ((chan < 1) || (chan > MAX_DVBS_PID)) {
            LOG_START1("Invalid NOAAPORT channel number: %u", chan);
            status = 1;
        }
        else {
            *channel = chan;
            status = 0;
        }
    }

    return status;
}

/**
 * Initializes a multicast IPv4 address.
 *
 * @param[out] mcastAddr  The multicast IPv4 address in network byte order.
 * @param[in]  mcastSpec  The IPv4 address of the multicast group.
 * @retval     0          Success. `*mcastAddr` is set.
 * @retval     1          Usage error. `log_start()` called.
 */
static int
initMcastAddr(
    in_addr_t* const restrict  mcastAddr,
    const char* const restrict mcastSpec)
{
    in_addr_t addr;
    int       status = initAddr(&addr, mcastSpec);

    if (0 == status) {
        if ((ntohl(addr) & 0xF0000000) != 0xE0000000) {
            LOG_START1("Invalid multicast address: \"%s\"", mcastSpec);
            status = 1;
        }
        else {
            *mcastAddr = addr;
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
    int       status = initMcastAddr(&addr, mcastSpec);

    if (0 == status) {
        (void)memset(mcastSockAddr, 0, sizeof(*mcastSockAddr));
        mcastSockAddr->sin_family = AF_INET;
        mcastSockAddr->sin_addr.s_addr = addr;
        mcastSockAddr->sin_port = htons(port);
        status = 0;
    }

    return status;
}

/**
 * Initializes a UDP socket from an Internet socket address.
 *
 * @param[out] sock      The socket.
 * @param[in]  sockAddr  The Internet socket address.
 * @retval     0         Success.
 * @retval     2         System failure. `log_start()` called.
 */
static int
initUdpSocket(
    int* const restrict                      sock,
    const struct sockaddr_in* const restrict sockAddr)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int status;

    if (-1 == fd) {
        LOG_SERROR0("Couldn't create UDP socket");
        status = 2;
    }
    else {
        if (bind(fd, (struct sockaddr*)sockAddr, sizeof(*sockAddr))) {
            LOG_SERROR2("Couldn't bind UDP socket to %s:%d",
                    inet_ntoa(sockAddr->sin_addr), ntohs(sockAddr->sin_port));
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
 *                        multicast UDP packets. May specify `INADDR_ANY`.
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
 * Initializes an IPv4 multicast socket.
 *
 * @param[out] socket         The socket.
 * @param[in]  mcastSockAddr  IPv4 socket address of the multicast group to
 *                            join.
 * @param[in]  ifaceAddr      IPv4 address of the interface. May specify
 *                            `INADDR_ANY`.
 * @retval     0              Success.
 * @retval     1              Usage failure. `log_start()` called.
 * @retval     2              System failure. `log_start()` called.
 */
static int
initMcastSocket(
    int* const restrict                      socket,
    const struct sockaddr_in* const restrict mcastSockAddr,
    const struct in_addr* const restrict     ifaceAddr)
{
    int sock;
    int status = initUdpSocket(&sock, mcastSockAddr);

    if (status) {
        LOG_ADD0("Couldn't initialize multicast socket");
    }
    else {
        status = joinMcastGroup(sock, &mcastSockAddr->sin_addr, ifaceAddr);
        if (status) {
            LOG_ADD0("Couldn't have socket join multicast group");
            (void)close(sock);
        }
        else {
            *socket = sock;
        }
    } // `sock` is open

    return status;
}

/**
 * Initializes the Internet socket address of a NOAAPORT multicast");
 *
 * @param[in] nportSockAddr  The Internet socket address.
 * @param[in] nportSpec      The NOAAPORT multicast address.
 * @retval    0              Success. `*nportSockAddr` is set.
 * @retval    1              Usage error. `log_start()` called.
 * @retval    2              System error. `log_start()` called.
 */
static int
initNportSockAddr(
    struct sockaddr_in* const restrict nportSockAddr,
    const char* const restrict         nportSpec)
{
    unsigned channel;
    int      status = initChannel(&channel, nportSpec);

    if (status) {
        LOG_ADD0("Couldn't initialize NOAAPORT channel");
    }
    else {
        status = initMcastSockAddr(nportSockAddr, nportSpec, s_port[channel-1]);
        if (status)
            LOG_ADD0("Couldn't initialize socket address for NOAAPORT multicast");
    }

    return status;
}

/**
 * Initializes a socket for receiving a NOAAPORT multicast.
 *
 * @param[out] socket     The socket.
 * @param[in]  nportSpec  IPv4 address of the NOAAPORT multicast.
 * @param[in]  ifaceSpec  IPv4 address of interface on which to listen for
 *                        multicast UDP packets or NULL to listen on all
 *                        available interfaces.
 * @retval     0          Success. `*socket` is set.
 * @retval     1          Usage failure. \c log_start() called.
 * @retval     2          O/S failure. \c log_start() called.
 */
static int
initNportSocket(
    int* const restrict        socket,
    const char* const restrict nportSpec,
    const char* const restrict ifaceSpec)
{
    struct sockaddr_in nportSockAddr;
    int                status = initNportSockAddr(&nportSockAddr, nportSpec);

    if (0 == status) {
        struct in_addr ifaceAddr;

        status = initInetAddr(&ifaceAddr, ifaceSpec);
        if (status) {
            LOG_ADD0("Couldn't initialize address of NOAAPORT interface");
        }
        else {
            status = initMcastSocket(socket, &nportSockAddr, &ifaceAddr);
            if (status)
                LOG_ADD0("Couldn't initialize NOAAPORT socket");
        }
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
    int status = initNportSocket(&socket, mcastSpec, ifaceSpec);

    if (0 == status) {
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
            (void)close(socket);
        }
    } // `socket` set

    return status;
}
