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

#include "log.h"
#include "vcmtp_c_api.h"
#include "PerFileNotifier.h"

#include <BofResponse.h>
#include <VCMTPReceiver.h>
#include <VcmtpFileEntry.h>

#include <exception>
#include <stdexcept>
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <xdr.h>

/**
 * The VCMTP C Receiver data-object.
 */
struct vcmtp_c_receiver {
    /**
     * The VCMTP-layer receiver.
     */
    VCMTPReceiver*      receiver;
};

/**
 * Initializes a VCMTP C Receiver.
 *
 * @param[out] receiver               The VCMTP C Receiver to initialize.
 * @param[in]  tcpAddr                Address of the TCP server from which to
 *                                    retrieve missed data-blocks. May be
 *                                    hostname or IP address.
 * @param[in]  tcpPort                Port number of the TCP server to which to
 *                                    connect.
 * @param[in]  bof_func               Function to call when the VCMTP layer
 *                                    has seen a beginning-of-file.
 * @param[in]  eof_func               Function to call when the VCMTP layer
 *                                    has completely received a file.
 * @param[in]  missed_file_func       Function to call when a file is missed
 *                                    by the VCMTP layer.
 * @param[in]  mcastAddr              Address of the multicast group to receive.
 *                                    May be groupname or formatted IP address.
 * @param[in]  mcastPort              Port number of the multicast group.
 * @param[in]  obj                    Relevant object in the receiving
 *                                    application to pass to the above
 *                                    functions. May be NULL.
 * @throws     std::invalid_argument  if @code{0==buf_func || 0==eof_func ||
 *                                    0==missed_file_func || 0==addr}.
 * @throws     std::invalid_argument  if the multicast group address couldn't be
 *                                    converted into a binary IPv4 address.
 * @throws     std::runtime_error     if the IP address of the PA interface
 *                                    couldn't be obtained. (The PA address
 *                                    seems to be specific to Linux and might
 *                                    cause problems.)
 * @throws     std::runtime_error     if the socket couldn't be bound to the
 *                                    interface.
 * @throws     std::runtime_error     if the socket couldn't be bound to the
 *                                    Internet address.
 * @throws     std::runtime_error     if the multicast group couldn't be
 *                                    added to the socket.
 * @throws     std::exception         If the VCMTP C Receiver can't be
 *                                    initialized.
 */
static void vcmtpReceiver_init(
    VcmtpCReceiver* const       receiver,
    const char* const           tcpAddr,
    const unsigned short        tcpPort,
    const BofFunc               bof_func,
    const EofFunc               eof_func,
    const MissedFileFunc        missed_file_func,
    const char* const           mcastAddr,
    const unsigned short        mcastPort,
    void* const                 obj)
{
    std:string          hostId(tcpAddr);
    VCMTPReceiver*      rcvr = new VCMTPReceiver(hostId, tcpPort,
            PerFileNotifier(bof_func, eof_func, missed_file_func, obj));

    try {
        rcvr->JoinGroup(std::string(mcastAddr), mcastPort);
        receiver->receiver = rcvr;
    }
    catch (const std::exception& e) {
        delete rcvr;
        throw;
    } /* "rcvr" allocated */
}

/**
 * Returns a new VCMTP C Receiver.
 *
 * @param[out] receiver          Pointer to returned VCMTP receiver.
 * @param[in]  tcpAddr           Address of the TCP server from which to
 *                               retrieve missed data-blocks. May be hostname or
 *                               IP address.
 * @param[in]  tcpPort           Port number of the TCP server to which to
 *                               connect.
 * @param[in]  bof_func          Function to call when the VCMTP layer has seen
 *                               a beginning-of-file.
 * @param[in]  eof_func          Function to call when the VCMTP layer has
 *                               completely received a file.
 * @param[in]  missed_file_func  Function to call when a file is missed by the
 *                               VCMTP layer.
 * @param[in]  mcastAddr         Address of the multicast group to receive. May
 *                               be groupname or formatted IP address.
 * @param[in]  mcastPort         Port number of the multicast group.
 * @param[in]  obj               Relevant object in the receiving application to
 *                               pass to the above functions. May be NULL.
 * @retval     0                 Success. The client should call \c
 *                               vcmtp_receiver_free(*receiver) when the
 *                               receiver is no longer needed.
 * @retval     EINVAL            if @code{0==buf_func || 0==eof_func ||
 *                               0==missed_file_func || 0==addr} or the
 *                               multicast group address couldn't be converted
 *                               into a binary IP address.
 * @retval     ENOMEM            Out of memory. \c log_add() called.
 * @retval     -1                Other failure. \c log_add() called.
 */
