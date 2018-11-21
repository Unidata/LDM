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
    /// AL2S virtual circuit subnet
    CidrAddr                        al2sSubnet;
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
     * Constructs from a specification of the subnet to be used by FMTP clients.
     *
     * @param[in] cidr               Al2S subnet specification
     */
    Impl(const CidrAddr& cidr)
        : al2sSubnet(cidr)
        // Parentheses are needed to obtain correct construction
        , available(cidrAddr_getNumHostAddrs(&cidr), cidrAddr_getSubnet(&cidr))
        , allocated{}
        , mutex{}
    {
        log_debug(("cidr=" + to_string(cidr)).c_str());
        auto size = available.size();
        // Doesn't include network or broadcast address
        for (in_addr_t i = 1; i <= size; ++i)
            available[i-1] |= htonl(i);
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

        if (cidrAddr_isMember(&al2sSubnet, addr))
            available.push_back(addr);

        log_debug((std::string("Address ") + std::to_string(addr) + " released")
                .c_str());
    }
}; // class InAddrPool::Impl

/******************************************************************************
 * Collection of FMTP client addresses:
 ******************************************************************************/

FmtpClntAddrs::FmtpClntAddrs(const CidrAddr& subnet)
    : pImpl{new Impl(subnet)}
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

void* fmtpClntAddrs_new(const CidrAddr* fmtpSubnet)
{
    return new FmtpClntAddrs{*fmtpSubnet};
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
