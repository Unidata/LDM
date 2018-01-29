/**
 * This file implements a message-queue server that accepts authorizations for
 * client FMTP layers to connect to the server FMTP layer.
 *
 *        File: AuthServer.cpp
 *  Created on: Dec 11, 2017
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "AuthServer.h"
#include "log.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <mqueue.h>
#include <pthread.h>
#include <sys/stat.h>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include "../mcast_lib/C++/AuthConn.h"

class AuthServer::Impl final
{
    AuthConn    authMsgQ;   /// Authorization message-queue
    Authorizer  authorizer; /// Authorization database
    std::thread thread;     /// Server thread

    void runServer()
    {
        for (;;) {
            struct in_addr addr;
            try {
                authMsgQ.receive(addr);
            }
            catch (const std::exception& ex) {
                log_add(ex.what());
                log_add("Didn't receive authorization message");
                break;
            }

            try {
                authorizer.authorize(addr);
            }
            catch (const std::exception& ex) {
                log_add(ex.what());
                char dottedQuad[INET_ADDRSTRLEN];
                log_add("Couldn't add authorization for client %s",
                        ::inet_ntop(AF_INET, &addr.s_addr, dottedQuad,
                                sizeof(dottedQuad)));
                break;
            }
        } // Loop
        // Log now because end-of-thread
        log_error("Authorization-server failure for message-queue %s",
                authMsgQ.getName().c_str());
    }

public:
    /**
     * Starts executing the server immediately on a separate thread: doesn't
     * block.
     * @param[in] authorizer     Authorization database
     * @param[in] feed           Associated data-product feed
     * @throw std::system_error  Couldn't open message-queue
     * @throw std::system_error  Couldn't create server-thread
     */
    Impl(   Authorizer&     authorizer,
            const feedtypet feed)
        : authMsgQ{feed, true}
        , authorizer{authorizer}
    {
        try {
            thread = std::thread([this]{runServer();});
        }
        catch (const std::exception& ex) {
            throw std::runtime_error("Couldn't create server-thread for "
                    "reading from authorization message-queue " +
                    authMsgQ.getName() + ": " + ex.what());
        }
    }

    ~Impl() noexcept
    {
        auto status = ::pthread_cancel(thread.native_handle());
        if (status)
            log_errno(status, "Couldn't cancel server-thread for authorization "
                    "message-queue %s", authMsgQ.getName().c_str());
        thread.join(); // Can't fail. Might hang, though
    }
};

AuthServer::AuthServer(
        Authorizer&     authorizer,
        const feedtypet feed)
    : pImpl{new Impl(authorizer, feed)}
{}

void* authSrvr_new(
        void*           authorizer,
        const feedtypet feed)
{
    AuthServer* authSrvr;
    try {
        authSrvr = new AuthServer(*static_cast<Authorizer*>(authorizer), feed);
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        authSrvr = nullptr;
    }
    return authSrvr;
}

void authSrvr_free(void* authServer)
{
    delete static_cast<AuthServer*>(authServer);
}
