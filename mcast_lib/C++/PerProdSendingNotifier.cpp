/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file PerProdSendingNotifier.cpp
 *
 * This file defines a class that notifies the sending application about
 * events on a per-product basis.
 *
 * @author: Steven R. Emmerson
 */

#include "PerProdSendingNotifier.h"
#include "mcast.h"
#include "log.h"

#include <stdlib.h>
#include <stdexcept>
#include <strings.h>
#include <sys/socket.h>

PerProdSendingNotifier::PerProdSendingNotifier(
        void      (*eop_func)(FmtpProdIndex iProd),
        Authorizer& authDb)
    : eop_func(eop_func)
    , authDb{authDb}
{
    if (!eop_func)
        throw std::invalid_argument("Null argument: eop_func");
}

/**
 * Notifies the sending application when the FMTP layer is done with a product.
 *
 * @param[in,out] prodIndex             Index of the product.
 */
void PerProdSendingNotifier::notify_of_eop(
        const FmtpProdIndex prodIndex)
{
    eop_func(prodIndex);
}

/**
 * Requests the application to verify an incoming connection request,
 * and to decide whether to accept or to reject the connection. This
 * method is thread-safe.
 * @return    true: receiver accepted; false: receiver rejected.
 */
bool PerProdSendingNotifier::verify_new_recv(int newsock)
{
    struct sockaddr sockaddr;
    socklen_t       len = sizeof(sockaddr);
    if (::getsockname(newsock, &sockaddr, &len)) {
        log_warning("Couldn't get address of new FMTP socket");
        return false;
    }
    if (sockaddr.sa_family != AF_INET) {
        log_warning(std::string{"Address family of new FMTP socket is " +
                std::to_string(sockaddr.sa_family) + " and not " +
                std::to_string(AF_INET) + " (AF_INET)"}.c_str());
        return false;
    }
    const struct sockaddr_in* addr =
            reinterpret_cast<struct sockaddr_in*>(&sockaddr);
    return authDb.isAuthorized(addr->sin_addr);
}
