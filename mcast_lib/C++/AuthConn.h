/**
 * This file declares a message queue for authorizing connections from the FMTP
 * layer of remote LDM7-s to the FMTP server of the local LDM7. A TCP-based
 * client/server architecture is use because authorization of a downstream LDM7
 * must be synchronous (and message queues aren't) because the downstream LDM7
 * must be authorized before it tries to connect to the local, upstream, FMTP
 * server.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *        File: AuthConn.h
 *  Created on: Dec 14, 2017
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___AUTHCONN_H_
#define MCAST_LIB_C___AUTHCONN_H_

#include "Authorizer.h"
#include "ldm.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <netinet/in.h>

/**
 * Authorizes a downstream LDM7 to receive a feed.
 * @param[in] port  Port number of feed-specific server in host byte-order
 * @param[in] addr  Address of host to be authorized in network byte-order
 * @retval 0        Success
 */
extern "C"
Ldm7Status AuthConn_authorize(
        const in_port_t port,
        const in_addr_t addr);

class AuthConn
{
protected:
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

    AuthConn(Impl& impl);

public:
    AuthConn();

    virtual ~AuthConn() noexcept =0;

    void send(
            const void* msg,
            size_t      nbytes) const;

    size_t receive(
            void*  msg,
            size_t nbytes) const;
};

class ClntAuthConn final : public AuthConn
{
    class Impl;

public:
    ClntAuthConn();

    ~ClntAuthConn() noexcept;

    /**
     * Synchronous.
     * @param[in] addr  Address of client to authorize
     */
    void authorize(in_addr_t addr) const;
};

class SrvrAuthConn final : public AuthConn
{
    class Impl;

public:
    SrvrAuthConn(Authorizer& auth);

    ~SrvrAuthConn() noexcept;

    /**
     * Returns the port number of the server.
     * @return Port number of server in host byte-order
     */
    in_port_t getPort() const noexcept;

    void runServer() const;
};

#endif /* MCAST_LIB_C___AUTHCONN_H_ */
