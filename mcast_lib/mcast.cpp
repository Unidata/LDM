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

#include <errno.h>
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
 * @param[in]  hostId                 Address of the TCP server from which to
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
    std:string             hostId(tcpAddr);
    // Following object will be deleted by `VCMTPReceiver` destructor
    PerFileNotifier* const notifier =
            new PerFileNotifier(bof_func, eof_func, missed_file_func, obj);
    // VCMTP call
    VCMTPReceiver*         rcvr = new VCMTPReceiver(hostId, tcpPort, notifier);

    try {
        // VCMTP call
        rcvr->JoinGroup(std::string(mcastAddr), mcastPort);
        receiver->receiver = rcvr;
    }
    catch (const std::exception& e) {
        // VCMTP call
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
    // VCMTP call
    delete receiver->receiver;
    free(receiver);
}

/**
 * Executes a multicast receiver. Only returns when an error occurs.
 *
 * @param[in,out] receiver      The receiver.
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
        // VCMTP call
        receiver->receiver->RunReceivingThread();
    }
    catch (const std::exception& e) {
        LOG_ADD1("%s", e.what());
    }
    return -1;
}

/**
 * Stops a multicast receiver. Blocks until the receiver stops. Undefined
 * behavior will result if called from a signal handler that was invoked by the
 * delivery of a signal during execution of an async-signal-unsafe function.
 *
 * @param[in] receiver  Pointer to the multicast receiver to be stopped.
 */
void
mcastReceiver_stop(
    McastReceiver* const receiver)
{
    // VCMTP call
    receiver->receiver->stop();
}

/**
 * Returns a new multicast sender. Starts the sender's TCP server. This method
 * doesn't block.
 *
 * @param[out] sender      Pointer to returned sender. Caller should call
 *                         `mcastSender_free(*sender)` when it's no longer
 *                         needed.
 * @param[in]  serverAddr  Dotted-decimal IPv4 address of the interface on which
 *                         the TCP server will listen for connections from
 *                         receivers for retrieving missed data-blocks.
 * @param[in]  serverPort  Port number of the TCP server.
 * @param[in]  groupAddr   Dotted-decimal IPv4 address address of the multicast
 *                         group.
 * @param[in]  groupPort   Port number of the multicast group.
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
 * @param[in]  iProd       Initial product-index. The first multicast data-
 *                         product will have this as its index.
 * @retval     0           Success. `*sender` is set.
 * @retval     EINVAL      One of the address couldn't  be converted into a
 *                         binary IP address. `log_start()` called.
 * @retval     ENOMEM      Out of memory. \c log_start() called.
 * @retval     -1          Other failure. \c log_start() called.
 */
int
mcastSender_new(
    void** const         sender,
    const char* const    serverAddr,
    const unsigned short serverPort,
    const char* const    groupAddr,
    const unsigned short groupPort,
    const unsigned       ttl,
    const McastProdIndex iProd)
{
    try {
        // VCMTP call
        VCMTPSender* sndr = new VCMTPSender(std::string(serverAddr), serverPort,
                iProd);

        try {
            // VCMTP call
            sndr->JoinGroup(std::string(groupAddr), groupPort);
            *sender = sndr;
            return 0;
        }
        catch (const std::exception& e) {
            // VCMTP call
            delete sndr;
            throw;
        } // `sndr->sender` allocated
    } // `sndr` allocated
    catch (const std::invalid_argument& e) {
        LOG_START1("%s", e.what());
        return EINVAL;
    }
    catch (const std::exception& e) {
        LOG_START1("%s", e.what());
        return -1;
    }
}

/**
 * Frees a multicast sender's resources.
 *
 * @param[in] sender  The multicast sender whose resources are to be freed.
 */
void
mcastSender_free(
    void* const sender)
{
    // VCMTP call
    delete (VCMTPSender*)sender;
}

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
    const size_t nbytes)
{
    try {
        // VCMTP call
        ((VCMTPSender*)sender)->SendMemoryData(data, nbytes);
        return 0;
    }
    catch (const std::exception& e) {
        LOG_START1("%s", e.what());
        return EIO;
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
    // VCMTP call
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
    // VCMTP call
    return ((VcmtpFileEntry*)file_entry)->isMemoryTransfer();
}

/**
 * Returns the identifier of the file.
 *
 * @param[in] file_entry        Metadata about the file.
 * @return                      The identifier of the file.
 */
McastProdIndex
mcastFileEntry_getProductIndex(
    const void*                 file_entry)
{
    // VCMTP call
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
    // VCMTP call
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
    // VCMTP call
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
    // VCMTP call
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

    // VCMTP call
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
    // VCMTP call
    return ((VcmtpFileEntry*)file_entry)->getBofResponse();
}
