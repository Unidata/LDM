/**
 * This file declares a thread-safe class that authorizes connections from
 * client FMTP layers to the server FMTP layer for data-block recovery.
 *
 *        File: Authorizer.h
 *  Created on: Dec 11, 2017
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___AUTHORIZER_H_
#define MCAST_LIB_C___AUTHORIZER_H_

#include <arpa/inet.h>

#ifdef __cplusplus
#include <memory>

class Authorizer
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    /**
     * Constructs.
     */
    Authorizer();

    /**
     * Authorizes a client to connect to the server FMTP layer.
     * @param[in] clntInetAddr  Address of the client
     * @exceptionsafety         Strong guarantee
     * @threadsafety            Safe
     */
    void authorize(const struct in_addr& clntInetAddr) const;

    /**
     * Indicates if a client is authorized to connect to the server FMTP layer.
     * @param[in] clntInetAddr  Address of the client
     * @retval `true`           Client is authorized
     * @retval `false`          Client is not authorized
     * @exceptionsafety         NoThrow
     * @threadsafety            Safe
     */
    bool isAuthorized(const struct in_addr& clntInetAddr) const noexcept;

    /**
     * Unauthorizes a client to connect to the server FMTP layer.
     * @param[in] clntInetAddr  Address of the client
     * @exceptionsafety         NoThrow
     * @threadsafety            Safe
     */
    void unauthorize(const struct in_addr& clntInetAddr) const noexcept;
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

void* auth_new();

void auth_unauthorize(
        void* const                 authorizer,
        const struct in_addr* const addr);

void auth_free(void* const authorizer);

#ifdef __cplusplus
}
#endif

#endif /* MCAST_LIB_C___AUTHORIZER_H_ */
