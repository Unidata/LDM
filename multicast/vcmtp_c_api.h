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

typedef struct {
    char        name[256];      // keep consonant with VcmtpSenderMessage */
    double      time;
    size_t      length;
}       file_metadata;

#ifdef __cplusplus
extern "C" {
#endif

typedef void    vcmtp_receiver;

typedef bool    (*BofFunc)(void* extra_arg, const file_metadata* metadata);
typedef void    (*EofFunc)(void* extra_arg, const file_metadata* metadata);
typedef void    (*MissedFileFunc)(void* extra_arg, const file_metadata* metadata);

/**
 * Returns a new VCMTP receiver.
 *
 * @param[in] bof_func          Function to call when the VCMTP layer has seen
 *                              a beginning-of-file.
 * @param[in] eof_func          Function to call when the VCMTP layer has
 *                              completely received a file.
 * @param[in] missed_file_func  Function to call when a file is missed by the
 *                              VCMTP layer.
 * @param[in] extra_arg         Extra argument to pass to the above functions.
 *                              May be NULL.
 * @retval    NULL              Failure.
 * @retval    !NULL             A new VCMTP receiver. The client should call
 *                              vcmtp_receiver_free() when the receiver is no
 *                              longer needed.
 */
vcmtp_receiver* vcmtp_receiver_new(
    BofFunc             bof_func,
    EofFunc             eof_func,
    MissedFileFunc      missed_file_func,
    void*               extra_arg);

/**
 * Joins a multicast group for receiving data.
 *
 * @param[in]   addr    Address of the multicast group.
 * @param[in]   port    Port number of the multicast group.
 * @retval      1       Success.
 */
int vcmtp_receiver_join_group(
    const char* const           addr,
    const unsigned short        port);

/**
 * Releases the resources of a VCMTP receiver.
 *
 * @param receiver      The VCMTP receiver.
 */
void vcmtp_receiver_free(
    vcmtp_receiver*     receiver);

#ifdef __cplusplus
}
#endif

#endif
