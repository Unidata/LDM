/**
 * This file implements an Internet socket address.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: InetSockAddr.c
 *  Created on: Oct 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "InetSockAddr.h"
#include "log.h"

#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>

struct inetSockAddr {
    InetId*   inetId;    ///< Internet identifier
    in_port_t port;      ///< Port number in host byte order
    /// String representation
    char      sockAddrStr[1+_POSIX_HOST_NAME_MAX+1+1+5+1];
};

InetSockAddr*
isa_newFromId(
        const char* const inetSockId,
        in_port_t         port)
{
    InetSockAddr* isa;

    if (inetSockId == NULL) {
        log_add("NULL ID argument");
        isa = NULL;
    }
    else {
        isa = log_malloc(sizeof(*isa), "Internet socket address");

        if (isa) {
            int         status;
            const char* fmt = (inetSockId[0] == '[')
                    ? "[%m[^\]]]:%" SCNu16
                    : "%m[^:]:%" SCNu16;
            char*       addrId;

            int numAssigned = sscanf(inetSockId, fmt, &addrId, &port);

            if (numAssigned <= 0) {
                log_add("Can't decode \"%s\"", inetSockId);
                status = EINVAL;
            }
            else {
                isa->port = port;
                isa->inetId = inetId_newFromStr(addrId);

                if (isa->inetId == NULL) {
                    log_add("inetId_newFromId() failure");
                    status = -1;
                }
                else {
                    status = 0;
                }

                free(addrId);
            } // Address ID allocated

            if (status) {
                free(isa);
                isa = NULL;
            }
        } // Internet socket address allocated
    } // ID argument isn't NULL

    return isa;
}

void
isa_free(InetSockAddr* const isa)
{
    if (isa) {
        inetId_free(isa->inetId);
        free(isa);
    }
}

const char*
isa_toString(InetSockAddr* const isa)
{
    const char* addrId = inetId_getId(isa->inetId);
    const char* fmt = (inetId_idIsName(isa->inetId) || strchr(addrId, '.'))
            ? "%s:%u"
            : "[%s]:%u";
    int         nbytes = snprintf(isa->sockAddrStr, sizeof(isa->sockAddrStr),
            fmt, addrId, isa->port);

    log_assert(nbytes < sizeof(isa->sockAddrStr));

    return isa->sockAddrStr;
}

InetSockAddr*
isa_clone(const InetSockAddr* const isa)
{
    return (isa == NULL)
            ? NULL
            : isa_newFromId(inetId_getId(isa->inetId), isa->port);
}

int
isa_getSockAddr(
        InetSockAddr* const restrict    isa,
        struct sockaddr* const restrict sockaddr,
        socklen_t* const restrict       socklen)
{
    int       status = inetId_getAddr(isa->inetId, &sockaddr->sa_family,
            sockaddr, socklen);

    if (status == 0) {
        if (sockaddr->sa_family == AF_INET) {
            ((struct sockaddr_in*)sockaddr)->sin_port = htons(isa->port);
        }
        else if (sockaddr->sa_family == AF_INET6) {
            ((struct sockaddr_in6*)sockaddr)->sin6_port = htons(isa->port);
        }
        else {
            log_add("Invalid address family");
            status = EINVAL;
        }
    }

    return status;
}

const char*
isa_getInetAddrStr(const InetSockAddr* const restrict isa)
{
    return inetId_getId(isa->inetId);
}

const InetId*
isa_getInetId(const InetSockAddr* const isa)
{
    return isa->inetId;
}

in_port_t
isa_getPort(const InetSockAddr* const isa)
{
    return isa->port;
}

int
isa_setPort(
        InetSockAddr* const isa,
        const in_port_t     port)
{
    int status;

    if (isa == NULL) {
        log_add("NULL argument");
        status = EINVAL;
    }
    else {
        isa->port = port;
        status = 0;
    }

    return status;
}

int
isa_compare(
        const InetSockAddr* const isa1,
        const InetSockAddr* const isa2)
{
    int status = inetId_compare(isa1->inetId, isa2->inetId);

    if (status == 0)
        status = (isa1->port < isa2->port)
                ? -1
                : (isa1->port == isa2->port)
                  ? 0
                  : 1;

    return status;
}

int
isa_initSockAddr(
        const InetSockAddr* const restrict isa,
        const int                          family,
        const bool                         forBind,
        struct sockaddr* const restrict    sockAddr)
{
    int status = EINVAL;

    if (isa == NULL) {
        log_add("NULL Internet socket address");
    }
    else if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) {
        log_add("Invalid address family: %d", family);
    }
    else if (sockAddr == NULL) {
        log_add("NULL socket address");
    }
    else {
        struct addrinfo* addrInfo;

        struct addrinfo hints = {};
        hints.ai_family = family;
        // The address family must be compatible with the local host
        hints.ai_flags |= AI_ADDRCONFIG;
        // The `getaddrinfo()` call specifies the numeric port number
        hints.ai_flags |= AI_NUMERICSERV;
        if (forBind)
            hints.ai_flags |= AI_PASSIVE;

        char portStr[6];
        (void)sprintf(portStr, "%" PRIu16, isa->port); // Can't fail

        status = getaddrinfo(inetId_getId(isa->inetId), portStr, &hints,
                &addrInfo);

        if (status) {
            log_add("Couldn't get address information for \"%s\"",
                    inetId_getId(isa->inetId));
        }
        else {
            *sockAddr = *addrInfo->ai_addr;

            freeaddrinfo(addrInfo);
        }
    }

    return status;
}

/**
 * Initializes an Internet socket address structure from a string
 * representation.
 *
 * @param[out] sockAddr      Internet socket address
 * @param[in]  inetId        String representation of an Internet identifier in
 *                           one of the following forms:
 *                             - <hostname>
 *                             - <IPv4 address>
 *                             - <IPv6 address>
 * @param[in]  family        IP address family. One of `AF_UNSPEC`, `AF_INET` or
 *                           `AF_INET6`. This function will fail if the local
 *                           host doesn't support the requested address family.
 * @param[in]  forBind       Socket address is intended for `bind()`
 * @retval     EAI_AGAIN     The name could not be resolved at this time. Future
 *                           attempts may succeed. `log_add()` called.
 * @retval     EAI_FAIL      A non-recoverable error occurred. `log_add()`
 *                           called.
 * @retval     EAI_MEMORY    There was a memory allocation failure. `log_add()`
 *                           called.
 * @retval     EAI_NONAME    The Internet identifier's name cannot be located.
 *                           `log_add()` called.
 * @retval     EAI_OVERFLOW  An argument buffer overflowed. `log_add()` called.
 * @retval     EAI_SYSTEM    A system error occurred. The error code can be
 *                           found in `errno`. `log_add()` called.
 */
