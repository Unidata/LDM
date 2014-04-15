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

typedef struct vcmtp_c_receiver   VcmtpCReceiver;

typedef int     (*BofFunc)(void* obj, void* file_entry);
typedef int     (*EofFunc)(void* obj, const void* file_entry);
typedef void    (*MissedFileFunc)(void* obj, const void* file_entry);

int vcmtpReceiver_new(
    VcmtpCReceiver**            cReceiver,
    BofFunc                     bof_func,
    EofFunc                     eof_func,
    MissedFileFunc              missed_file_func,
    const char* const           addr,
    const unsigned short        port,
    void*                       obj);

void vcmtpReceiver_free(
    VcmtpCReceiver*             cReceiver);

int vcmtpReceiver_execute(
    const VcmtpCReceiver*       cReceiver);

bool vcmtpFileEntry_isMemoryTransfer(
    const void*                 file_entry);

const char* vcmtpFileEntry_getName(
    const void*                 file_entry);

size_t vcmtpFileEntry_getSize(
    const void*                 file_entry);

void vcmtpFileEntry_setBofResponseToIgnore(
    void*                       file_entry);

int vcmtpFileEntry_setMemoryBofResponse(
    void*                       file_entry,
    unsigned char*              buf,
    size_t                      size);

#ifdef __cplusplus
}
#endif

#endif
