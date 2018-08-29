/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file SendProxy.h
 *
 * This file declares the API for classes that notify a sending application
 * about files.
 *
 * @author: Shawn Chen <sc7cq@virginia.edu>
 */

#ifndef FMTP_SENDER_SENDPROXY_H_
#define FMTP_SENDER_SENDPROXY_H_


#include <stdint.h>


/**
 * This base class notifies a sending application about events.
 */
class SendProxy
{
public:
    SendProxy() {};
    virtual ~SendProxy() {};        // definition must exist

    /**
     * Notifies the sending application about the complete reception of the
     * previous product. This method is thread-safe.
     */
    virtual void notify_of_eop(uint32_t prodindex) = 0;
    /**
     * Requests the application to verify an incoming connection request,
     * and to decide whether to accept or to reject the connection. This
     * method is thread-safe.
     * @return    true: receiver accepted; false: receiver rejected.
     */
    virtual bool verify_new_recv(int newsock) = 0;
};


#endif /* FMTP_SENDER_SENDPROXY_H_ */
