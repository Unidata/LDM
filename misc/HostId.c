/**
 * This file implements a host identifier.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: HostId.c
 *  Created on: Oct 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "HostId.h"

#include "log.h"

#include <arpa/inet.h>
#include <limits.h>
#include <netdb.h>
#include <sys/socket.h>

struct hostId {
    union {
        struct in_addr  inAddr;
        struct in6_addr in6Addr;
    }    addr;                              ///< Host's IP address
    char      id[_POSIX_HOST_NAME_MAX+1];   ///< Host's name or IP address
    char      name[_POSIX_HOST_NAME_MAX+1]; ///< Host's name
    char      addrStr[INET6_ADDRSTRLEN];    ///< Host's IP address
    int       family;                       ///< AF_INET or AF_INET6
    socklen_t addrLen;                      ///< Address size in bytes
    bool      idIsName;                     ///< `id` is host name?
};

/**
 * Sets the name of a host identifier based on the host's IP address. Uses DNS
 * every time.
 *
 * @param[in,out] host       Host identifier
 * @retval     0             Success
 * @retval     EAI_AGAIN     The name could not be resolved at this time. Future
 *                           attempts may succeed. `log_add()` called.
 * @retval     EAI_FAIL      A non-recoverable error occurred. `log_add()`
 *                           called.
 * @retval     EAI_MEMORY    There was a memory allocation failure. `log_add()`
 *                           called.
 * @retval     EAI_NONAME    The host's name cannot be located. `log_add()`
 *                           called.
 * @retval     EAI_OVERFLOW  An argument buffer overflowed. `log_add()` called.
 * @retval     EAI_SYSTEM    A system error occurred. The error code can be
 *                           found in `errno`. `log_add()` called.
 */
static int
hostId_setName(HostId* const host)
{
    log_assert(!host->idIsName);

    struct sockaddr sockAddr = {};
    sockAddr.sa_family = host->family;
    (void)memcpy(sockAddr.sa_data, &host->addr, host->addrLen);

    // Don't return numeric address
    int status = getnameinfo(&sockAddr, sizeof(sockAddr), host->name,
            sizeof(host->name), NULL, 0, NI_NAMEREQD);

    if (status)
        log_syserr("");

    return status;
}

/**
 * Sets the IP address fields of a host identifier based on the host's name.
 * Uses DNS every time.
 *
 * @param[in,out] host          Host identifier
 * @retval        0             Success
 * @retval        EAI_AGAIN     The name could not be resolved at this time.
 *                              Future attempts may succeed. `log_add()` called.
 * @retval        EAI_FAIL      A non-recoverable error occurred when attempting
 *                              to resolve the name. `log_add()` called.
 * @retval        EAI_MEMORY    There was a memory allocation failure.
 *                              `log_add()` called.
 * @retval        EAI_NONAME    The name does not resolve. `log_add()` called.
 * @retval        EAI_OVERFLOW  An argument buffer overflowed. `log_add()`
 *                              called.
 * @retval        EAI_SYSTEM    A system error occurred; the error code can be
 *                              found in `errno`. `log_add()` called.
 */
static int
hostId_setAddr(HostId* const host)
{
    log_assert(host->idIsName);

    int              status;
    struct addrinfo* addrInfo;

    struct addrinfo hints = {};
    // The address family must be compatible with the local host
    hints.ai_flags = AI_ADDRCONFIG;

    hints.ai_family = AF_INET6; // Use IPv6 if possible
    status = getaddrinfo(host->id, NULL, &hints, &addrInfo);

    if (status) {
        hints.ai_family = AF_INET;
        status = getaddrinfo(host->id, NULL, &hints, &addrInfo);
    }

    if (status) {
        log_add("Couldn't get address information for host \"%s\"", host->id);
    }
    else {
        host->family = addrInfo->ai_family;
        host->addrLen = addrInfo->ai_addrlen;

        (void)memcpy(&host->addr, addrInfo->ai_addr->sa_data,
                addrInfo->ai_addrlen);
        (void)inet_ntop(host->family, &host->addr, host->addrStr,
                sizeof(host->addrStr)); // Can't fail
        freeaddrinfo(addrInfo);
    }

    return status;
}