int vcmtpReceiver_new(
    VcmtpCReceiver** const      receiver,
    const char* const           tcpAddr,
    const unsigned short        tcpPort,
    const BofFunc               bof_func,
    const EofFunc               eof_func,
    const MissedFileFunc        missed_file_func,
    const char* const           mcastAddr,
    const unsigned short        mcastPort,
    void* const                 obj)
{
    VcmtpCReceiver* rcvr = (VcmtpCReceiver*)LOG_MALLOC(sizeof(VcmtpCReceiver),
            "VCMTP receiver");

    if (0 == rcvr)
        return ENOMEM;

    try {
        vcmtpReceiver_init(rcvr, tcpAddr, tcpPort, bof_func, eof_func,
                missed_file_func, mcastAddr, mcastPort, obj);
        *receiver = rcvr;
        return 0;
    }
    catch (const std::invalid_argument& e) {
        log_add("%s", e.what());
        free(rcvr);
        return EINVAL;
    }
    catch (const std::exception& e) {
        log_add("%s", e.what());
        free(rcvr);
        return -1;
    }
}

/**
 * Frees the resources of a VCMTP C Receiver.
 *
 * @param[in,out] receiver      The VCMTP C Receiver.
 */
void vcmtpReceiver_free(
    VcmtpCReceiver* const       receiver)
{
    delete receiver->receiver;
    free(receiver);
}

/**
 * Executes a VCMTP C Receiver. Returns when the receiver terminates.
 *
 * @param[in,out] receiver      The VCMTP C Receiver.
 * @retval        0             Success.
 * @retval        EINVAL        @code{receiver == NULL}. \c log_add() called.
 * @retval        -1            Other failure. \c log_add() called.
 */
int vcmtpReceiver_execute(
    const VcmtpCReceiver* const receiver)
{
    if (0 == receiver) {
        LOG_ADD0("NULL receiver argument");
        return EINVAL;
    }

    try {
        receiver->receiver->RunReceivingThread();
        return 0;
    }
    catch (const std::exception& e) {
        LOG_ADD1("%s", e.what());
        return -1;
    }
}

/**
 * Indicates if the VCMTP file is wanted or not.
 *
 * @param[in] file_entry  VCMTP file metadata.
 * @return    1           The file is wanted.
 * @retval    0           The file is not wanted.
 */
int vcmtpFileEntry_isWanted(
    const void* const file_entry)
{
    return ((VcmtpFileEntry*)file_entry)->isWanted();
}

/**
 * Indicates if the transfer mode of a file being received is to memory.
 *
 * @param[in] file_entry        Metadata about the file.
 * @return    true              if and only if the transfer mode is to memory.
 */
bool vcmtpFileEntry_isMemoryTransfer(
    const void* const           file_entry)
{
    return ((VcmtpFileEntry*)file_entry)->isMemoryTransfer();
}

/**
 * Returns the identifier of the file.
 *
 * @param[in] file_entry        Metadata about the file.
 * @return                      The identifier of the file.
 */
VcmtpFileId vcmtpFileEntry_getFileId(
    const void*                 file_entry)
{
    return ((const VcmtpFileEntry*)file_entry)->getFileId();
}

/**
 * Returns the name of the file.
 *
 * @param[in] file_entry        Metadata about the file.
 * @return                      The name of the file.
 */
const char* vcmtpFileEntry_getFileName(
    const void*                 file_entry)
{
    return ((const VcmtpFileEntry*)file_entry)->getName();
}

/**
 * Returns the size of the file in bytes.
 *
 * @param[in] file_entry        Metadata about the file.
 * @return                      The size of the file in bytes.
 */
size_t vcmtpFileEntry_getSize(
    const void*                 file_entry)
{
    return ((VcmtpFileEntry*)file_entry)->getSize();
}

/**
 * Sets the beginning-of-file response in a file-entry to ignore the file.
 *
 * @param[in,out] file_entry    The VCMTP file-entry in which to set the
 *                              response.
 */
void vcmtpFileEntry_setBofResponseToIgnore(
    void* const                 file_entry)
{
    ((VcmtpFileEntry*)file_entry)->setBofResponseToIgnore();
}

/**
 * Sets the beginning-of-file response in a file-entry.
 *
 * @param[in,out] fileEntry     The VCMTP file-entry in which to set the
 *                              BOF response.
 * @param[in]     bofResponse   Pointer to the beginning-of-file response.
 * @retval        0             Success.
 * @retval        EINVAL        if @code{fileEntry == NULL || bofResponse ==
 *                              NULL}. \c log_add() called.
 */
int vcmtpFileEntry_setBofResponse(
    void* const       fileEntry,
    const void* const bofResponse)
{
    VcmtpFileEntry* const    entry = (VcmtpFileEntry*)fileEntry;
    const BofResponse* const bof = (const BofResponse*)bofResponse;

    if (fileEntry == NULL || bofResponse == NULL) {
        LOG_ADD0("NULL argument");
        return EINVAL;
    }

    entry->setBofResponse(bof);
    return 0;
}

/**
 * Returns the beginning-of-file response from the receiving application
 * associated with a VCMTP file.
 *
 * @param[in] file_entry  The entry for the VCMTP file.
 * @return                The corresponding BOF response from the receiving
 *                        application. May be NULL.
 */
const void* vcmtpFileEntry_getBofResponse(
    const void* const file_entry)
{
    return ((VcmtpFileEntry*)file_entry)->getBofResponse();
}
