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
#include "log.h"

#include <mutex>
#include <set>
#include <thread>

class Authorizer::Impl
{
    InAddrPool inAddrPool;

public:
    /**
     * Constructs.
     */
    explicit Impl(InAddrPool& inAddrPool)
        : inAddrPool{inAddrPool}
    {}

    inline bool isAuthorized(const struct in_addr& clntAddr) const noexcept
    {
        return inAddrPool.isReserved(clntAddr.s_addr);
    }
};

Authorizer::Authorizer(InAddrPool& inAddrPool)
    : pImpl{new Impl(inAddrPool)}
{}

bool Authorizer::isAuthorized(const struct in_addr& clntAddr) const noexcept
{
    return pImpl->isAuthorized(clntAddr);
}

/******************************************************************************
 * C API:
 ******************************************************************************/

void* auth_new(void* inAddrPool)
{
    return new Authorizer(*static_cast<InAddrPool*>(inAddrPool));
}

void auth_delete(void* const authorizer)
{
    delete static_cast<Authorizer*>(authorizer);
}
