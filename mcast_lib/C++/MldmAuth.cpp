/**
 * This file implements a mechanism for authorizing a connection by a downstream
 * FMTP layer of a remote LDM7 to the FMTP server of the local LDM7. A TCP-based
 * client/server architecture is use because authorization of a downstream LDM7
 * must be synchronous (and message queues aren't) because the downstream LDM7
 * must be authorized before it tries to connect to the local, upstream, FMTP
 * server.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *        File: MldmAuth.cpp
 *  Created on: Dec 14, 2017
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "MldmAuth.h"

#include "Authorizer.h"
#include "ldm.h"
#include "log.h"

#include <chrono>
#include <cstdio>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <random>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

static std::string getSecretPathname(const in_port_t port)
{
    return std::string{"/tmp/MldmAuth_"} + std::to_string(port);
}

/**
 * Creates the secret that's shared between the multicast LDM authorization
 * server and its client processes on the same system and belonging to the same
 * user.
 * @param port               Port number of authorization server in host
 *                           byte-order
 * @throw std::system_error  Couldn't open secret-file
 * @throw std::system_error  Couldn't write secret to secret-file
 */
static void createSecret(
        const in_port_t port)
{
    const std::string pathname = getSecretPathname(port);
    auto fd = ::open(pathname.c_str(), O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
    if (fd < 0)
        throw std::system_error(errno, std::system_category(),
                "Couldn't open multicast authorization secret-file " +
                pathname + " for writing");
    try {
        auto seed = std::chrono::high_resolution_clock::now()
            .time_since_epoch().count();
        uint64_t secret = std::mt19937_64{seed}();
        if (::write(fd, &secret, sizeof(secret)) != sizeof(secret))
            throw std::system_error(errno, std::system_category(),
                    "Couldn't write secret to secret-file " + pathname);
        ::close(fd);
    } // `fd` is open
    catch (const std::exception& ex) {
        ::close(fd);
        throw;
    }
}

/**
 * Returns the secret that's shared between the multicast LDM authorization
 * server and its client processes on the same system and belonging to the same
 * user.
 * @param port               Port number of authorization server in host
 *                           byte-order
 * @throw std::system_error  Couldn't open secret-file
 * @throw std::system_error  Couldn't read secret from secret-file
 */
static uint64_t getSecret(const in_port_t port)
{
    uint64_t secret;
    const std::string pathname = getSecretPathname(port);
    auto fd = ::open(pathname.c_str(), O_RDONLY);
    if (fd < 0)
        throw std::system_error(errno, std::system_category(),
                "Couldn't open multicast authorization secret-file " +
                pathname + " for reading");
    try {
        if (::read(fd, &secret, sizeof(secret)) != sizeof(secret))
            throw std::system_error(errno, std::system_category(),
                    "Couldn't read secret from secret-file " + pathname);
        ::close(fd);
    } // `fd` is open
    catch (const std::exception& ex) {
        ::close(fd);
        throw;
    }
    return secret;
}

/**
 * C function to authorize a host to receive a multicast.
 * @param[in] port      Port number of the multicast authorization server
 * @param[in] addr      Address of the host to be authorized
 * @retval LDM7_OK      Success
 * @retval LDM&_SYSTEM  Failure. `log_add()` called.
 */
Ldm7Status mldmAuth_authorize(
        const in_port_t port,
        const in_addr_t addr)
{
    Ldm7Status ldm7Status = LDM7_SYSTEM;
    int        sd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sd < 0) {
        log_add("Couldn't create TCP socket");
    }
    else {
        struct sockaddr_in service = {};
        service.sin_family = AF_INET;
        service.sin_port = htons(port);
        inet_pton(AF_INET, "127.0.0.1", &service.sin_addr);
        ssize_t status = ::connect(sd,
                reinterpret_cast<struct sockaddr*>(&service), sizeof(service));
        if (status) {
            log_add("Couldn't connect socket to loopback interface");
        }
        else try {
            char dottedQuad[INET_ADDRSTRLEN];
            uint64_t secret = getSecret(port);
            status = ::send(sd, &secret, sizeof(secret), MSG_NOSIGNAL);
            if (status < sizeof(secret)) {
                ::inet_ntop(AF_INET, &addr, dottedQuad, sizeof(dottedQuad));
                log_add("Couldn't authenticate with multicast authorization "
                        "server for host %s", dottedQuad);
            }
            else {
                status = ::send(sd, &addr, sizeof(addr), MSG_NOSIGNAL);
                if (status < sizeof(addr)) {
                    ::inet_ntop(AF_INET, &addr, dottedQuad, sizeof(dottedQuad));
                    log_add("Couldn't send authorized host address %s to "
                            "loopback interface", dottedQuad);
                }
                else {
                    status = ::recv(sd, &ldm7Status, sizeof(ldm7Status),
                            MSG_WAITALL);
                    if (status < sizeof(ldm7Status)) {
                        ::inet_ntop(AF_INET, &addr, dottedQuad,
                                sizeof(dottedQuad));
                        log_add("Couldn't receive authorization status for %s "
                                "from loopback interface", dottedQuad);
                    }
                }
            }
        }
        catch (const std::exception& ex) {
            log_add(ex.what());
        }
        ::close(sd);
    } // `srvrSock` is open
    return ldm7Status;
}

