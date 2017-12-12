/**
 * This file declares a client that sends to a message-queue in order to
 * authorize a remote FMTP layer to connect to the local server FMTP layer.
 *
 *        File: AuthClient.h
 *  Created on: Dec 11, 2017
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___AUTHCLIENT_H_
#define MCAST_LIB_C___AUTHCLIENT_H_

#include "ldm.h"

#ifdef __cplusplus
#include <memory>

class AuthClient
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs.
     * @param[in] authMsgQName   Name of authorization message-queue
     * @throw std::system_error  Couldn't open message-queue
     */
    AuthClient(const std::string& authMsgQName);

    /**
     * Authorizes a remote FMTP client to connect to the local FMTP server.
     * @param[in] addr  Address of remote FMTP client
     */
    void authorize(const struct in_addr& addr) const;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

void* authClnt_new(const char* authMsgQName);

Ldm7Status authClnt_authorize(
        const void*           authClnt,
        const struct in_addr* addr);

void authClnt_free(void* authClient);

#ifdef __cplusplus
}
#endif

#endif /* MCAST_LIB_C___AUTHCLIENT_H_ */
