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
#include "inetutil.h"
#include "ldm_config_file.h"
#include "log.h"

#include <mutex>
#include <set>
#include <thread>

class Authorizer::Impl
{
    InAddrPool inAddrPool;
    feedtypet  feed;

public:
    /**
     * Constructs.
     */
    explicit Impl(InAddrPool& inAddrPool, const feedtypet feed)
        : inAddrPool{inAddrPool}
        , feed{feed}
    {}

    inline bool isAuthorized(const struct in_addr& clntAddr) const noexcept
    {
        bool authorized = inAddrPool.isReserved(clntAddr.s_addr);

        if (!authorized) {
            struct sockaddr_in sockAddrIn = {};

            sockAddrIn.sin_family = AF_INET;
            sockAddrIn.sin_addr = clntAddr;
            const char* name = hostbyaddr(&sockAddrIn);

            authorized = lcf_reduceByAllowedFeeds(name, &clntAddr, feed) ==
                    feed;
        }

        return authorized;
    }
};

Authorizer::Authorizer(
        InAddrPool&     inAddrPool,
        const feedtypet feed)
    : pImpl{new Impl(inAddrPool, feed)}
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
    return new Authorizer(*static_cast<InAddrPool*>(inAddrPool), feed);
}

void auth_delete(void* const authorizer)
{
    delete static_cast<Authorizer*>(authorizer);
}
