/**
 * This file implements a collection of addresses used by FMTP clients.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: FmtpClntAddrs.cpp
 *  Created on: Oct 29, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "CidrAddr.h"
#include "FmtpClntAddrs.h"
#include "log.h"

#include <deque>
#include <mutex>
#include <unordered_set>
#include <stdexcept>

/******************************************************************************
 * Misc functions:
 ******************************************************************************/

static std::string to_string(const in_addr_t addr)
{
    char buf[INET_ADDRSTRLEN];
    return std::string(::inet_ntop(AF_INET, &addr, buf, sizeof(buf)));
}

static std::string to_string(const CidrAddr& addr)
{
    return std::string{to_string(addr.addr)} +
            "/" + std::to_string(addr.prefixLen);
}

/******************************************************************************
 * Pool of potential FMTP client addresses on an AL2S virtual circuit:
 ******************************************************************************/

class FmtpClntAddrs::Impl final
{
    /// FMTP server address and subnet
    CidrAddr                        fmtpSrvr;
    /// Available IP addresses
    std::deque<in_addr_t>           available;
    /// Allocated IP addresses. Includes explicitly-allowed and pool addresses
    std::unordered_set<in_addr_t>   allocated;
    /// Concurrency support
    typedef std::mutex              Mutex;
    typedef std::lock_guard<Mutex>  LockGuard;
    mutable Mutex                   mutex;

public:
    /**
     * Constructs from a specification of the FMTP server address and subnet.
     *
     * @param[in] fmtpSrvr FMTP server address and subnet
     */
    Impl(const CidrAddr& fmtpSrvr)
        : fmtpSrvr(fmtpSrvr)
          // Parentheses are needed to obtain correct construction
        , available(cidrAddr_getNumHostAddrs(&fmtpSrvr) - 1,
                cidrAddr_getSubnet(&fmtpSrvr))
        , allocated{}
        , mutex{}
    {
        log_debug("fmtpSrvr=%s", to_string(fmtpSrvr).c_str());

        in_addr_t subnet = cidrAddr_getSubnet(&fmtpSrvr);

        if (subnet == cidrAddr_getAddr(&fmtpSrvr))
            throw std::invalid_argument("FMTP server address mustn't be same "
                    "as subnet address, " + to_string(subnet));

        SubnetLen suffixLen = 32 - cidrAddr_getPrefixLen(&fmtpSrvr);
        in_addr_t bcastAddr = subnet | ((1 << suffixLen) - 1);

        if (cidrAddr_getSubnet(&fmtpSrvr) == cidrAddr_getAddr(&fmtpSrvr))
            throw std::invalid_argument("FMTP server address mustn't be same "
                    "as broadcast address, " + to_string(bcastAddr));

        auto      size = available.size();
        in_addr_t fmtpSrvrAddr = cidrAddr_getAddr(&fmtpSrvr);

        // Doesn't include network, broadcast, and FMTP server addresses
        for (in_addr_t i = 1, j = 0; i <= size; ++i) {
            const in_addr_t addr = available[j] | htonl(i);

            if (addr != fmtpSrvrAddr) {
                available[j] = addr;
                ++j;
            }
        }
    }

    /**
     * Returns an available (i.e., unused) address for an FMTP client on an AL2S
     * virtual circuit.
     *
     * @return                    Reserved address in network byte-order
     * @throw std::out_of_range   No address is available
     * @threadsafety              Safe
     * @exceptionsafety           Strong guarantee
     */
    in_addr_t getAvailable()
    {
        LockGuard lock{mutex};
        in_addr_t addr = {available.at(0)};
        available.pop_front();
        allocated.insert(addr);
        return addr;
    }

    /**
     * Explicitly allows an FMTP client to connect.
     *
     * @param[in] addr  IPv4 address of FMTP client in network byte order
     */
    void allow(const in_addr_t addr)
    {
        LockGuard lock{mutex};
        allocated.insert(addr);
    }

    /**
     * Indicates if an IP address for an FMTP client is allowed to connect. Both
     * explicitly allowed addresses and implicitly allocated addresses are
     * checked.
     *
     * @param[in] addr      IP address to check
     * @retval    `true`    IP address is allowed
     * @retval    `false`   IP address is not allowed
     * @threadsafety        Safe
     * @exceptionsafety     Nothrow
     */
    bool isAllowed(const in_addr_t addr) const noexcept
    {
        LockGuard lock{mutex};
        bool      found = allocated.find(addr) != allocated.end();

        if (!found) {
            char buf[INET_ADDRSTRLEN];
            log_debug("Address %s not found", ::inet_ntop(AF_INET, &addr, buf,
                    sizeof(buf)));
        }

        return found;
    }

    /**
     * Releases an address of an FMTP client so that it is no longer allowed to
     * connect.
     *
     * @param[in] addr          Address to be released in network byte-order
     * @throw std::logic_error  `addr` wasn't previously allowed
     * @threadsafety            Safe
     * @exceptionsafety         Strong guarantee
     */
    void release(const in_addr_t addr)
    {
        LockGuard lock{mutex};
        auto      iter = allocated.find(addr);

        if (iter == allocated.end())
            throw std::logic_error("IP address " + to_string(addr) +
                    " wasn't previously reserved");

        allocated.erase(iter);

        if (cidrAddr_isMember(&fmtpSrvr, addr))
            available.push_back(addr);

        log_debug("Address %s released", to_string(addr).c_str());
    }
}; // class InAddrPool::Impl

/******************************************************************************
 * Collection of FMTP client addresses:
 ******************************************************************************/

FmtpClntAddrs::FmtpClntAddrs(const CidrAddr& fmtpSrvr)
    : pImpl{new Impl(fmtpSrvr)}
{}

in_addr_t FmtpClntAddrs::getAvailable() const
{
    return pImpl->getAvailable();
}

void FmtpClntAddrs::allow(const in_addr_t addr) const
{
    return pImpl->allow(addr);
}

bool FmtpClntAddrs::isAllowed(const in_addr_t addr) const noexcept
{
    return pImpl->isAllowed(addr);
}

void FmtpClntAddrs::release(const in_addr_t addr) const
{
    pImpl->release(addr);
}

/******************************************************************************
 * C interface:
 ******************************************************************************/

void* fmtpClntAddrs_new(const CidrAddr* const fmtpSrvr)
{
    return new FmtpClntAddrs(*fmtpSrvr);
}

in_addr_t fmtpClntAddrs_getAvailable(const void* const fmtpClntAddrs)
{
    try {
        return static_cast<const FmtpClntAddrs*>(fmtpClntAddrs)->getAvailable();
    }
    catch (const std::exception& ex) {
        return INADDR_ANY;
    }
}

void fmtpClntAddrs_allow(
        const void* const fmtpClntAddrs,
        const in_addr_t   addr)
{
    static_cast<const FmtpClntAddrs*>(fmtpClntAddrs)->allow(addr);
}

bool fmtpClntAddrs_isAllowed(
        const void* const fmtpClntAddrs,
        const in_addr_t   addr)
{
    return static_cast<const FmtpClntAddrs*>(fmtpClntAddrs)->isAllowed(addr);
}

void fmtpClntAddrs_free(void* const fmtpClntAddrs)
{
    delete static_cast<FmtpClntAddrs*>(fmtpClntAddrs);
}
