/**
 *   Copyright 2014, University Corporation for Atmospheric Research.
 *
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 *
 *   @file   noaaport_socket.c
 *   @author Steven E. Emmerson
 *
 *   This file implements a socket for reading a NOAAPORT multicast channel.
 */
#include <config.h>

#include "inetutil.h"
#include "mylog.h"
#include "dvbs.h"
#include "noaaport_socket.h"

#include <netinet/in.h>

/**
 * Initializes an IPv4 socket address for a NOAAPORT channel from a
 * specification of the address of the NOAAPORT multicast group.
 *
 * @param[in] nportSockAddr  The IPv4 socket address.
 * @param[in] nportSpec      The NOAAPORT IPv4 multicast address.
 * @retval    0              Success. `*nportSockAddr` is set.
 * @retval    1              Usage error. `mylog_add()` called.
 */
static int
initNportSockAddr(
        struct sockaddr_in* const restrict nportSockAddr,
        const char* const restrict         nportSpec)
{
    in_addr_t addr;
    int       status = addr_init(&addr, nportSpec);

    if (0 == status) {
        if (!mcastAddr_isValid(addr)) {
            mylog_add("Invalid multicast address: \"%s\"", nportSpec);
            status = 1;
        }
        else {
            unsigned channel = ntohl(addr) & 0xFF;

            sockAddr_init(nportSockAddr, addr, s_port[channel-1]);
        }
    }

    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Initializes a socket for receiving a NOAAPORT channel.
 *
 * @param[out] socket     The socket. The caller should close when it's no
 *                        longer needed.
 * @param[in]  nportSpec  IPv4 address of the NOAAPORT multicast.
 * @param[in]  ifaceSpec  IPv4 address of interface on which to listen for
 *                        multicast UDP packets or NULL to listen on all
 *                        available interfaces.
 * @retval     0          Success. `*socket` is set.
 * @retval     1          Usage failure. `mylog_add()` called.
 * @retval     2          O/S failure. `mylog_add()` called.
 */
int
nportSock_init(
        int* const restrict        socket,
        const char* const restrict nportSpec,
        const char* const restrict ifaceSpec)
{
    struct sockaddr_in nportSockAddr;
    int                status = initNportSockAddr(&nportSockAddr, nportSpec);

    if (status) {
        mylog_add("Couldn't initialize address of socket");
    }
    else {
        struct in_addr ifaceAddr;

        status = inetAddr_init(&ifaceAddr, ifaceSpec);
        if (status) {
            mylog_add("Couldn't initialize address of interface");
        }
        else {
            status = mcastRecvSock_init(socket, &nportSockAddr, &ifaceAddr);
            if (status)
                mylog_add("Couldn't initialize socket for multicast reception");
        }
    }

    return status;
}
