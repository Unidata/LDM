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
#include <string>

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
 * @param[in,out] cReceiver              The VCMTP C Receiver to initialize.
 * @param[in]     bof_func               Function to call when the VCMTP layer
 *                                       has seen a beginning-of-file.
 * @param[in]     eof_func               Function to call when the VCMTP layer
 *                                       has completely received a file.
 * @param[in]     missed_file_func       Function to call when a file is missed
 *                                       by the VCMTP layer.
 * @param[in]     addr                   Address of the multicast group.
 *                                        224.0.0.0-224.0.0.255 Reserved for
 *                                                              local purposes
 *                                        224.0.1.0-238.255.255.255
 *                                                              User-defined
 *                                                              multicast
 *                                                              addresses
 *                                        239.0.0.0-239.255.255.255
 *                                                              Reserved for
 *                                                              administrative
 *                                                              scoping
 * @param[in]     port                   Port number of the multicast group.
 * @param[in]     obj                    Relevant object in the receiving
 *                                       application to pass to the above
 *                                       functions. May be NULL.
 * @throws        std::invalid_argument  if @code{0==buf_func || 0==eof_func ||
 *                                       0==missed_file_func || 0==addr}.
 * @throws        std::invalid_argument  if \c addr couldn't be converted into a
 *                                       binary IPv4 address.
 * @throws        std::runtime_error     if the IP address of the PA interface
 *                                       couldn't be obtained. (The PA address
 *                                       seems to be specific to Linux and might
 *                                       cause problems.)
 * @throws        std::runtime_error     if the socket couldn't be bound to the
 *                                       interface.
 * @throws        std::runtime_error     if the socket couldn't be bound to the
 *                                       Internet address.
 * @throws        std::runtime_error     if the multicast group couldn't be
 *                                       added to the socket.
 * @throws        std::exception         If the VCMTP C Receiver can't be
 *                                       initialized.
 */
static void vcmtpReceiver_init(
    VcmtpCReceiver* const cReceiver,
    const BofFunc         bof_func,
    const EofFunc         eof_func,
    const MissedFileFunc  missed_file_func,
    const char* const     addr,
    const unsigned short  port,
    void* const           obj)
{
    VCMTPReceiver*      rec = new VCMTPReceiver(
            PerFileNotifier(bof_func, eof_func, missed_file_func, obj));

    try {
        if (0 == addr)
            throw std::invalid_argument(std::string("NULL address argument"));
        rec->JoinGroup(std::string(addr), port);
        cReceiver->receiver = rec;
    }
    catch (const std::exception& e) {
        delete rec;
        throw;
    }
}

/**
 * Returns a new VCMTP C Receiver.
 *
 * @param[in] bof_func          Function to call when the VCMTP layer has seen
 *                              a beginning-of-file.
 * @param[in] eof_func          Function to call when the VCMTP layer has
 *                              completely received a file.
 * @param[in] missed_file_func  Function to call when a file is missed by the
 *                              VCMTP layer.
 * @param[in] addr              Address of the multicast group.
 *                               224.0.0.0 - 224.0.0.255     Reserved for local
 *                                                           purposes
 *                               224.0.1.0 - 238.255.255.255 User-defined
 *                                                           multicast addresses
 *                               239.0.0.0 - 239.255.255.255 Reserved for
 *                                                           administrative
 *                                                           scoping
 * @param[in] port              Port number of the multicast group.
 * @param[in] obj               Relevant object in the receiving application to
 *                              pass to the above functions. May be NULL.
 * @retval    0                 Success. The client should call
 *                              vcmtp_receiver_free() when the receiver is no
 *                              longer needed.
 * @retval    EINVAL            if @code{0==buf_func || 0==eof_func ||
 *                              0==missed_file_func || 0==addr}.
 * @retval    ENOMEM            Out of memory. \c log_add() called.
 * @retval    -1                Other failure. \c log_add() called.
 */
int vcmtpReceiver_new(
    VcmtpCReceiver** const      cReceiver,
    const BofFunc               bof_func,
    const EofFunc               eof_func,
    const MissedFileFunc        missed_file_func,
    const char* const           addr,
    const unsigned short        port,
    void* const                 obj)
{
    VcmtpCReceiver*      rec = (VcmtpCReceiver*)LOG_MALLOC(sizeof(VcmtpCReceiver),
            "VCMTP receiver");

    if (0 == rec)
        return ENOMEM;

    try {
        vcmtpReceiver_init(rec, bof_func, eof_func, missed_file_func,
                addr, port, obj);
        *cReceiver = rec;
        return 0;
    }
    catch (const std::invalid_argument& e) {
        log_add("%s", e.what());
        free(rec);
        return EINVAL;
    }
    catch (const std::exception& e) {
        log_add("%s", e.what());
        free(rec);
        return -1;
    }
}

/**
 * Frees the resources of a VCMTP C Receiver.
 *
 * @param[in,out] cReceiver      The VCMTP C Receiver.
 */
void vcmtpReceiver_free(
    VcmtpCReceiver* const       cReceiver)
{
    delete cReceiver->receiver;
    free(cReceiver);
}

/**
 * Executes a VCMTP C Receiver. Returns when the receiver terminates.
 *
 * @param[in,out] cReceiver     The VCMTP C Receiver.
 * @retval        0             Success.
 * @retval        EINVAL        @code{cReceiver == NULL}. \c log_add() called.
 * @retval        -1            Other failure. \c log_add() called.
 */
int vcmtpReceiver_execute(
    const VcmtpCReceiver* const cReceiver)
{
    if (0 == cReceiver) {
        LOG_ADD0("NULL receiver argument");
        return EINVAL;
    }

    try {
        cReceiver->receiver->RunReceivingThread();
        return 0;
    }
    catch (const std::exception& e) {
        LOG_ADD1("%s", e.what());
        return -1;
    }
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
 * Returns the name of the file.
 *
 * @param[in] file_entry        Metadata about the file.
 * @return                      The name of the file.
 */
const char* vcmtpFileEntry_getName(
    const void*                 file_entry)
{
    return ((VcmtpFileEntry*)file_entry)->getName();
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
 * Sets the response in a file-entry to a beginning-of-file notification of
 * a memory transfer.
 *
 * @param[in,out] file_entry    The VCMTP file-entry in which to set the
 *                              response.
 * @param[in]     buf           The receiving buffer for the file.
 * @param[in]     size          The size of the receiving buffer in bytes.
 * @retval        0             Success.
 * @retval        EINVAL        if @code{buf == NULL}. \c log_add() called.
 * @retval        ENOMEM        Out of memory. \c log_add() called.
 */
int vcmtpFileEntry_setMemoryBofResponse(
    void* const                 file_entry,
    unsigned char* const        buf,
    const size_t                size)
{
    try {
        BofResponse bofResponse(new MemoryBofResponse(buf, size));

        ((VcmtpFileEntry*)file_entry)->setBofResponse(bofResponse);
        return 0;
    }
    catch (const std::invalid_argument& e) {
        LOG_ADD1("%s", e.what());
        return EINVAL;
    }
    catch (const std::bad_alloc& e) {
        LOG_ADD1("Couldn't allocate BOF response for memory transfer: %s",
                e.what());
        return ENOMEM;
    }
}
