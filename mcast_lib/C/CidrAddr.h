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

#ifndef MCAST_LIB_C_CIDRADDR_H_
#define MCAST_LIB_C_CIDRADDR_H_

#include "ldm.h"

#include <stdbool.h>

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
cidrAddr_construct(
        CidrAddr*       cidrAddr,
        const in_addr_t addr,
        const SubnetLen subnetLen);

/**
 * Returns an allocated CIDR address.
 * @param[in] addr       IPv4 address in network byte-order
 * @param[in] subnetLen  Number of bits in network prefix
 * @retval    NULL       Couldn't allocate instance. `log_add()` called.
 * @return               CIDR address. Caller should call `cidrAddr_delete()`
 *                       when it's no longer needed.
 * @see cidrAddr_delete()
 */
CidrAddr*
cidrAddr_new(
        const in_addr_t addr,
        const SubnetLen subnetLen);

/**
 * Deletes a CIDR address.
 * @param[in] cidrAddr  Instance to be deleted
 * @see cidrAddr_new()
 */
void
cidrAddr_delete(CidrAddr* cidrAddr);

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
size_t cidrAddr_getNumHostAddrs(const CidrAddr* cidrAddr);

/**
 * Parses a CIDR address.
 * @param[in] spec  Formatted CIDR address to be parsed in the form
 *                  `nnn.nnn.nnn.nnn/nn`
 * @retval    NULL  Couldn't parse or allocate instance. `log_add()` called.
 * @return          CIDR address. Caller should call `cidrAddr_delete()` when
 *                  it's no longer needed.
 * @see cidrAddr_delete()
 */
CidrAddr*
cidrAddr_parse(const char* const spec);

/**
 * Formats a CIDR address.
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

#endif /* MCAST_LIB_C_CIDRADDR_H_ */
