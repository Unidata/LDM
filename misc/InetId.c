/**
 * This file implements an Internet identifier (i.e., either a host name or an
 * IP address).
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: InetId.c
 *  Created on: Oct 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "InetId.h"

#include "log.h"

#include <arpa/inet.h>
#include <limits.h>
#include <netdb.h>
#include <sys/socket.h>

struct inetId {
    union {
        struct in_addr  inAddr;
        struct in6_addr in6Addr;
    }    addr;                              ///< IP address
    char      id[_POSIX_HOST_NAME_MAX+1];   ///< Name or IP address
    char      name[_POSIX_HOST_NAME_MAX+1]; ///< Name
    char      addrStr[INET6_ADDRSTRLEN];    ///< IP address
    int       family;                       ///< AF_INET or AF_INET6
    socklen_t addrLen;                      ///< Address size in bytes
    bool      idIsName;                     ///< `id` is name?
};

/**
 * Sets the name of an Internet identifier based on the IP address. Uses DNS
 * every time.
 *
 * @param[in,out] inetId     Internet identifier
 * @retval     0             Success
 * @retval     EAI_AGAIN     The name could not be resolved at this time. Future
 *                           attempts may succeed. `log_add()` called.
 * @retval     EAI_FAIL      A non-recoverable error occurred. `log_add()`
 *                           called.
 * @retval     EAI_MEMORY    There was a memory allocation failure. `log_add()`
 *                           called.
 * @retval     EAI_NONAME    The name cannot be located. `log_add()` called.
 * @retval     EAI_OVERFLOW  An argument buffer overflowed. `log_add()` called.
 * @retval     EAI_SYSTEM    A system error occurred. The error code can be
 *                           found in `errno`. `log_add()` called.
 */
static int
inetId_setName(InetId* const inetId)
{
    log_assert(!inetId->idIsName);

    struct sockaddr sockAddr = {};
    sockAddr.sa_family = inetId->family;
    (void)memcpy(sockAddr.sa_data, &inetId->addr, inetId->addrLen);

    // Don't return numeric address
    int status = getnameinfo(&sockAddr, sizeof(sockAddr), inetId->name,
            sizeof(inetId->name), NULL, 0, NI_NAMEREQD);

    if (status)
        log_syserr("");

    return status;
}

