/**
 * This file declares a mechanism for authorizing connections from the FMTP
 * layer of a remote LDM7 to the FMTP server of the local LDM7. A TCP-based
 * client/server architecture is use because authorization of a downstream LDM7
 * must be synchronous (and UNIX message queues aren't) because the downstream
 * LDM7 must be authorized before it tries to connect to the local, upstream,
 * FMTP server.
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *        File: MldmAuth.h
 *  Created on: Dec 14, 2017
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___MLDMAUTH_H_
#define MCAST_LIB_C___MLDMAUTH_H_

#include "Authorizer.h"
#include "ldm.h"

#include <netinet/in.h>

/******************************************************************************
 * C interface:
 ******************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Authorizes a downstream LDM7 to receive a feed.
 * @param[in] port  Port number of feed-specific server in host byte-order
 * @param[in] addr  Address of host to be authorized in network byte-order
 * @retval 0        Success
 */
Ldm7Status mldmAuth_authorize(
        const in_port_t port,
        in_addr_t       addr);

/**
 * Returns a new multicast LDM authorization server.
 * @param[in] authorizer  Authorization database
 * @retval NULL           Failure. `log_add()` called.
 * @return                Pointer to new multicast LDM authorization server.
 *                        Caller should call `mldmAuthSrvr_delete()` when it's
 *                        no longer needed.
 * @see mldmAuthSrvr_delete()
 */
void* mldmAuthSrvr_new(void* authorizer);

/**
 * Returns the port number of the server.
 * @param[in] srvr  Multicast LDM authorization server
 * @return          Port number of server in host byte-order
 */
in_port_t mldmAuthSrvr_getPort(void* srvr);

/**
 * Runs the server. Doesn't return unless an error occurs.
 * @param[in] srvr      Multicast LDM authorization server
 * @return LDM7_SYSTEM  System error
 */
Ldm7Status mldmAuthSrvr_run(void* srvr);

/**
 * Deletes a multicast LDM authorization server.
 * @param[in] srvr      Multicast LDM authorization server
 * @see mldmAuthsrvr_new()
 */
void mldmAuthSrvr_delete(void* srvr);

#ifdef __cplusplus
} // `extern "C"`

/******************************************************************************
 * C++ interface:
 ******************************************************************************/

#include <memory>

class MldmAuthSrvr final
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs. The server's socket is bound to a local address and
     * configured for listening.
     * @param[in] authorizer  Authorization database
     */
    MldmAuthSrvr(Authorizer& authorizer);

    /**
     * Returns the port number of the server.
     * @return Port number of server in host byte-order
     */
    in_port_t getPort() const noexcept;

    void runServer() const;
};

#endif // `#ifdef __cplusplus`

#endif /* MCAST_LIB_C___MLDMAUTH_H_ */
