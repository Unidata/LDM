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

/**
 * Returns the name of the authorization message-queue that's associated with a
 * particular data-product feed.
 * @param[in] feed        Data-product feed
 * @return                Name of authorization message-queue associated with
 *                        `feed`
 * @throw std::bad_alloc  Necessary space can't be allocated
 */
std::string authMsgQName(const feedtypet feed);

class AuthClient
{
    class                        Impl;
    static std::shared_ptr<Impl> pImpl;

public:
    /**
     * Initializes.
     * @param[in] feed           Data-product feed
     * @throw std::logic_error   Already initialized
     * @throw std::system_error  Couldn't open message-queue
     */
    static void init(const feedtypet feed);

    /**
     * Authorizes a remote FMTP client to connect to the local FMTP server.
     * @param[in] addr  Address of remote FMTP client
     */
    static void authorize(const struct in_addr& addr);

    /**
     * Releases allocated resources.
     */
    static void fini();
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

Ldm7Status authClnt_init(const feedtypet feed);

Ldm7Status authClnt_authorize(const struct in_addr* addr);

void authClnt_fini();

#ifdef __cplusplus
}
#endif

#endif /* MCAST_LIB_C___AUTHCLIENT_H_ */
