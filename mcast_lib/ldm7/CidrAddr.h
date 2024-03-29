/**
 * This file declares a Classless Inter-Domain Routing (CIDR) address.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: CiddrAddr.h
 *  Created on: Mar 8, 2018
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_LDM7_CIDRADDR_H_
#define MCAST_LIB_LDM7_CIDRADDR_H_

#include "ldm.h"

#include <netinet/in.h>
#include <stdbool.h>

#define CIDRSTRLEN (INET_ADDRSTRLEN + 1 + 2) // "xxx.xxx.xxx.xxx/nn\0"

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Constructs a CIDR address.
 * @param[out] cidrAddr   CIDR address to be constructed
 * @param[in]  addr       IPv4 address in network byte-order
 * @param[in]  subnetLen  Number of bits in network prefix
 */
bool
cidrAddr_init(
        CidrAddr*       cidrAddr,
        const in_addr_t addr,
        const SubnetLen subnetLen);

/**
 * Destroys a CIDR address.
 *
 * @param[in,out] cidrAddr  CIDR address to be destroyed
 */
void
cidrAddr_destroy(CidrAddr* const cidrAddr);

/**
 * Returns an allocated CIDR address.
 * @param[in] addr       IPv4 address in network byte-order
 * @param[in] subnetLen  Number of bits in network prefix
 * @retval    NULL       Couldn't allocate instance. `log_add()` called.
 * @return               CIDR address. Caller should call `cidrAddr_free()`
 *                       when it's no longer needed.
 * @see cidrAddr_free()
 */
CidrAddr*
cidrAddr_new(
        const in_addr_t addr,
        const SubnetLen subnetLen);

/**
 * Frees a CIDR address.
 *
 * @param[in] cidrAddr  Instance to be freed
 * @see cidrAddr_new()
 */
void
cidrAddr_free(CidrAddr* cidrAddr);

/**
 * Returns the subnet mask of a CIDR address.
 * @param[in] cidrAddr  CIDR address
 * @return              Network mask in network byte order
 */
in_addr_t
cidrAddr_getSubnetMask(const CidrAddr* cidrAddr);

/**
 * Returns the subnet of a CIDR address (i.e., the address anded with the subnet
 * mask).
 * @param[in] cidrAddr  CIDR address
 * @return              Subnet address in network byte order
 */
in_addr_t
cidrAddr_getSubnet(const CidrAddr* cidrAddr);

/**
 * Indicates if an address is a valid member of a CIDR address.
 * @param[in] cidrAddr  CIDR address
 * @param[in] addr      Possible address in network byte order
 * @retval    `true`    Iff address is valid member of CIDR address
 */
bool
cidrAddr_isMember(
        const CidrAddr* cidrAddr,
        const in_addr_t addr);

/**
 * Returns the network address.
 * @param[in] cidrAddr  CIDR address
 * @return              IPv4 address in network byte-order
 */
in_addr_t
cidrAddr_getAddr(const CidrAddr* cidrAddr);

/**
 * Returns the number of bits in the network prefix.
 * @param[in] cidrAddr  CIDR address
 * @return              Number of bits in network prefix
 */
SubnetLen
cidrAddr_getPrefixLen(const CidrAddr* cidrAddr);

/**
 * Copies a CIDR address.
 *
 * @param[out] lhs   CIDR address to be set
 * @param[in]  rhs   CIDR address to be copied
 * @retval     NULL  `rhs` is invalid
 * @return           `lhs`
 */
CidrAddr*
cidrAddr_copy(
        CidrAddr* lhs,
        const CidrAddr* rhs);

/**
 * Returns the number of IPv4 host addresses -- excluding the network address
 * and the broadcast address.
 * @param[in] cidrAddr  Subnet specification
 * @return              Number of addresses
 */
uint32_t cidrAddr_getNumHostAddrs(const CidrAddr* cidrAddr);

/**
 * Parses a CIDR address.
 * @param[in] spec  Formatted CIDR address to be parsed in the form
 *                  `nnn.nnn.nnn.nnn/nn`
 * @retval    NULL  Couldn't parse or allocate instance. `log_add()` called.
 * @return          CIDR address. Caller should call `cidrAddr_free()` when
 *                  it's no longer needed.
 * @see cidrAddr_free()
 */
CidrAddr*
cidrAddr_parse(const char* const spec);

/**
 * Formats a CIDR address into a user-supplied buffer.
 *
 * @param[in]  addr  CIDR address
 * @param[out] buf   Buffer to contain the string representation
 * @param[in]  size  Size of the buffer in bytes. Should be at least
 *                   `CIDRSTRLEN`.
 * @return           Number of bytes in string representation excluding the
 *                   terminating NUL byte
 */
int
cidrAddr_snprintf(
        const CidrAddr* const addr,
        char* const           buf,
        const size_t          size);

/**
 * Returns a string representation of a CIDR address.
 * @param[in] addr  CIDR address to be formatted
 * @retval NULL     Couldn't format address. `log_add()` called.
 * @return          Formatted address in the form `nnn.nnn.nnn.nnn/nn`. Caller
 *                  should call `free()` when it's no longer needed.
 */
char*
cidrAddr_format(const CidrAddr* addr);

#ifdef __cplusplus
    }
#endif

#endif /* MCAST_LIB_LDM7_CIDRADDR_H_ */
