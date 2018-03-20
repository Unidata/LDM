/**
 * This file declares a message-queue server that accepts authorizations for
 * client FMTP layers to connect to the server FMTP layer.
 *
 *        File: AuthServer.h
 *  Created on: Dec 11, 2017
 *      Author: Steven R. Emmerson
 */

#ifndef MLDM_SENDER_AUTHSERVER_H_
#define MLDM_SENDER_AUTHSERVER_H_

#include "../mcast_lib/ldm7/Authorizer.h"
#include "ldm.h"

#ifdef __cplusplus
#include <memory>

class AuthServer
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs. Creates the authorization message-queue if it doesn't already
     * exist. Starts executing immediately on a separate thread.
     * @param[in] authorizer     Authorization database
     * @param[in] name           Name of communications channel for receiving
     *                           authorizations
     * @throw std::system_error  Couldn't open message-queue
     * @throw std::system_error  Couldn't create server-thread
     */
    AuthServer(
            Authorizer&     authorizer,
            const feedtypet feed);
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

void* authSrvr_new(
        void*           authorizer,
        const feedtypet feed);

void authSrvr_free(void* authServer);

#ifdef __cplusplus
}
#endif

#endif /* MLDM_SENDER_AUTHSERVER_H_ */