/**
 * Sets the IP address fields of an Internet identifier based on the name. Uses
 * DNS every time.
 *
 * @param[in,out] inetId        Internet identifier
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
inetId_setAddr(InetId* const inetId)
{
    log_assert(inetId->idIsName);

    int              status;
    struct addrinfo* addrInfo;

    struct addrinfo hints = {};
    // The address family must be compatible with the local host
    hints.ai_flags = AI_ADDRCONFIG;

    hints.ai_family = AF_INET;
    status = getaddrinfo(inetId->id, NULL, &hints, &addrInfo);

    if (status) {
        hints.ai_family = AF_INET6; // Use IPv6 as a second choice
        status = getaddrinfo(inetId->id, NULL, &hints, &addrInfo);
    }

    if (status) {
        log_add("Couldn't get IP address information for \"%s\"", inetId->id);
    }
    else {
        inetId->family = addrInfo->ai_family;
        inetId->addrLen = addrInfo->ai_addrlen;

        (void)memcpy(&inetId->addr, addrInfo->ai_addr->sa_data,
                addrInfo->ai_addrlen);
        (void)inet_ntop(inetId->family, &inetId->addr, inetId->addrStr,
                sizeof(inetId->addrStr)); // Can't fail
        freeaddrinfo(addrInfo);
    }

    return status;
}

InetId*
inetId_newFromStr(const char* const id)
{
    InetId* inetId;

    if (id == NULL) {
        log_add("Internet ID is NULL");
        inetId = NULL;
    }
    else {
        int status;

        inetId = log_malloc(sizeof(InetId), "Internet identifier");

        if (inetId) {
            if (strlen(id)+1 > sizeof(inetId->id)) {
                log_add("ID is too long: \"%s\"", id);
                status = EINVAL;
            }
            else {
                (void)strncpy(inetId->id, id, sizeof(inetId->id));

                status = inet_pton(AF_INET6, id, &inetId->addr);

                if (status == 1) {
                    inetId->idIsName = false;
                    inetId->family = AF_INET6;
                    inetId->addrLen = sizeof(struct in6_addr);
                    status = 0;
                }
                else {
                    status = inet_pton(AF_INET, id, &inetId->addr);

                    if (status == 1) {
                        inetId->idIsName = false;
                        inetId->family = AF_INET;
                        inetId->addrLen = sizeof(struct in_addr);
                    }
                    else {
                        // Must be a name
                        inetId->idIsName = true;
                    }

                    status = 0;
                }
            } // ID argument isn't too long

            if (status) {
                free(inetId);
                inetId = NULL;
            }
        } // Internet identifier allocated
    } // Non-null ID argument

    return inetId;
}

InetId*
inetId_newFromAddr(
        const int         family,
        const void* const addr)
{
    InetId* inetId;

    if (family != AF_INET && family != AF_INET6) {
        log_add("Invalid address family: %d", family);
        inetId = NULL;
    }
    else if (addr == NULL) {
        log_add("NULL address argument");
        inetId = NULL;
    }
    else {
        inetId = log_malloc(sizeof(InetId), "Internet identifier");

        if (inetId) {
            inetId->idIsName = false;
            inetId->family = family;
            inetId->addrLen = (family == AF_INET)
                    ? sizeof(struct in_addr)
                    : sizeof(struct in6_addr);

            (void)memcpy(&inetId->addr, addr, inetId->addrLen);
            // Can't fail
            (void)inet_ntop(family, addr, inetId->id, sizeof(inetId->id));
        } // Internet ID allocated
    } // Valid arguments

    return inetId;
}

void
inetId_free(InetId* const inetId)
{
    free(inetId);
}

InetId*
inetId_clone(const InetId* const inetId)
{
    return inetId_newFromStr(inetId->id);
}

int
inetId_fill(InetId* const inetId)
{
    int status;

    if (inetId->idIsName) {
        status = inetId_setAddr(inetId);

        if (status)
            log_add("Can't get IP address of \"%s\"", inetId->id);
    }
    else {
        status = inetId_setName(inetId);

        if (status)
            log_add("Can't get name of \"%s\"", inetId->id);
    }

    return status;
}

const char*
inetId_getName(InetId* const inetId)
{
    const char* name;

    if (inetId->idIsName) {
        name = inetId->id;
    }
    else {
        int status = inetId_setName(inetId);

        if (status) {
            errno = status;
            name = NULL;
        }
        else {
            name = inetId->name;
        }
    }

    return name;
}

const int
inetId_getAddr(
        InetId* const restrict      inetId,
        sa_family_t* const restrict family,
        void* const restrict        addr,
        socklen_t* const restrict   size)
{
    int status = inetId->idIsName ? inetId_setAddr(inetId) : 0;

    if (status == 0) {
        if (*size < inetId->addrLen) {
            log_add("Receiving address is too small");
            status = EAI_OVERFLOW;
        }
        else {
            (void)memcpy(addr, &inetId->addr, inetId->addrLen);
            *family = inetId->family;
            *size = inetId->addrLen;
            status = 0;
        }
    }

    return status;
}

const char*
inetId_getAddrStr(InetId* const inetId)
{
    const char* addrStr;
    int         status = inetId->idIsName ? inetId_setAddr(inetId) : 0;

    if (status) {
        errno = status;
        addrStr = NULL;
    }
    else {
        addrStr = inetId->addrStr;
    }

    return addrStr;
}

const int
inetId_initSockAddr(
        InetId* const restrict          inetId,
        const in_port_t                 port,
        struct sockaddr* const restrict sockAddr)
{
    int status = inetId->idIsName ? inetId_setAddr(inetId) : 0;

    if (status == 0) {
        if (inetId->family == AF_INET) {
            struct sockaddr_in* const sa = (struct sockaddr_in*)sockAddr;

            (void)memset(sa, 0, sizeof(*sa));
            sa->sin_addr = inetId->addr.inAddr;
            sa->sin_family = inetId->family;
            sa->sin_port = htons(port);
        }
        else {
            struct sockaddr_in6* const sa = (struct sockaddr_in6*)sockAddr;

            (void)memset(sa, 0, sizeof(*sa));
            sa->sin6_addr = inetId->addr.in6Addr;
            sa->sin6_family = inetId->family;
            sa->sin6_port = htons(port);
        }
    }

    return status;
}

const char*
inetId_getId(const InetId* const restrict inetId)
{
    return inetId->id;
}

bool
inetId_idIsName(const InetId* const inetId)
{
    return inetId->idIsName;
}

int
inetId_compare(
        const InetId* const id1,
        const InetId* const id2)
{
    return strcmp(id1->id, id2->id);
}
