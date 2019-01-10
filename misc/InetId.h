/**
 * This file declares an Internet identifier (i.e., host name or IP address).
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: InetId.h
 *  Created on: Oct 6, 2018
 *      Author: Steven R. Emmerson
 */
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>

#ifndef MISC_INETID_H_
#define MISC_INETID_H_

#ifdef __cplusplus
    extern "C" {
    #define restrict
#endif

typedef struct inetId InetId;

/**
 * Returns a new Internet identifier.
 *
 * @param[in] id    Formatted identifier. May be a host name, a formatted IPv4
 *                  address, or a formatted IPv6 address.
 * @retval    NULL  Failure. `log_add()` called.
 * @return          Internet identifier. Caller should call `inetId_free()` when
 *                  it's no longer needed.
 */
InetId*
inetId_newFromStr(const char* const id);

/**
 * Returns a new Internet identifier.
 *
 * @param[in] family  Address family: either `AF_INET` or `AF_INET6`.
 * @param[in] addr    Internet address. Either `struct in_addr*` or
 *                    `struct in6_addr*`.
 * @retval    NULL    Failure. `log_add()` called.
 * @return            Internet identifier. Caller should call `inetId_free()`
 *                    when it's no longer needed.
 */
InetId*
inetId_newFromAddr(
        const int         family,
        const void* const addr);

/**
 * Frees an Internet identifier.
 *
 * @param[in,out] inetId  Internet identifier or `NULL`.
 */
void
inetId_free(InetId* const inetId);

/**
 * Clones an Internet identifier.
 *
 * @param[in] inetId  Internet identifier to be cloned.
 * @retval    `NULL`  Failure. `log_add()` called.
 * @return            Clone of `inetId`
 */
InetId*
inetId_clone(const InetId* const inetId);

/**
 * Completes an Internet identifier by consulting DNS for missing attributes.
 * Uses DNS every time.
 *
 * @param[in] inetId       Internet identifier
 * @retval    0            Success
 * @retval    EAI_AGAIN    The name could not be resolved at this time.
 *                         Future attempts may succeed. `log_add()` called.
 * @retval    EAI_FAIL     A non-recoverable error occurred when attempting to
 *                         resolve the name. `log_add()` called.
 * @retval    EAI_MEMORY   There was a memory allocation failure. `log_add()`
 *                         called.
 * @retval    EAI_NONAME   The name does not resolve. `log_add()` called.
 * @retval    EAI_OVERFLOW An argument buffer overflowed. `log_add()` called.
 * @retval    EAI_SYSTEM   A system error occurred; the error code can be found
 *                         in `errno`. `log_add()` called.
 */
int
inetId_fill(InetId* const inetId);

/**
 * Returns the address associated with an Internet identifier. Calls
 * `inetId_fill()` if necessary.
 *
 * @param[in,out] inetId  Internet identifier
 * @param[out]    family  Address family. One of `AF_INET` or `AF_INET6`.
 * @param[out]    addr    IP address of `inetId`. Must be capacious enough to
 *                        hold a `struct in_addr` or a `struct in6_addr`.
 * @param[in,out] size    Size of `*addr` in bytes
 * @retval        0       Success. `*family`, `*addr`, and `*size` are set.
 * @return                Error code. See `inetId_fill()` for possible values.
 * @see `inetId_fill()`
 */
const int
inetId_getAddr(
        InetId* const restrict      inetId,
        sa_family_t* const restrict family,
        void* const restrict        addr,
        socklen_t* const restrict   size);

/**
 * Returns the formatted representation of the IP address associated with an
 * Internet identifier. Calls `inetId_fill()` if necessary.
 *
 * @param[in,out] inetId  Internet identifier
 * @retval        NULL    IP address cannot be obtained. `log_add()` called. See
 *                        `inetId_fill()` for `errno` values.
 * @return                Formatted IP address of `inetId`
 * @see `inetId_fill()`
 */
const char*
inetId_getAddrStr(InetId* const inetId);

/**
 * Returns the name associated with an Internet identifier. Calls
 * `inetId_fill()` if necessary.
 *
 * @param[in,out] inetId  Internet identifier
 * @retval        NULL    Name cannot be obtained. `log_add()` called. See
 *                        `inetId_fill()` for `errno` values.
 * @return                Name associated with `inetId`
 * @see `inetId_fill()`
 */
const char*
inetId_getName(InetId* const inetId);

/**
 * Returns the ID of an Internet identifier. The ID is based on what was used
 * for initialization.
 *
 * @param[in] inetId     Internet identifier
 * @return               ID of `inetId`
 */
const char*
inetId_getId(const InetId* const inetId);

/**
 * Indicates if an Internet identifier is based on a host name.
 *
 * @param[in] inetId     Ineternet identifier
 * @retval    `true`     The identifier is based on a host name
 * @retval    `false`    The identifier is not based on a host name
 */
bool
inetId_idIsName(const InetId* const inetId);

/**
 * Compares two Internet identifiers.
 *
 * @param[in] id1  First Internet identifier
 * @param[in] id2  Second Internet identifier
 * @retval    -1   `id1 <  id2`
 * @retval     0   `id1 == id2`
 * @retval     1   `id1 >  id2`
 */
int
inetId_compare(
        const InetId* const id1,
        const InetId* const id2);

/**
 * Returns the Internet socket address corresponding to an Internet identifier
 * and a port number. Calls `inetId_fill()` if necessary.
 *
 * @param[in,out] inetId    Internet identifier
 * @param[in]     port      Port number in host byte order
 * @param[out]    sockAddr  Internet socket address
 * @retval        0         Success. `*sockAddr` is set.
 * @return                  Error code. See `inetId_fill()` for possible values.
 * @see `inetId_fill()`
 */
const int
inetId_initSockAddr(
        InetId* const restrict          inetId,
        const in_port_t                 port,
        struct sockaddr* const restrict sockAddr);

#ifdef __cplusplus
    }
#endif

#endif /* MISC_INETID_H_ */
