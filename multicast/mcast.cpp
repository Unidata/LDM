/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file defines the C API to the multicasting layer.
 *
 *   @file: mcast.c
 * @author: Steven R. Emmerson
 */

#include "log.h"
#include "mcast.h"
#include "PerFileNotifier.h"

#include <BofResponse.h>
#include <VCMTPReceiver.h>
#include <VCMTPSender.h>
#include <VcmtpFileEntry.h>

#include <exception>
#include <stdexcept>
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <xdr.h>

/**
 * The multicast receiver:
 */
struct mcast_receiver {
    /**
     * The multicast-layer receiver.
     */
    VCMTPReceiver*      receiver;
};

/**
 * Initializes a multicast receiver.
 *
 * @param[out] receiver               The receiver to initialize.
 * @param[in]  tcpAddr                Address of the TCP server from which to
 *                                    retrieve missed data-blocks. May be
 *                                    hostname or IP address.
 * @param[in]  tcpPort                Port number of the TCP server to which to
 *                                    connect.
 * @param[in]  bof_func               Function to call when the multicast layer
 *                                    has seen a beginning-of-file.
 * @param[in]  eof_func               Function to call when the multicast layer
 *                                    has completely received a file.
 * @param[in]  missed_file_func       Function to call when a file is missed
 *                                    by the multicast layer.
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
 * @throws     std::exception         If the multicast receiver can't be
 *                                    initialized.
 */
