/**
 * This file declares an Internet socket address.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: InetSockAddr.h
 *  Created on: Oct 9, 2018
 *      Author: Steven R. Emmerson
 */
#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>
#include "InetId.h"

#ifndef MISC_INETSOCKADDR_H_
#define MISC_INETSOCKADDR_H_

typedef struct inetSockAddr InetSockAddr;

#ifdef __cplusplus
extern "C" {
#define restrict
#endif

/**
 * Returns a new Internet socket identifier.
 *
 * @param[in] id    Internet socket identifier in one of the following forms:
 *                    - <hostname>[:<port>]
 *                    - <IPv4 address>[:<port>]
 *                    - '['<IPv6 address>']'[:<port>]
 * @param[in] port  Default port number in host byte order if it's not specified
 *                  in `id`
 * @retval    NULL  Failure. `log_add()` called.
 * @return          New Internet socket identifier
 */
InetSockAddr*
isa_newFromId(
        const char* id,
        in_port_t   port);

#if 0
InetSockAddr*
isa_newFromAddr(
        const void* const addr,
        const socklen_t   size,
        const in_port_t   port);
#endif

/**
 * Frees an Internet socket address.
 *
 * @param[in,out] isa  Internet socket address to be freed or `NULL`
 */
void
isa_free(InetSockAddr* isa);

/**
 * Returns a string representation of an Internet socket address.
 *
 * @param[in,out] isa  Internet socket address
 * @retval        NULL Failure. `log_add()` called.
 * @return             String representation. Caller must *not* free.
 */
const char*
isa_toString(InetSockAddr* isa);

/**
 * Returns a copy of an Internet socket address.
 *
 * @param[in] isa   Internet socket address to be copied
 * @retval    NULL  Failure. `log_add()` called.
 * @return          Copy of `isa`
 */
InetSockAddr*
isa_clone(const InetSockAddr* const isa);

/**
 * Sets a `struct sockaddr` to correspond to an Internet socket address.
 *
 * @param[in]     isa       Internet socket address
 * @param[out]    sockaddr  Socket address to be set
 * @param[in,out] socklen   Length of socket address in bytes
 * @retval        0         Success. The address portion of `*sockaddr` is set and
 *                          `*socklen` is set. NB: Only the address portion is set.
 * @retval        EINVAL    Invalid address family. `log_add()` called.
 */
int
isa_getSockAddr(
        InetSockAddr* const restrict    isa,
        struct sockaddr* const restrict sockaddr,
        socklen_t* const restrict       socklen);

/**
 * Returns a string representation of the address portion of an Internet socket
 * address.
 *
 * @param[in,out] isa         Internet socket address
 * @retval        NULL        Failure. `log_add()` called.
 * @return                    String representation of the Internet address
 */
const char*
isa_getInetAddrStr(const InetSockAddr* const restrict isa);

/**
 * Returns the address identifier portion of an Internet socket address.
 *
 * @param[in] isa Internet socket address
 * @return        Internet address identifier
 */
const InetId*
isa_getInetId(const InetSockAddr* const isa);

/**
 * Returns the port number of an Internet socket address.
 *
 * @param[in] isa  Internet socket address
 * @return         Port number of `isa` in host byte order
 */
in_port_t
isa_getPort(const InetSockAddr* const isa);

/**
 * Sets the port number of an Internet socket address.
 *
 * @param[in,out] isa     Internet socket address
 * @param[in]     port    New port number in host byte order
 * @retval        0       Success
 * @retval        EINVAL  Invalid `isa`. `log_add()` called.
 */
int
isa_setPort(
        InetSockAddr* const isa,
        const in_port_t     port);

/**
 * Compares two Internet socket addresses.
 *
 * @param[in] isa1  First Internet socket address
 * @param[in] isa2  Second Internet socket address
 * @retval    -1    `isa1 <  isa2`
 * @retval     0    `isa1 == isa2`
 * @retval     1    `isa1 >  isa2`
 */
int
isa_compare(
        const InetSockAddr* const isa1,
        const InetSockAddr* const isa2);

/**
 * Initializes a socket address structure from an Internet socket address.
 *
 * @param[in]  isa          Internet socket address
 * @param[in]  family       Address family. One of `AF_UNSPEC`, `AF_INET`, or
 *                          `AF_INET6`.
 * @param[in]  forBind      Is the address intended for a call to `bind()`?
 * @param[out] sockAddr     Socket address structure to be initialized
 * @retval     0            Success. `*sockAddr` is set.
 * @retval     EAI_AGAIN    The name could not be resolved at this time.
 *                          Future attempts may succeed. `log_add()` called.
 * @retval     EAI_FAIL     A non-recoverable error occurred when attempting to
 *                          resolve the name. `log_add()` called.
 * @retval     EAI_MEMORY   There was a memory allocation failure. `log_add()`
 *                          called.
 * @retval     EAI_NONAME   The name does not resolve. `log_add()` called.
 * @retval     EAI_OVERFLOW An buffer overflowed. `log_add()` called.
 * @retval     EAI_SYSTEM   A system error occurred; the error code can be found
 *                          in `errno`. `log_add()` called.
 */
int
isa_initSockAddr(
        const InetSockAddr* const restrict isa,
        const int                          family,
        const bool                         forBind,
        struct sockaddr* const restrict    sockAddr);

/**
 * Initializes a `struct sockaddr` from an Internet socket address identifier
 * and a default port number.
 *
 * @param[out] sockAddr      Socket address structure to be initialied
 * @param[in]  id            Identifier in one of the following forms:
 *                             - <hostname>[:<port>]
 *                             - <IPv4 address>[:<port>]
 *                             - '['<IPv6 address>']'[:<port>]
 * @param[in]  port          Default port number if it's not specified in `id`
 * @param[in]  family        IP address family. One of `AF_UNSPEC`, `AF_INET` or
 *                           `AF_INET6`. This function will fail if the local
 *                           host doesn't support the requested address family.
 * @param[in]  forBind       Socket address is intended for `bind()`
 * @retval     0             Success. `*sockAddr` is initialized.
 * @retval     EINVAL        Invalid `sockaddr`, `id`, or `family`. `log_add()`
 *                           called.
 * @retval     EAI_AGAIN     The name could not be resolved at this time. Future
 *                           attempts may succeed. `log_add()` called.
 * @retval     EAI_FAIL      A non-recoverable error occurred. `log_add()`
 *                           called.
 * @retval     EAI_MEMORY    There was a memory allocation failure. `log_add()`
 *                           called.
 * @retval     EAI_NONAME    The Internet identifier's name cannot be located.
 *                           `log_add()` called.
 * @retval     EAI_OVERFLOW  A buffer overflowed. `log_add()` called.
 * @retval     EAI_SYSTEM    A system error occurred. The error code can be
 *                           found in `errno`. `log_add()` called.
 */
int
isa_initFromId(
        struct sockaddr* const restrict sockAddr,
        const char* const restrict      id,
        in_port_t                       port,
        const int                       family,
        const bool                      forBind);

/**
 * Extracts and returns the IP address portion of an Internet socket address.
 *
 * @param[in] sockAddrId  Internet socket address
 * @retval    `NULL`      Failure. `log_add()` called.
 * @return                IP address portion of `sockAddrId`. Caller should
 *                        free when it's no longer needed.
 */
char*
isa_getIpAddrId(const char* const sockAddrId);

/**
 * Returns the port number of an Internet socket address.
 *
 * @param[in] sockAddrId   Internet socket address
 * @param[in] defaultPort  Default port to return if `sockAddrId` doesn't
 *                         specify
 * @return                 Port number in host byte order
 */
in_port_t
isa_getPortFromId(
        const char* const sockAddrId,
        const in_port_t   defaultPort);

#ifdef __cplusplus
}
#endif

#endif /* MISC_INETSOCKADDR_H_ */