/******************************************************************************
 * Multicast LDM authorization server
 ******************************************************************************/

class MldmAuthSrvr::Impl
{
    /// Socket descriptor of server
    int                srvrSock;
    struct sockaddr_in srvrAddr;
    Authorizer         authorizer;

public:
    Impl(Authorizer& authorizer)
        : srvrSock{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)}
        , srvrAddr{}
        , authorizer{authorizer}
    {
        if (srvrSock < 0)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't create socket for multicast LDM authorization "
                    "server");
        srvrAddr.sin_family = AF_INET;
        srvrAddr.sin_port = 0;
        srvrAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        auto status = ::bind(srvrSock,
                reinterpret_cast<struct sockaddr*>(&srvrAddr),
                sizeof(srvrAddr));
        if (status)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't bind multicast LDM authorization socket to "
                    "loopback interface");
        status = listen(srvrSock, 32);
        if (status)
            throw std::system_error(errno, std::system_category(),
                    "listen() failure on multicast LDM authorization socket");
    }

    /**
     * Destroys.
     */
    ~Impl() noexcept
    {
        ::close(srvrSock);
    }

    /**
     * Runs the server. Doesn't return until an exception is thrown.
     * @throw std::system_error   `accept()` failure
     * @throw std::runtime_error  Couldn't authorize FMTP client
     */
    void runServer()
    {
        for (;;) {
            //struct sockaddr_in fmtpAddr;
            auto sd = ::accept(srvrSock, nullptr, nullptr);
            if (sd < 0)
                throw std::system_error(errno, std::system_category(),
                        "accept() failure on multicast LDM authorization "
                        "socket");
            try {
                in_addr_t fmtpAddr;
                auto status = ::recv(sd, &fmtpAddr, sizeof(fmtpAddr),
                        MSG_WAITALL);
                char dottedQuad[INET_ADDRSTRLEN];
                if (status == 0 || status != sizeof(fmtpAddr)) {
                    ::inet_ntop(AF_INET, &fmtpAddr, dottedQuad,
                         sizeof(dottedQuad));
                    log_notice("Couldn't receive FMTP client address from %s",
                            dottedQuad);
                }
                else {
                    try {
                        struct in_addr addr = {fmtpAddr};
                        authorizer.authorize(addr);
                    }
                    catch (const std::exception& ex) {
                        ::inet_ntop(AF_INET, &fmtpAddr, dottedQuad,
                                sizeof(dottedQuad));
                        std::throw_with_nested(std::runtime_error(
                                std::string{"Couldn't authorize FMTP client "} +
                                dottedQuad));
                    }
                }
                ::close(sd);
            }
            catch (const std::exception& ex) {
                ::close(sd);
                throw;
            }
        }
    }

    /**
     * Returns the port number of the server.
     * @return Port number of server in host byte-order
     */
    in_port_t getPort() const noexcept
    {
        return ::ntohs(srvrAddr.sin_port);
    }
};

MldmAuthSrvr::MldmAuthSrvr(Authorizer& authorizer)
    : pImpl{new Impl(authorizer)}
{}

in_port_t MldmAuthSrvr::getPort() const noexcept
{
    return pImpl->getPort();
}

void MldmAuthSrvr::runServer() const
{
    pImpl->runServer();
}

/******************************************************************************
 * C interface
 ******************************************************************************/

void* mldmAuthSrvr_new(void* authorizer)
{
    try {
        return new MldmAuthSrvr(*reinterpret_cast<Authorizer*>(&authorizer));
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
    }
    return nullptr;
}

in_port_t mldmAuthSrvr_getPort(void* srvr)
{
    return reinterpret_cast<MldmAuthSrvr*>(srvr)->getPort();
}

Ldm7Status mldmAuthSrvr_run(void* srvr)
{
    try {
        reinterpret_cast<MldmAuthSrvr*>(srvr)->runServer();
        return LDM7_OK;
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
    }
    return LDM7_SYSTEM;
}

void mldmAuthSrvr_delete(void* srvr)
{
    delete reinterpret_cast<MldmAuthSrvr*>(srvr);
}
