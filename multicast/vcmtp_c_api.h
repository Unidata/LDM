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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long             VcmtpFileId;
#define xdr_VcmtpFileId           xdr_u_long
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

int vcmtpFileEntry_isWanted(
    const void*                 file_entry);

bool vcmtpFileEntry_isMemoryTransfer(
    const void*                 file_entry);

VcmtpFileId vcmtpFileEntry_getFileId(
    const void*                 file_entry);

const char* vcmtpFileEntry_getFileName(
    const void*                 file_entry);

size_t vcmtpFileEntry_getSize(
    const void*                 file_entry);

void vcmtpFileEntry_setBofResponseToIgnore(
    void*                       file_entry);

int vcmtpFileEntry_setBofResponse(
    void*                       fileEntry,
    const void*                 bofResponse);

const void* vcmtpFileEntry_getBofResponse(
    const void*                 file_entry);

void* bofResponse_getPointer(
    const void*                 bofResponse);

#ifdef __cplusplus
}
#endif

#endif
