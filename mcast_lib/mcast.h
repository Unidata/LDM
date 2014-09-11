/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file:   mcast.h
 * @author: Steven R. Emmerson
 *
 * This file declares the C API for the Virtual Circuit Multicast Transport
 * Protocol, VCMTP.
 */

#ifndef VCMTP_C_API_H
#define VCMTP_C_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
    extern "C" {
#endif

typedef u_int32_t                McastFileId;
#define xdr_McastFileId          xdr_u_long
typedef struct mcast_receiver    McastReceiver;

typedef int     (*BofFunc)(void* obj, void* file_entry);
typedef int     (*EofFunc)(void* obj, const void* file_entry);
typedef void    (*MissedFileFunc)(void* obj, const McastFileId fileId);

int mcastReceiver_new(
    McastReceiver**             receiver,
    const char* const           tcpAddr,
    const unsigned short        tcpPort,
    BofFunc                     bof_func,
    EofFunc                     eof_func,
    MissedFileFunc              missed_file_func,
    const char* const           mcastAddr,
    const unsigned short        mcastPort,
    void*                       obj);

void mcastReceiver_free(
    McastReceiver*              receiver);

int mcastReceiver_execute(
    const McastReceiver*        receiver);

void mcastReceiver_stop(
    McastReceiver* const        receiver);

/**
 * Returns a new multicast sender.
 *
 * @param[out] sender      Pointer to returned sender.
 * @param[in]  tcpAddr     IP address of the interface on which the TCP
 *                         server will listen for connections from
 *                         receivers for retrieving missed data-blocks.
 *                         May be hostname or IP address.
 * @param[in]  tcpPort     Port number of the TCP server.
 * @param[in]  mcastAddr   Address of the multicast group. May be
 *                         groupname or formatted IP address.
 * @param[in]  mcastPort   Port number of the multicast group.
 * @param[in]  ttl         Time-to-live of outgoing packets.
 *                               0  Restricted to same host. Won't be output by
 *                                  any interface.
 *                               1  Restricted to the same subnet. Won't be
 *                                  forwarded by a router (default).
 *                             <32  Restricted to the same site, organization or
 *                                  department.
 *                             <64  Restricted to the same region.
 *                            <128  Restricted to the same continent.
 *                            <255  Unrestricted in scope. Global.
 * @param[in]  fileId      Initial file-identifier. The first multicast data-
 *                         product will have this as its file-identifier.
 * @retval     0           Success. The client should call \c
 *                         mcastSender_free(*sender) when the sender is no
 *                         longer needed.
 * @retval     EINVAL      if @code{0==addr} or the multicast group
 *                         address couldn't be converted into a binary IP
 *                         address.
 * @retval     ENOMEM      Out of memory. \c log_add() called.
 * @retval     -1          Other failure. \c log_add() called.
 */
int
mcastSender_new(
    void** const         sender,
    const char* const    tcpAddr,
    const unsigned short tcpPort,
    const char* const    mcastAddr,
    const unsigned short mcastPort,
    const unsigned       ttl,
    const McastFileId    fileId);

/**
 * Frees a multicast sender's resources.
 *
 * @param[in] sender  The multicast sender whose resources are to be freed.
 */
void
mcastSender_free(
    void* const sender);

/**
 * Multicasts memory data.
 *
 * @param[in] sender  VCMTP sender.
 * @param[in] data    Data to send.
 * @param[in] nbytes  Amount of data in bytes.
 * @retval    0       Success.
 * @retval    EIO     Failure. `log_start()` called.
 */
int
mcastSender_send(
    void* const  sender,
    void* const  data,
    const size_t nbytes);

int mcastFileEntry_isWanted(
    const void*                 file_entry);

bool mcastFileEntry_isMemoryTransfer(
    const void*                 file_entry);

McastFileId mcastFileEntry_getFileId(
    const void*                 file_entry);

const char* mcastFileEntry_getFileName(
    const void*                 file_entry);

size_t mcastFileEntry_getSize(
    const void*                 file_entry);

void mcastFileEntry_setBofResponseToIgnore(
    void*                       file_entry);

int mcastFileEntry_setBofResponse(
    void*                       fileEntry,
    const void*                 bofResponse);

const void* mcastFileEntry_getBofResponse(
    const void*                 file_entry);

void* bofResponse_getPointer(
    const void*                 bofResponse);

#ifdef __cplusplus
}
#endif

#endif
