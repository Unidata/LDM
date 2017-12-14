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
    typedef std::mutex             Mutex;
    typedef std::lock_guard<Mutex> LockGuard;
    typedef std::chrono::seconds   Duration;

    struct Compare
    {
        bool operator()(
                const struct sockaddr_in& addr1,
                const struct sockaddr_in& addr2) const
        {
            return
#if 1
                    addr1.sin_addr.s_addr < addr2.sin_addr.s_addr;
#else
                    (addr1.sin_addr.s_addr < addr2.sin_addr.s_addr) ||
                    ((addr1.sin_addr.s_addr == addr2.sin_addr.s_addr) &&
                     (addr1.sin_port < addr2.sin_port));
#endif
        }
    };

    mutable Mutex                                 mutex;
    std::set<struct sockaddr_in, Compare>         inetAddrs;
    FixedDelayQueue<struct sockaddr_in, Duration> delayQ;
    std::thread                                   thread;

    /**
     * De-authorizes remote LDM7-s after a delay. Intended to run on its own
     * thread.
     */
    void deauthorize()
    {
        for (;;) {
            auto addr = delayQ.pop();
            unauthorize(addr);
        }
    }

public:
    /**
     * Constructs.
     */
    Impl()
        : mutex{}
        , inetAddrs{}
        , delayQ{Duration{30}}
        , thread{[this]{deauthorize();}}
    {}

    ~Impl() noexcept
    {
        auto status = ::pthread_cancel(thread.native_handle());
        if (status)
            log_errno(status, "Couldn't cancel de-authorization thread");
        thread.join(); // Can't fail. Might hang, though.
    }

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
        delayQ.push(clntAddr);
    }

    bool isAuthorized(const struct sockaddr_in& clntAddr) const
    {
        LockGuard lock{mutex};
        return inetAddrs.find(clntAddr) != inetAddrs.end();
    }

    /**
     * Unauthorizes a remote LDM7 client.
     * @param[in] clntAddr  Address of client
     * @exceptionsafety     NoThrow
     * @threadsafety        Safe
     */
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
