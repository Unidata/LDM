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

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char        name[256];      // keep consonant with VcmtpSenderMessage */
    double      time;
    size_t      length;
}       file_metadata;

typedef struct vcmtp_c_receiver   VcmtpCReceiver;

typedef bool    (*BofFunc)(void* extra_arg, const file_metadata* metadata);
typedef void    (*EofFunc)(void* extra_arg, const file_metadata* metadata);
typedef void    (*MissedFileFunc)(void* extra_arg, const file_metadata* metadata);

/**
 * Returns a new VCMTP C Receiver.
 *
 * @param[in] bof_func         Function to call when the VCMTP layer has seen
 *                             a beginning-of-file.
 * @param[in] eof_func         Function to call when the VCMTP layer has
 *                             completely received a file.
 * @param[in] missed_file_func Function to call when a file is missed by the
 *                              VCMTP layer.
 * @param[in] addr             Address of the multicast group.
 *                              224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                          purposes
 *                              224.0.1.0 - 238.255.255.255 User-defined
 *                                                          multicast addresses
 *                              239.0.0.0 - 239.255.255.255 Reserved for
 *                                                          administrative
 *                                                          scoping
 * @param[in] port             Port number of the multicast group.
 * @param[in] extra_arg        Extra argument to pass to the above functions.
 *                             May be NULL.
 * @retval    NULL             Failure.
 * @retval    !NULL            A new VCMTP C Receiver. The client should call
 *                             vcmtp_receiver_free() when the receiver is no
 *                             longer needed.
 */
VcmtpCReceiver* vcmtp_receiver_new(
    BofFunc              bof_func,
    EofFunc              eof_func,
    MissedFileFunc       missed_file_func,
    const char* const    addr,
    const unsigned short port,
    void*                extra_arg);

/**
 * Frees the resources of a VCMTP C Receiver.
 *
 * @param cReceiver      The VCMTP C Receiver.
 */
void vcmtp_receiver_free(
    VcmtpCReceiver*     cReceiver);

/**
 * Joins a multicast group for receiving data.
 *
 * @param[in]   cReceiver       The VCMTP C Receiver.
 * @param[in]   addr            Address of the multicast group.
 * @param[in]   port            Port number of the multicast group.
 * @retval      1               Success.
 */
int vcmtp_receiver_join_group(
    VcmtpCReceiver*             cReceiver,
    const char* const           addr,
    const unsigned short        port);

#ifdef __cplusplus
}
#endif

#endif
