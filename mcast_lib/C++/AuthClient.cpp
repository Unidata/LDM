/**
 * This file implements a client that sends to a message-queue in order to
 * authorize a remote FMTP layer to connect to the local server FMTP layer.
 *
 *        File: AuthClient.cpp
 *  Created on: Dec 11, 2017
 *      Author: Steven R. Emmerson
 */
#include <AuthConn.h>
#include "config.h"

#include "AuthClient.h"
#include "log.h"

#include <mqueue.h>
#include <system_error>

/******************************************************************************
 * Private Implementation:
 ******************************************************************************/

class AuthClient::Impl final
{
    AuthConn    authMsgQ;

public:
    /**
     * Constructs.
     * @param[in] feed           Data-product feed
     * @throw std::system_error  Couldn't open message-queue
     */
    Impl(const feedtypet feed)
        : authMsgQ{feed, false}
    {}

    void authorize(const struct in_addr& addr)
    {
        authMsgQ.send(addr);
    }
};

/******************************************************************************
 * Public C++ API:
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
 * Public C API:
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
