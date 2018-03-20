/**
 * This file declares a thread-safe class that authorizes connections from
 * client FMTP layers to the server FMTP layer for data-block recovery.
 *
 *        File: Authorizer.h
 *  Created on: Dec 11, 2017
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_LDM7_AUTHORIZER_H_
#define MCAST_LIB_LDM7_AUTHORIZER_H_

#include "MldmRpc.h"

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
     * @param[in] inAddrPool  Pool of IP addresses
     */
    explicit Authorizer(InAddrPool& inAddrPool);

    /**
     * Indicates if a client is authorized to connect to the server FMTP layer.
     * @param[in] clntAddr  Address of client
     * @retval `true`       Client is authorized
     * @retval `false`      Client is not authorized
     * @exceptionsafety     NoThrow
     * @threadsafety        Safe
     */
    bool isAuthorized(const struct in_addr& clntAddr) const noexcept;
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

void* auth_new(void* inAddrPool);

void auth_delete(void* const authorizer);

#ifdef __cplusplus
}
#endif

#endif /* MCAST_LIB_LDM7_AUTHORIZER_H_ */
