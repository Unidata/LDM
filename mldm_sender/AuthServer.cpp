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
#include <system_error>
#include <thread>
#include <unistd.h>

class AuthServer::Impl final
{
    std::string name;       /// Name of message-queue
    Authorizer  authorizer; /// Authorization database
    mqd_t       mqId;       /// Message-queue handle
    std::thread thread;     /// Server thread

    void getAddr(struct sockaddr_in& addr)
    {
        auto nbytes = ::mq_receive(mqId, reinterpret_cast<char*>(&addr),
                sizeof(addr), nullptr); // Priority argument is irrelevant
        if (nbytes == -1)
            throw std::system_error(errno, std::system_category(),
                    "mq_receive() failure");
        if (nbytes != sizeof(addr)) {
            throw std::runtime_error(std::to_string(nbytes) + "-byte "
                    "authorization-message is too short; should have been " +
                    std::to_string(sizeof(addr)) + " bytes");
        }
    }

    void runServer()
    {
        for (;;) {
            struct sockaddr_in addr;
            try {
                getAddr(addr);
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
                unsigned port = ntohs(addr.sin_port);
                log_add("Couldn't add authorization for client %s:%d",
                        ::inet_ntop(AF_INET, &addr.sin_addr.s_addr, dottedQuad,
                        sizeof(dottedQuad)), port);
                break;
            }
        } // Loop
        // Log now because end-of-thread
        log_error("Authorization-server failure for message-queue %s",
                name.c_str());
    }

public:
    /**
     * Starts executing the server immediately on a separate thread: doesn't
     * block.
     * @param[in] authorizer     Authorization database
     * @param[in] name           Name of message-queue for receiving
     *                           authorizations
     * @throw std::system_error  Couldn't open message-queue
     * @throw std::system_error  Couldn't create server-thread
     */
    Impl(   Authorizer&        authorizer,
            const std::string& name)
        : name{name}
        , authorizer{authorizer}
        /*
         * Create the queue in case the program is started stand-alone. Assume
         * that only the user needs access and that the default attributes are
         * adequate.
         */
        , mqId{::mq_open(name.c_str(), O_RDONLY|O_CREAT, S_IRUSR|S_IWUSR,
                nullptr)}
    {
        if (mqId == static_cast<mqd_t>(-1))
            throw std::system_error(errno, std::system_category(),
                    "Couldn't open authorization message-queue " + name);
        try {
            thread = std::thread([this]{runServer();});
        }
        catch (const std::exception& ex) {
            ::mq_close(mqId); // Can't fail
            throw std::system_error(errno, std::system_category(),
                    "Couldn't create server-thread for authorization "
                    "message-queue "+ name + ": " + ex.what());
        }
    }

    ~Impl() noexcept
    {
        ::mq_close(mqId); // Can't fail
        /*
         * No need for the queue if this instance is destroyed. Done before
         * thread cancellation to ensure removal in case the join hangs.
         */
        if (::mq_unlink(name.c_str())) {
            log_add_syserr("mq_unlink() failure");
            log_error("Couldn't delete authorization message-queue %s");
        }

        auto status = ::pthread_cancel(thread.native_handle());
        if (status)
            log_errno(status, "Couldn't cancel server-thread for authorization "
                    "message-queue %s", name.c_str());
        thread.join(); // Can't fail. Might hang, though
    }
};

AuthServer::AuthServer(
        Authorizer&        authorizer,
        const std::string& name)
    : pImpl{new Impl(authorizer, name)}
{}

void* authSrvr_new(
        void*       authorizer,
        const char* name)
{
    AuthServer* authSrvr;
    try {
        authSrvr = new AuthServer(*static_cast<Authorizer*>(authorizer),
                std::string{name});
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
