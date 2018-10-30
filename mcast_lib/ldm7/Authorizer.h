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

#include "FmtpClntAddrs.h"

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
     *
     * @param[in] addrs       FMTP client IP addresses
     * @param[in] feed        Outgoing LDM feed
     */
    Authorizer(FmtpClntAddrs& addrs, const feedtypet feed);

    /**
     * Indicates if an FMTP client is authorized to connect.
     *
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

void* auth_new(
        void*           inAddrPool,
        const feedtypet feed);

void auth_delete(void* const authorizer);

#ifdef __cplusplus
}
#endif

#endif /* MCAST_LIB_LDM7_AUTHORIZER_H_ */