static int
isa_initFromInetId(
        struct sockaddr* const restrict sockAddr,
        const char* const restrict      inetId,
        const int                       family,
        const bool                      forBind)
{
    struct addrinfo* addrInfo;

    struct addrinfo hints = {};
    hints.ai_family = family;
    // The address family must be compatible with the local host
    hints.ai_flags = AI_ADDRCONFIG;
    if (forBind)
        hints.ai_flags |= AI_PASSIVE;

    int status = getaddrinfo(inetId, NULL, &hints, &addrInfo);

    if (status) {
        log_add("Couldn't get address information for \"%s\"", inetId);
    }
    else {
        *sockAddr = *addrInfo->ai_addr;

        freeaddrinfo(addrInfo);
    }

    return status;
}

int
isa_initFromId(
        struct sockaddr* const restrict sockAddr,
        const char* const restrict      id,
        in_port_t                       port,
        const int                       family,
        const bool                      forBind)
{
    int status;

    if (sockAddr == NULL) {
        log_add("NULL sockaddr argument");
        status = EINVAL;
    }
    else if (id == NULL) {
        log_add("NULL ID argument");
        status = EINVAL;
    }
    else if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6) {
        log_add("Invalid address family: %d", family);
        status = EINVAL;
    }
    else {
        const char* fmt = (id[0] == '[')
                ? "[%m[^\]]]:%u"
                : "%m[^:]:%u";
        char*       inetId;

        int numAssigned = sscanf(id, fmt, &inetId, &port);

        if (numAssigned <= 0) {
            log_add("Can't decode \"%s\"", id);
            status = EINVAL;
        }
        else {
            status = 0;

            if (numAssigned == 2 && port > UINT16_MAX) {
                log_add("Invalid port number: %u", port);
                status = EINVAL;
            }

            if (status == 0) {
                status = isa_initFromInetId(sockAddr, inetId, family, forBind);

                if (status) {
                    log_add("Couldn't initialize sockaddr from \"%s\"", inetId);
                }
                else {
                    if (sockAddr->sa_family == AF_INET) {
                        ((struct sockaddr_in*)sockAddr)->sin_port = port;
                    }
                    else {
                        ((struct sockaddr_in6*)sockAddr)->sin6_port = port;
                    }
                }
            }

            free(inetId);
        } // Internet ID allocated
    } // Valid arguments

    return status;
}

char*
isa_getIpAddrId(const char* const sockAddrId)
{
    char* ipAddrId = NULL;

    if (sockAddrId == NULL) {
        log_add("NULL argument");
    }
    else {
        const char* const fmt = (sockAddrId[0] == '[')
                ? "[%m[^\]]]"
                : "%m[^:]";

        int numAssigned = sscanf(sockAddrId, fmt, &ipAddrId);

        if (numAssigned < 1)
            log_add("Invalid socket address ID: \"%s\"", sockAddrId);
    }

    return ipAddrId;
}

in_port_t
isa_getPortFromId(
        const char* const sockAddrId,
        const in_port_t   defaultPort)
{
    in_port_t port = defaultPort;

    if (sockAddrId == NULL) {
        log_add("NULL argument");
    }
    else {
        const char* cp = (sockAddrId[0] == '[')
                ? strchr(sockAddrId, ']')
                : sockAddrId;

        if (cp != NULL) {
            cp = strchr(cp, ':');

            if (cp != NULL)
                (void)sscanf(++cp, "%hu", &port);
        }
    }

    return port;
}
