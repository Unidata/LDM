/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file vcmtp_c_api.c
 *
 * This file defines the C API to the Virtual Circuit Multicast Transport
 * Protocol, VCMTP.
 *
 * @author: Steven R. Emmerson
 */

#include <vcmtp_c_api.h>
#include <VCMTPReceiver.h>

/**
 * Returns a new VCMTP receiver.
 *
 * @retval A new VCMTP receiver.
 */
vcmtp_receiver* vcmtp_receiver_new(void)
{
    return new VCMTPReceiver(0);
}

/**
 * Joins a multicast group for receiving data.
 *
 * @param addr  Address of the multicast group.
 * @param port  Port number of the multicast group.
 * @retval 1    Success.
 */
int vcmtp_receiver_join_group(
    vcmtp_receiver* const       self,
    const char* const           addr,
    const unsigned short        port)
{
    return static_cast<VCMTPReceiver*>(self)->JoinGroup(std::string(addr), port);
}