HostId*
hostId_newFromId(const char* const id)
{
    HostId* host;

    if (id == NULL) {
        log_add("Host ID is NULL");
        host = NULL;
    }
    else {
        int status;

        host = log_malloc(sizeof(HostId), "host identifier");

        if (host) {
            if (strlen(id)+1 > sizeof(host->id)) {
                log_add("Host ID is too long: \"%s\"", id);
                status = EINVAL;
            }
            else {
                (void)strncpy(host->id, id, sizeof(host->id));

                status = inet_pton(AF_INET6, id, &host->addr);

                if (status == 1) {
                    host->idIsName = false;
                    host->family = AF_INET6;
                    host->addrLen = sizeof(struct in6_addr);
                    status = 0;
                }
                else {
                    status = inet_pton(AF_INET, id, &host->addr);

                    if (status == 1) {
                        host->idIsName = false;
                        host->family = AF_INET;
                        host->addrLen = sizeof(struct in_addr);
                    }
                    else {
                        // Must be the name of a host
                        host->idIsName = true;
                    }

                    status = 0;
                }
            } // Host ID argument isn't too long

            if (status) {
                free(host);
                host = NULL;
            }
        } // Host identifier allocated
    } // Non-null host ID argument

    return host;
}

HostId*
hostId_newFromAddr(
        const int         family,
        const void* const addr)
{
    HostId* host;

    if (family != AF_INET && family != AF_INET6) {
        log_add("Invalid address family: %d", family);
        host = NULL;
    }
    else if (addr == NULL) {
        log_add("NULL address argument");
        host = NULL;
    }
    else {
        host = log_malloc(sizeof(HostId), "host identifier");

        if (host) {
            host->idIsName = false;
            host->family = family;
            host->addrLen = (family == AF_INET)
                    ? sizeof(struct in_addr)
                    : sizeof(struct in6_addr);

            (void)memcpy(&host->addr, addr, host->addrLen);
            // Can't fail
            (void)inet_ntop(family, addr, host->id, sizeof(host->id));
        } // Host ID allocated
    } // Valid arguments

    return host;
}

void
hostId_free(HostId* const host)
{
    free(host);
}

int
hostId_fill(HostId* const host)
{
    int status;

    if (host->idIsName) {
        status = hostId_setAddr(host);

        if (status)
            log_add("Can't get IP address of host \"%s\"", host->id);
    }
    else {
        status = hostId_setName(host);

        if (status)
            log_add("Can't get name of host \"%s\"", host->id);
    }

    return status;
}

const char*
hostId_getName(HostId* const host)
{
    const char* name;

    if (host->idIsName) {
        name = host->id;
    }
    else {
        int status = hostId_setName(host);

        if (status) {
            errno = status;
            name = NULL;
        }
        else {
            name = host->name;
        }
    }

    return name;
}

const int
hostId_getAddr(
        HostId* const restrict      host,
        sa_family_t* const restrict family,
        void* const restrict        addr,
        socklen_t* const restrict   size)
{
    int status = host->idIsName ? hostId_setAddr(host) : 0;

    if (status == 0) {
        if (*size < host->addrLen) {
            log_add("Receiving address is too small");
            status = EAI_OVERFLOW;
        }
        else {
            (void)memcpy(addr, &host->addr, host->addrLen);
            *family = host->family;
            *size = host->addrLen;
            status = 0;
        }
    }

    return status;
}

const char*
hostId_getAddrStr(HostId* const host)
{
    const char* addrStr;
    int         status = host->idIsName ? hostId_setAddr(host) : 0;

    if (status) {
        errno = status;
        addrStr = NULL;
    }
    else {
        addrStr = host->addrStr;
    }

    return addrStr;
}

const int
hostId_initSockAddr(
        HostId* const restrict          host,
        const in_port_t                 port,
        struct sockaddr* const restrict sockAddr)
{
    int status = host->idIsName ? hostId_setAddr(host) : 0;

    if (status == 0) {
        if (host->family == AF_INET) {
            struct sockaddr_in* const sa = (struct sockaddr_in*)sockAddr;

            (void)memset(sa, 0, sizeof(*sa));
            sa->sin_addr = host->addr.inAddr;
            sa->sin_family = host->family;
            sa->sin_port = htons(port);
        }
        else {
            struct sockaddr_in6* const sa = (struct sockaddr_in6*)sockAddr;

            (void)memset(sa, 0, sizeof(*sa));
            sa->sin6_addr = host->addr.in6Addr;
            sa->sin6_family = host->family;
            sa->sin6_port = htons(port);
        }
    }

    return status;
}

const char*
hostId_getId(const HostId* const restrict host)
{
    return host->id;
}

bool
hostId_idIsName(const HostId* const host)
{
    return host->idIsName;
}

int
hostId_compare(
        const HostId* const id1,
        const HostId* const id2)
{
    return strcmp(id1->id, id2->id);
}
