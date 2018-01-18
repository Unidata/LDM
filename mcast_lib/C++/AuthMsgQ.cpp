/**
 * This file implements a message queue for authorizing connections from the
 * FMTP layer of remote LDM7-s to the FMTP server of the local LDM7.
 *
 *        File: AuthMsgQ.cpp
 *  Created on: Dec 14, 2017
 *      Author: Steven R. Emmerson
 */

#include "AuthMsgQ.h"
#include "log.h"

#include "fcntl.h"
#include "mqueue.h"
#include <string>
#include <system_error>

class AuthMsgQ::Impl final
{
    const feedtypet   feed;
    const std::string name;
    const bool        readOnly;
    mqd_t             mqd;

    static std::string getQName(const feedtypet feed)
    {
        char buf[2+sizeof(feed)*2 + 1];
        (void)snprintf(buf, sizeof(buf), "%#X", feed);
        return std::string{"/AuthMsgQ_feed_"} + buf;
    }

    static int getMode(const bool readOnly)
    {
        return (readOnly ? O_RDONLY : O_WRONLY) | O_CREAT;
    }

public:
    Impl(
            const feedtypet feed,
            const bool      readOnly)
        : feed{feed}
        , name{getQName(feed)}
        , readOnly{readOnly}
        , mqd{-1}
    {
        struct mq_attr attr = {};
        attr.mq_msgsize = sizeof(struct in_addr);
        attr.mq_maxmsg = 1;
        mqd = ::mq_open(name.c_str(), getMode(readOnly), S_IRUSR|S_IWUSR,
                &attr);
        if (mqd == static_cast<mqd_t>(-1))
            throw std::system_error(errno, std::system_category(),
                    "Couldn't open authorization message-queue " + name);
    }

    ~Impl() noexcept
    {
        ::mq_close(mqd); // Can't fail
        /*
         * No need for the queue if the reading end is destroyed.
         */
        if (readOnly && ::mq_unlink(name.c_str())) {
            log_add_syserr("mq_unlink() failure");
            log_error("Couldn't delete authorization message-queue %s" ,
                    name.c_str());
        }
    }

    const std::string& getName()
    {
        return name;
    }

    void send(const struct in_addr& addr)
    {
        // Priority argument is irrelevant
        if (::mq_send(mqd, reinterpret_cast<const char*>(&addr), sizeof(addr),
                0)) {
            char     dottedQuad[INET_ADDRSTRLEN];
            throw std::system_error(errno, std::system_category(),
                    std::string{"mq_send() failure: Couldn't send "
                        "authorization for client "} +
                    ::inet_ntop(AF_INET, &addr, dottedQuad, sizeof(dottedQuad))
                    + " to message-queue " + name);
        }
    }

    void receive(struct in_addr& addr)
    {
        auto nbytes = ::mq_receive(mqd, reinterpret_cast<char*>(&addr),
                sizeof(addr), nullptr); // Priority argument is irrelevant
        if (nbytes == -1)
            throw std::system_error(errno, std::system_category(),
                    "mq_receive() failure");
        if (nbytes != sizeof(addr)) {
            throw std::runtime_error(std::to_string(nbytes) + "-byte "
                    "authorization-message is wrong length; should have been " +
                    std::to_string(sizeof(addr)) + " bytes");
        }
    }
};

AuthMsgQ::AuthMsgQ(
        const feedtypet feed,
        const int       readOnly)
    : pImpl{new Impl(feed, readOnly)}
{}

void AuthMsgQ::send(const struct in_addr& addr) const
{
    pImpl->send(addr);
}

const std::string& AuthMsgQ::getName() const
{
    return pImpl->getName();
}

void AuthMsgQ::receive(struct in_addr& addr) const
{
    pImpl->receive(addr);
}
