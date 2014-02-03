/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file vcmtp.h
 *
 * This file declares the C API for the Virtual Circuit Multicast Transport
 * Protocol, VCMTP.
 *
 * @author: Steven R. Emmerson
 */

#ifndef VCMTP_C_API_H
#define VCMTP_C_API_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void    vcmtp_receiver;

vcmtp_receiver* vcmtp_receiver_new(void);

int vcmtp_receiver_join_group(
    const char* const           addr,
    const unsigned short        port);

#ifdef __cplusplus
}
#endif

#endif
