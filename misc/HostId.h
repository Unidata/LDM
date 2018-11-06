/**
 * This file declares a host identifier.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: HostId.h
 *  Created on: Oct 6, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>

#ifndef MISC_HOSTID_H_
#define MISC_HOSTID_H_

#ifdef __cplusplus
    extern "C" {
    #define restrict
#endif

typedef struct hostId HostId;

/**
 * Returns a new host identifier.
 *
 * @param[in] id    Formatted identifier. May be a host name, a formatted IPv4
 *                  address, or a formatted IPv6 address.
 * @retval    NULL  Failure. `log_add()` called.
 * @return          Host identifier. Caller should call `hostId_free()` when
 *                  it's no longer needed.
 */
HostId*
hostId_newFromId(const char* const id);

/**
 * Returns a new host identifier.
 *
 * @param[in] family  Address family: either `AF_INET` or `AF_INET6`.
 * @param[in] addr    Host address. Either `struct in_addr*` or
 *                    `struct in6_addr*`.
 * @retval    NULL    Failure. `log_add()` called.
 * @return            Host identifier. Caller should call `hostId_free()` when
 *                    it's no longer needed.
 */
HostId*
hostId_newFromAddr(
        const int         family,
        const void* const addr);

/**
 * Frees a host identifier.
 *
 * @param[in,out] host  Host identifier or `NULL`.
 */
void
hostId_free(HostId* const host);

/**
 * Completes a host identifier by consulting DNS for missing attributes. Uses
 * DNS every time.
 *
 * @param[in] host         Host identifier
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
hostId_fill(HostId* const host);

/**
 * Returns the address associated with a host identifier. Calls `hostId_fill()`
 * if necessary.
 *
 * @param[in,out] host    Host identifier
 * @param[out]    family  Address family. One of `AF_INET` or `AF_INET6`.
 * @param[out]    addr    IP address of host. Must be capacious enough to hold
 *                        a `struct in_addr` or a `struct in6_addr`.
 * @param[in,out] size    Size of `*addr` in bytes
 * @retval        0       Success. `*family`, `*addr`, and `*size` are set.
 * @return                Error code. See `hostId_fill()` for possible values.
 * @see `hostId_fill()`
 */
const int
hostId_getAddr(
        HostId* const restrict      host,
        sa_family_t* const restrict family,
        void* const restrict        addr,
        socklen_t* const restrict   size);

/**
 * Returns the formatted representation of the IP address associated with a host
 * identifier. Calls `hostId_fill()` if necessary.
 *
 * @param[in,out] host  Host identifier
 * @retval        NULL  IP address cannot be obtained. `log_add()` called. See
 *                      `hostId_fill()` for `errno` values.
 * @return              Formatted IP address of the host
 * @see `hostId_fill()`
 */
const char*
hostId_getAddrStr(HostId* const host);

/**
 * Returns the name associated with a host identifier. Calls `hostId_fill()` if
 * necessary.
 *
 * @param[in,out] host  Host identifier
 * @retval        NULL  Name cannot be obtained. `log_add()` called. See
 *                      `hostId_fill()` for `errno` values.
 * @return              Name of the host
 * @see `hostId_fill()`
 */
const char*
hostId_getName(HostId* const host);

/**
 * Returns the ID of a host identifier. The ID is based on what was used for
 * initialization.
 *
 * @param[in] host       Host identifier
 * @return               ID of the host
 */
const char*
hostId_getId(const HostId* const host);

/**
 * Indicates if the identifier is based on a host name.
 *
 * @param[in] host       Host identifier
 * @retval    `true`     The identifier is based on a host name
 * @retval    `false`    The identifier is not based on a host name
 */
bool
hostId_idIsName(const HostId* const host);

/**
 * Compares two host identifiers.
 *
 * @param[in] id1  First host identifier
 * @param[in] id2  Second host identifier
 * @retval    -1   `id1 <  id2`
 * @retval     0   `id1 == id2`
 * @retval     1   `id1 >  id2`
 */
int
hostId_compare(
        const HostId* const id1,
        const HostId* const id2);

/**
 * Returns the Internet socket address corresponding to a host identifier and
 * a port number. Calls `hostId_fill()` if necessary.
 *
 * @param[in,out] host      Host identifier
 * @param[in]     port      Port number in host byte order
 * @param[out]    sockAddr  Internet socket address
 * @retval        0         Success. `*sockAddr` is set.
 * @return                  Error code. See `hostId_fill()` for possible values.
 * @see `hostId_fill()`
 */
const int
hostId_initSockAddr(
        HostId* const restrict          host,
        const in_port_t                 port,
        struct sockaddr* const restrict sockAddr);

#ifdef __cplusplus
    }
#endif

#endif /* MISC_HOSTID_H_ */
