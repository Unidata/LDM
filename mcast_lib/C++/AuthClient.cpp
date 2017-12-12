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

#include <arpa/inet.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

class AuthClient::Impl final
{
    std::string name;       /// Name of message-queue
    mqd_t       mqId;       /// Message-queue handle

public:
    /**
     * Constructs.
     * @param[in] authMsgQName   Name of authorization message-queue
     * @throw std::system_error  Couldn't open message-queue
     */
    Impl(const std::string& name)
        : name{name}
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
        if (::mq_send(mqId, reinterpret_cast<const char*>(&addr), sizeof(addr), 0)) {
            char dottedQuad[INET_ADDRSTRLEN];
            throw std::system_error(errno, std::system_category(),
                    std::string{"mq_send() failure: Couldn't send "
                        "authorization for client "} +
                    ::inet_ntop(AF_INET, &addr.s_addr, dottedQuad,
                    sizeof(dottedQuad)) + " to message-queue " + name);
        }
    }
};

AuthClient::AuthClient(const std::string& name)
    : pImpl{new Impl(name)}
{}

void AuthClient::authorize(const struct in_addr& addr) const
{
    pImpl->authorize(addr);
}

void* authClnt_new(const char* name)
{
    AuthClient* authClnt;
    try {
        authClnt = new AuthClient(std::string{name});
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        authClnt = nullptr;
    }
    return authClnt;
}

Ldm7Status authClnt_authorize(
        const void*           authClnt,
        const struct in_addr* addr)
{
    Ldm7Status status;
    try {
        static_cast<const AuthClient*>(authClnt)->authorize(*addr);
        status = LDM7_OK;
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        status = LDM7_SYSTEM;
    }
    return status;
}

void authClnt_free(void* authClnt)
{
    delete static_cast<AuthClient*>(authClnt);
}
