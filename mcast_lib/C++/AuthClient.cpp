/**
 * This file implements a client that sends to a message-queue in order to
 * authorize a remote FMTP layer to connect to the local server FMTP layer.
 *
 *        File: AuthClient.cpp
 *  Created on: Dec 11, 2017
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "AuthClient.h"
#include "log.h"

#include <mqueue.h>
#include <system_error>

std::string authMsgQName(const feedtypet feed)
{
    char buf[2+sizeof(feed)*2 + 1];
    (void)snprintf(buf, sizeof(buf), "%#X", feed);
    return std::string{"/AuthMsgQ_feed_"} + buf;
}

char* authMsgQ_name(
        char* const     buf,
        const size_t    size,
        const feedtypet feed)
{
    ::strncpy(buf, authMsgQName(feed).c_str(), size);
    buf[size-1] = 0;
}

/******************************************************************************
 * Private Implementation:
 ******************************************************************************/

class AuthClient::Impl final
{
    std::string name;       /// Name of message-queue
    mqd_t       mqId;       /// Message-queue handle

public:
    /**
     * Constructs.
     * @param[in] feed           Data-product feed
     * @throw std::system_error  Couldn't open message-queue
     */
    Impl(const feedtypet feed)
        : name{authMsgQName(feed)}
        /*
         * Assume that only the user needs access and that the default
         * attributes are adequate.
         */
        , mqId{::mq_open(name.c_str(), O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR,
                nullptr)}
    {
        if (mqId == static_cast<mqd_t>(-1))
            throw std::system_error(errno, std::system_category(),
                    "Couldn't open authorization message-queue " + name);
    }

    ~Impl() noexcept
    {
        ::mq_close(mqId); // Can't fail
    }

    void authorize(const struct in_addr& addr)
    {
        // Priority argument is irrelevant
        if (::mq_send(mqId, reinterpret_cast<const char*>(&addr), sizeof(addr),
                0)) {
            char dottedQuad[INET_ADDRSTRLEN];
            throw std::system_error(errno, std::system_category(),
                    std::string{"mq_send() failure: Couldn't send "
                        "authorization for client "} +
                    ::inet_ntop(AF_INET, &addr.s_addr, dottedQuad,
                    sizeof(dottedQuad)) + " to message-queue " + name);
        }
    }
};

/******************************************************************************
 * Public API:
 ******************************************************************************/

std::shared_ptr<AuthClient::Impl> AuthClient::pImpl;

void AuthClient::init(const feedtypet feed)
{
    if (pImpl)
        throw std::logic_error("Authorization client is already initialized");
    pImpl = std::shared_ptr<Impl>{new Impl(feed)};
}

void AuthClient::authorize(const struct in_addr& addr)
{
    pImpl->authorize(addr);
}

void AuthClient::fini()
{
    pImpl.reset();
}

/******************************************************************************
 * C Functions:
 ******************************************************************************/

Ldm7Status authClnt_init(const feedtypet feed)
{
    Ldm7Status status;
    try {
        AuthClient::init(feed);
        status = LDM7_OK;
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        status = LDM7_SYSTEM;
    }
    return status;
}

Ldm7Status authClnt_authorize(const struct in_addr* addr)
{
    Ldm7Status status;
    try {
        AuthClient::authorize(*addr);
        status = LDM7_OK;
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        status = LDM7_SYSTEM;
    }
    return status;
}

void authClnt_fini()
{
    AuthClient::fini();
}
