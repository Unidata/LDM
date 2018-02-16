/**
 * This file implements a mechanism for authorizing a connection by a downstream
 * FMTP layer of a remote LDM7 to the FMTP server of the local LDM7. A TCP-based
 * client/server architecture is used because authorization of a downstream LDM7
 * must be synchronous (and message queues aren't) because the downstream LDM7
 * must be authorized before it tries to connect to the local, upstream, FMTP
 * server, and because a write on a UNIX socket never blocks -- so a
 * retry/timeout mechanism would have to be implemented.
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
#include "TcpSock.h"

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

static std::string getSecretFilePathname(const in_port_t port)
{
    const char* dir = ::getenv("HOME");
    if (dir == nullptr)
        dir = "/tmp";
    return dir + std::string{"/MldmAuth_"} + std::to_string(port);
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
    const std::string pathname = getSecretFilePathname(port);
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
 * @param[in] port      Port number of multicast authorization server in host
 *                      byte-order
 * @param[in] addr      Address of the host to be authorized in network
 *                      byte-order
 * @retval LDM7_OK      Success
 * @retval LDM7_SYSTEM  Failure. `log_add()` called.
 */
Ldm7Status mldmAuth_authorize(
        const in_port_t port,
        in_addr_t       addr)
{
    Ldm7Status         ldm7Status = LDM7_SYSTEM;
    const InetSockAddr srvrSockAddr{InetAddr{"127.0.0.1"}, port};
    try {
        TcpSock conn{srvrSockAddr.getFamily()};
        conn.connect(srvrSockAddr);
        uint64_t secret = getSecret(port);
        struct iovec iov[2];
        iov[0].iov_base = &secret;
        iov[0].iov_len = sizeof(secret);
        iov[1].iov_base = &addr;
        iov[1].iov_len = sizeof(addr);
        conn.writev(iov, 2);
        conn.read(&ldm7Status, sizeof(ldm7Status));
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        log_add("Couldn't authorize remote LDM7 host %s with multicast "
                "authorization server %s", srvrSockAddr.to_string().c_str());
    }
    return ldm7Status;
}

/******************************************************************************
 * Multicast LDM authorization server
 ******************************************************************************/

class MldmAuthSrvr::Impl
{
    /// Server's listening socket
    SrvrTcpSock  srvrSock;
    uint64_t     secret;
    Authorizer   authorizer;

    /**
     * Creates the secret that's shared between the multicast LDM authorization
     * server and its client processes on the same system and belonging to the
     * same user.
     * @param port               Port number of authorization server in host
     *                           byte-order
     * @return                   Secret value
     * @throw std::system_error  Couldn't open secret-file
     * @throw std::system_error  Couldn't write secret to secret-file
     */
    static uint64_t initSecret(const in_port_t port)
    {
        const std::string pathname = getSecretFilePathname(port);
        auto fd = ::open(pathname.c_str(), O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
        if (fd < 0)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't open multicast authorization secret-file " +
                    pathname + " for writing");
        uint64_t secret;
        try {
            auto seed = std::chrono::high_resolution_clock::now()
                .time_since_epoch().count();
            secret = std::mt19937_64{seed}();
            if (::write(fd, &secret, sizeof(secret)) != sizeof(secret))
                throw std::system_error(errno, std::system_category(),
                        "Couldn't write secret to secret-file " + pathname);
            ::close(fd);
        } // `fd` is open
        catch (const std::exception& ex) {
            ::close(fd);
            throw;
        }
        return secret;
    }

public:
    Impl(Authorizer& authorizer)
        : srvrSock{InetSockAddr{InetAddr{"127.0.0.1"}}, 32}
        , secret{initSecret(srvrSock.getPort())}
        , authorizer{authorizer}
    {}

    /**
     * Destroys. Removes the secret-file.
     */
    ~Impl() noexcept
    {
        ::unlink(getSecretFilePathname(srvrSock.getPort()).c_str());
    }

    /**
     * Runs the server. Doesn't return until an exception is thrown.
     * @throw std::system_error   `accept()` failure
     * @throw std::runtime_error  Couldn't authorize FMTP client
     */
    void runServer()
    {
        for (;;) {
            auto      connSock = srvrSock.accept();
            uint64_t  clntSecret;
            in_addr_t fmtpAddr;
            struct iovec iov[2];
            iov[0].iov_base = &clntSecret;
            iov[0].iov_len = sizeof(clntSecret);
            iov[1].iov_base = &fmtpAddr;
            iov[1].iov_len = sizeof(fmtpAddr);
            try {
                connSock.readv(iov, 2);
            }
            catch (const std::exception& ex) {
                log_add(ex.what());
                log_notice("Couldn't read authorization request from "
                        "socket %s. Ignoring request.");
                continue;
            }
            if (clntSecret != secret) {
                log_notice("Invalid secret read from socket %s. "
                        "Ignoring authorization request.",
                        connSock.to_string().c_str());
            }
            else {
                try {
                    struct in_addr addr = {fmtpAddr};
                    authorizer.authorize(addr);
                    Ldm7Status ldm7Status = LDM7_OK;
                    try {
                        connSock.write(&ldm7Status, sizeof(ldm7Status));
                    }
                    catch (const std::exception& ex) {
                        log_notice(ex.what());
                        log_notice("Couldn't reply to authorization request "
                                " on socket %s", connSock.to_string().c_str());
                    }
                }
                catch (const std::exception& ex) {
                    std::throw_with_nested(std::runtime_error(
                            std::string{"Couldn't authorize FMTP client "} +
                            ::to_string(fmtpAddr)));
                }
            }
        }
    }

    /**
     * Returns the port number of the server.
     * @return Port number of server in host byte-order
     */
    in_port_t getPort() const noexcept
    {
        return srvrSock.getPort();
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
