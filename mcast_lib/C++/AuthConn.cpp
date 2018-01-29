/**
 * This file implements a message queue for authorizing connections from the
 * FMTP layer of remote LDM7-s to the FMTP server of the local LDM7. A TCP-based
 * client/server architecture is use because authorization of a downstream LDM7
 * must be synchronous (and message queues aren't) because the downstream LDM7
 * must be authorized before it tries to connect to the local, upstream, FMTP
 * server.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *        File: AuthConn.cpp
 *  Created on: Dec 14, 2017
 *      Author: Steven R. Emmerson
 */

#include "AuthConn.h"
#include "log.h"

#include <cstddef>
#include <errno.h>
#include <fcntl.h>
#include <netinet/sctp.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

Ldm7Status AuthConn_authorize(
        const in_port_t port,
        const in_addr_t addr)
{
    int sd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sd < 0)
        throw std::system_error(errno, std::system_category(),
                "Couldn't create UNIX SCTP socket");
    try {
        static const char  LOOPBACK[] = "127.0.0.1";
        struct sockaddr_in service = {};
        service.sin_family = AF_INET;
        service.sin_port = htons(port);
        ::inet_pton(AF_INET, LOOPBACK, &service.sin_addr);
        int status = ::connect(sd,
                reinterpret_cast<struct sockaddr*>(&service), sizeof(service));
        if (status) {
            char dottedQuad[INET_ADDRSTRLEN];
            throw std::system_error(errno, std::system_category(),
                    "Couldn't connect socket to " + ::inet_ntop(AF_INET,
                            &service.sin_addr, dottedQuad, sizeof(dottedQuad)));
        }
        ::send(sd, )
        ::close(sd);
    } // `sd` open
    catch (const std::exception& ex) {
        ::close(sd);
    }
}

class AuthConn::Impl
{
protected:
    #define               LOOPBACK "127.0.0.1"
    const static uint16_t numStreams;
    int                   sd;

    virtual std::string getRemoteId() const =0;

public:
    /**
     * Constructs.
     * @param[in] feed           LDM feed
     * @throw std::system_error  Couldn't create socket
     */
    Impl()
        : sd{::socket(AF_LOCAL, SOCK_STREAM, IPPROTO_SCTP)}
    {
        if (sd < 0)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't create UNIX SCTP socket");
    }

    virtual ~Impl() noexcept
    {
        ::close(sd); // Can't fail
    }

    /**
     * Sends a message.
     * @param[in] msg            Message
     * @param[in] nbytes         Number of bytes in message
     * @throw std::system_error  Couldn't send message
     */
    void send(
            const void*    msg,
            size_t         nbytes)
    {
        static struct sctp_sndrcvinfo sinfo = {};
        auto status = ::sctp_send(sd, msg, nbytes, &sinfo, 0);
        if (status != nbytes) {
            char            dottedQuad[INET_ADDRSTRLEN];
            struct sockaddr sockaddr;
            socklen_t       sockaddrSize = sizeof(sockaddr);
            (void)::getpeername(sd, &sockaddr, &sockaddrSize);
            throw std::system_error(errno, std::system_category(),
                    "Couldn't send " + std::to_string(nbytes) + " bytes to " +
                    getRemoteId());
        }
    }

    /**
     * Receives a message.
     * @param[out] msg           Message
     * @param[in]  nbytes        Size of `msg`
     * @return                   Number of bytes read
     * @throw std::system_error  Couldn't receive message
     */
    uint16_t receive(
            void*  msg,
            size_t nbytes)
    {
        struct sctp_sndrcvinfo sinfo = {};
        int                    flags = 0;
        auto len = ::sctp_recvmsg(sd, msg, nbytes, nullptr, nullptr, &sinfo,
                &flags);
        if (len < 0)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't receive from " + getRemoteId());
        return len;
    }
};

uint16_t AuthConn::Impl::numStreams = 1;

AuthConn::AuthConn(Impl& impl)
    : pImpl{&impl}
{}

void AuthConn::send(
        const void* msg,
        size_t      nbytes) const
{
    pImpl->send(msg, nbytes);
}

size_t AuthConn::receive(
        void*  msg,
        size_t nbytes) const
{
    return pImpl->receive(msg, nbytes);
}

/******************************************************************************
 * Client-side authorization connection
 ******************************************************************************/

class ClntAuthConn::Impl final : public AuthConn::Impl
{
    std::string remoteId;

public:
    /**
     * Constructs.
     * @param[in] port  Port number of server on local host in host byte-order
     */
    Impl(const in_port_t port)
        : AuthConn::Impl{}
    {
        struct sockaddr_in       sockaddr = {};
        sockaddr.sin_family = AF_INET;
        ::inet_pton(AF_INET, LOOPBACK, &sockaddr.sin_addr);
        sockaddr.sin_port = htons(port);
        ::connect(sd, reinterpret_cast<struct sockaddr*>(&sockaddr),
                sizeof(sockaddr));
        remoteId = std::string{LOOPBACK} + ":" + std::to_string(port);
    }

    std::string getRemoteId() const noexcept
    {
        return remoteId;
    }

    /**
     * Synchronous.
     * @param[in] addr               Address of client to authorize in network
     *                               byte-order
     * @throw std::system_error      Couldn't send message
     * @throw std::system_error      Couldn't receive reply
     * @throw std::runtime_error     Invalid reply
     * @throw std::invalid_argument  Server didn't reply with `LDM7_OK`
     */
    void authorize(in_addr_t addr)
    {
        send(&addr, sizeof(addr));
        Ldm7Status status;
        const auto nbytes = receive(&status, sizeof(status));
        if (nbytes != sizeof(addr))
            throw std::runtime_error("Received " + std::to_string(nbytes) +
                    " bytes; expected " + std::to_string(sizeof(status)));
        if (status != LDM7_OK)
            throw std::runtime_error("Received reply " + std::to_string(status)
                    + "; expected 0 (LDM7_OK)");
    }
}; // class `ClntAuthConn::Impl`

/******************************************************************************
 * Server-side authorization connection
 ******************************************************************************/

class SrvrAuthConn::Impl final : public AuthConn::Impl
{
    Authorizer& auth;

public:
    Impl(Authorizer& auth)
        : auth{auth}
    {}

    std::string getRemoteId() const
    {
        struct sockaddr sockaddr;
        socklen_t       sockaddrSize = sizeof(sockaddr);
        ::getpeername(sd, &sockaddr, &sockaddrSize);
        char dottedQuad[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &sockaddr, dottedQuad, sizeof(dottedQuad));
        std::string remoteId{dottedQuad};
        remoteId += ":";
        remoteId += std::to_string(ntohs(reinterpret_cast<struct sockaddr_in*>
                (&sockaddr)->sin_port));
        return remoteId;
    }
};
