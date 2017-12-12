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

#include <mutex>
#include <set>

class Authorizer::Impl
{
    typedef std::mutex             Mutex;
    typedef std::lock_guard<Mutex> LockGuard;

    struct Compare
    {
        bool operator()(
                const struct in_addr& addr1,
                const struct in_addr& addr2) const
        {
            return addr1.s_addr < addr2.s_addr;
        }
    };

    mutable Mutex                     mutex;
    std::set<struct in_addr, Compare> inetAddrs;

public:
    /**
     * Authorizes a client.
     * @param[in] clntInetAddr  Address of the client
     * @exceptionsafety         Strong guarantee
     * @threadsafety            Compatible but not safe
     */
    void authorize(const struct in_addr& clntInetAddr)
    {
        LockGuard lock{mutex};
        inetAddrs.insert(clntInetAddr);
    }

    bool isAuthorized(const struct in_addr& clntInetAddr) const
    {
        LockGuard lock{mutex};
        return inetAddrs.find(clntInetAddr) != inetAddrs.end();
    }

    void unauthorize(const struct in_addr& clntInetAddr)
    {
        LockGuard lock{mutex};
        inetAddrs.erase(clntInetAddr);
    }
};

Authorizer::Authorizer()
    : pImpl{new Impl()}
{}

void Authorizer::authorize(const struct in_addr& clntInetAddr) const
{
    pImpl->authorize(clntInetAddr);
}

bool Authorizer::isAuthorized(const struct in_addr& clntInetAddr) const noexcept
{
    return pImpl->isAuthorized(clntInetAddr);
}

void Authorizer::unauthorize(const struct in_addr& clntInetAddr) const noexcept
{
    pImpl->unauthorize(clntInetAddr);
}

void* auth_new()
{
    return new Authorizer();
}

void auth_unauthorize(
        void* const                 authorizer,
        const struct in_addr* const addr)
{
    static_cast<Authorizer*>(authorizer)->unauthorize(*addr);
}

void auth_free(void* const authorizer)
{
    delete static_cast<Authorizer*>(authorizer);
}
