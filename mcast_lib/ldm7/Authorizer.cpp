/**
 * This file implements a class that authorizes connections from client FMTP
 * layers to the server FMTP layer for data-block recovery.
 *
 *        File: Authorizer.h
 *  Created on: Dec 11, 2017
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "Authorizer.h"
#include "FixedDelayQueue.h"
#include <LdmConfFile.h>
#include "inetutil.h"
#include "log.h"

#include <mutex>
#include <set>
#include <thread>

class Authorizer::Impl
{
    FmtpClntAddrs fmtpClntAddrs;
    feedtypet  feed;

public:
    /**
     * Constructs.
     */
    explicit Impl(FmtpClntAddrs& addrs, const feedtypet feed)
        : fmtpClntAddrs{addrs}
        , feed{feed}
    {}

    inline bool isAuthorized(const struct in_addr& clntAddr) const noexcept
    {
        bool authorized = fmtpClntAddrs.isAllowed(clntAddr.s_addr);

        if (!authorized) {
            struct sockaddr_in sockAddrIn = {};

            sockAddrIn.sin_family = AF_INET;
            sockAddrIn.sin_addr = clntAddr;
            const char* name = hostbyaddr(&sockAddrIn);

            authorized = lcf_getAllowed(name, &clntAddr, feed) ==
                    feed;
        }

        return authorized;
    }
};

Authorizer::Authorizer(
        FmtpClntAddrs&  addrs,
        const feedtypet feed)
    : pImpl{new Impl(addrs, feed)}
{}

bool Authorizer::isAuthorized(const struct in_addr& clntAddr) const noexcept
{
    return pImpl->isAuthorized(clntAddr);
}

/******************************************************************************
 * C API:
 ******************************************************************************/

void* auth_new(
        void*           inAddrPool,
        const feedtypet feed)
{
    return new Authorizer(*static_cast<FmtpClntAddrs*>(inAddrPool), feed);
}

void auth_delete(void* const authorizer)
{
    delete static_cast<Authorizer*>(authorizer);
}
