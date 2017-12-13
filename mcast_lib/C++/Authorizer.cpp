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
                const struct sockaddr_in& addr1,
                const struct sockaddr_in& addr2) const
        {
            return (addr1.sin_addr.s_addr < addr2.sin_addr.s_addr) ||
                   ((addr1.sin_addr.s_addr == addr2.sin_addr.s_addr) &&
                    (addr1.sin_port < addr2.sin_port)) ;
        }
    };

    mutable Mutex                     mutex;
    std::set<struct sockaddr_in, Compare> inetAddrs;

public:
    /**
     * Authorizes a client.
     * @param[in] clntAddr  Address of the client
     * @exceptionsafety     Strong guarantee
     * @threadsafety        Compatible but not safe
     */
    void authorize(const struct sockaddr_in& clntAddr)
    {
        LockGuard lock{mutex};
        inetAddrs.insert(clntAddr);
    }

    bool isAuthorized(const struct sockaddr_in& clntAddr) const
    {
        LockGuard lock{mutex};
        return inetAddrs.find(clntAddr) != inetAddrs.end();
    }

    void unauthorize(const struct sockaddr_in& clntAddr)
    {
        LockGuard lock{mutex};
        inetAddrs.erase(clntAddr);
    }
};

Authorizer::Authorizer()
    : pImpl{new Impl()}
{}

void Authorizer::authorize(const struct sockaddr_in& clntAddr) const
{
    pImpl->authorize(clntAddr);
}

bool Authorizer::isAuthorized(const struct sockaddr_in& clntAddr) const noexcept
{
    return pImpl->isAuthorized(clntAddr);
}

void Authorizer::unauthorize(const struct sockaddr_in& clntAddr) const noexcept
{
    pImpl->unauthorize(clntAddr);
}

void* auth_new()
{
    return new Authorizer();
}

void auth_unauthorize(
        void* const                     authorizer,
        const struct sockaddr_in* const addr)
{
    static_cast<Authorizer*>(authorizer)->unauthorize(*addr);
}

void auth_free(void* const authorizer)
{
    delete static_cast<Authorizer*>(authorizer);
}