static void
mcastReceiver_init(
    McastReceiver* const        receiver,
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
 * Returns a new multicast receiver.
 *
 * @param[out] receiver          Pointer to returned receiver.
 * @param[in]  tcpAddr           Address of the TCP server from which to
 *                               retrieve missed data-blocks. May be hostname or
 *                               IP address.
 * @param[in]  tcpPort           Port number of the TCP server to which to
 *                               connect.
 * @param[in]  bof_func          Function to call when the multicast layer has
 *                               seen a beginning-of-file.
 * @param[in]  eof_func          Function to call when the multicast layer has
 *                               completely received a file.
 * @param[in]  missed_file_func  Function to call when a file is missed by the
 *                               multicast layer.
 * @param[in]  mcastAddr         Address of the multicast group to receive. May
 *                               be groupname or formatted IP address.
 * @param[in]  mcastPort         Port number of the multicast group.
 * @param[in]  obj               Relevant object in the receiving application to
 *                               pass to the above functions. May be NULL.
 * @retval     0                 Success. The client should call \c
 *                               mcastReceiver_free(*receiver) when the
 *                               receiver is no longer needed.
 * @retval     EINVAL            if @code{0==buf_func || 0==eof_func ||
 *                               0==missed_file_func || 0==addr} or the
 *                               multicast group address couldn't be converted
 *                               into a binary IP address.
 * @retval     ENOMEM            Out of memory. \c log_add() called.
 * @retval     -1                Other failure. \c log_add() called.
 */
int
mcastReceiver_new(
    McastReceiver** const       receiver,
    const char* const           tcpAddr,
    const unsigned short        tcpPort,
    const BofFunc               bof_func,
    const EofFunc               eof_func,
    const MissedFileFunc        missed_file_func,
    const char* const           mcastAddr,
    const unsigned short        mcastPort,
    void* const                 obj)
{
    McastReceiver* rcvr = (McastReceiver*)LOG_MALLOC(sizeof(McastReceiver),
            "multicast receiver");

    if (0 == rcvr)
        return ENOMEM;

    try {
        mcastReceiver_init(rcvr, tcpAddr, tcpPort, bof_func, eof_func,
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
 * Frees the resources of a multicast receiver.
 *
 * @param[in,out] receiver      The multicast receiver.
 */
void
mcastReceiver_free(
    McastReceiver* const       receiver)
{
    delete receiver->receiver;
    free(receiver);
}

/**
 * Executes a multicast receiver. Blocks until the receiver is stopped.
 *
 * @param[in,out] receiver      The receiver.
 * @retval        0             Success. The receiver was stopped.
 * @retval        EINVAL        @code{receiver == NULL}. \c log_add() called.
 * @retval        -1            Other failure. \c log_add() called.
 */
int
mcastReceiver_execute(
    const McastReceiver* const receiver)
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
 * Stops a multicast receiver. Returns immediately. Undefined behavior results if
 * called from a signal handler.
 *
 * @param[in] receiver  Pointer to the multicast receiver to be stopped.
 */
void
mcastReceiver_stop(
    McastReceiver* const receiver)
{
    receiver->receiver->stop();
}

/**
 * The multicast sender:
 */
struct mcast_sender {
    /**
     * The multicast-layer sender.
     */
    VCMTPSender*      sender;
};

/**
 * Returns a new multicast sender. The sender is immediately active. This method
 * doesn't block.
 *
 * @param[out] sender            Pointer to returned sender. Caller should call
 *                               `mcastSender_free(*sender)` when it's no longer
 *                               needed.
 * @param[in]  tcpAddr           Internet address of the interface on which the
 *                               TCP server will listen for connections from
 *                               receivers for retrieving missed data-blocks.
 *                               May be hostname or formatted IP address.
 * @param[in]  tcpPort           Port number of the TCP server.
 * @param[in]  mcastAddr         Internet Address of the multicast group. May be
 *                               groupname or formatted IP address.
 * @param[in]  mcastPort         Port number of the multicast group.
 * @retval     0                 Success. `*sender` is set.
 * @retval     EINVAL            if @code{0==addr} or the multicast group
 *                               address couldn't be converted into a binary IP
 *                               address. `log_start()` called.
 * @retval     ENOMEM            Out of memory. \c log_start() called.
 * @retval     -1                Other failure. \c log_start() called.
 */
int
mcastSender_new(
    McastSender** const  sender,
    const char* const    tcpAddr,
    const unsigned short tcpPort,
    const char* const    mcastAddr,
    const unsigned short mcastPort)
{
    McastSender* sndr = (McastSender*)LOG_MALLOC(sizeof(McastSender),
            "multicast sender");

    if (0 == sndr)
        return ENOMEM;

    try {
        std::string         hostId(tcpAddr);
        std::string         mcastId(mcastAddr);

        sndr->sender = new VCMTPSender(mcastId, mcastPort, hostId, tcpPort);

        try {
            sndr->sender->JoinGroup(std::string(mcastAddr), mcastPort);
            *sender = sndr;
            return 0;
        }
        catch (const std::exception& e) {
            delete sndr->sender;
            throw;
        } /* "sndrImpl" allocated */
    } // `sndr` allocated
    catch (const std::invalid_argument& e) {
        LOG_START1("%s", e.what());
        free(sndr);
        return EINVAL;
    }
    catch (const std::exception& e) {
        LOG_START1("%s", e.what());
        free(sndr);
        return -1;
    }
}

/**
 * Indicates if the multicast file is wanted or not.
 *
 * @param[in] file_entry  multicast file metadata.
 * @return    1           The file is wanted.
 * @retval    0           The file is not wanted.
 */
int
mcastFileEntry_isWanted(
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
bool
mcastFileEntry_isMemoryTransfer(
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
McastFileId
mcastFileEntry_getFileId(
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
const char*
mcastFileEntry_getFileName(
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
size_t
mcastFileEntry_getSize(
    const void*                 file_entry)
{
    return ((VcmtpFileEntry*)file_entry)->getSize();
}

/**
 * Sets the beginning-of-file response in a file-entry to ignore the file.
 *
 * @param[in,out] file_entry    The multicast file-entry in which to set the
 *                              response.
 */
void
mcastFileEntry_setBofResponseToIgnore(
    void* const                 file_entry)
{
    ((VcmtpFileEntry*)file_entry)->setBofResponseToIgnore();
}

/**
 * Sets the beginning-of-file response in a file-entry.
 *
 * @param[in,out] fileEntry     The multicast file-entry in which to set the
 *                              BOF response.
 * @param[in]     bofResponse   Pointer to the beginning-of-file response.
 * @retval        0             Success.
 * @retval        EINVAL        if @code{fileEntry == NULL || bofResponse ==
 *                              NULL}. \c log_add() called.
 */
int
mcastFileEntry_setBofResponse(
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
 * associated with a multicast file.
 *
 * @param[in] file_entry  The entry for the multicast file.
 * @return                The corresponding BOF response from the receiving
 *                        application. May be NULL.
 */
const void*
mcastFileEntry_getBofResponse(
    const void* const file_entry)
{
    return ((VcmtpFileEntry*)file_entry)->getBofResponse();
}
