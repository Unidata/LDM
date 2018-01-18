/**
 * This file declares a message queue for authorizing connections from the FMTP
 * layer of remote LDM7-s to the FMTP server of the local LDM7.
 *
 *        File: AuthMsgQ.h
 *  Created on: Dec 14, 2017
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___AUTHMSGQ_H_
#define MCAST_LIB_C___AUTHMSGQ_H_

#include "ldm.h"

#include <memory>
#include <netinet/in.h>

class AuthMsgQ final
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

public:
    AuthMsgQ(
            const feedtypet feed,
            const int       readOnly);

    const std::string& getName() const;

    void send(const struct in_addr& addr) const;

    void receive(struct in_addr& addr) const;
};

#endif /* MCAST_LIB_C___AUTHMSGQ_H_ */
