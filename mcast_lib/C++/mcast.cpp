/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file defines the C API to the multicasting layer.
 *
 *   @file: mcast.cpp
 * @author: Steven R. Emmerson
 */

#include "log.h"
#include "mcast.h"
#include "PerProdNotifier.h"

#include <vcmtpRecvv3.h>
#include <vcmtpSendv3.h>

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
    vcmtpRecvv3*      receiver;
    /**
     * The per-product notifier passed to the VCMTP receiver. Pointer kept so
     * that the object can be deleted when it's no longer needed.
     */
    PerProdNotifier*  notifier;
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
 * @param[in]  bop_func               Function to call when the multicast layer
 *                                    has seen a beginning-of-product.
 * @param[in]  eop_func               Function to call when the multicast layer
 *                                    has completely received a product.
 * @param[in]  missed_prod_func       Function to call when a product is missed
 *                                    by the multicast layer.
 * @param[in]  mcastAddr              Address of the multicast group to receive.
 *                                    May be groupname or formatted IP address.
 * @param[in]  mcastPort              Port number of the multicast group.
 * @param[in]  obj                    Relevant object in the receiving
 *                                    application to pass to the above
 *                                    functions. May be NULL.
 * @throws     std::invalid_argument  if @code{0==buf_func || 0==eof_func ||
 *                                    0==missed_prod_func || 0==addr}.
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
    const BopFunc               bop_func,
    const EopFunc               eop_func,
    const MissedProdFunc        missed_prod_func,
    const char* const           mcastAddr,
    const unsigned short        mcastPort,
    void* const                 obj)
{
    std::string             hostId(tcpAddr);
    std::string             groupId(mcastAddr);
    // Following object will be deleted by `vcmtpRecvv3` destructor
    receiver->notifier =
            new PerProdNotifier(bop_func, eop_func, missed_prod_func, obj);
    receiver->receiver = new vcmtpRecvv3(hostId, tcpPort, groupId,
            mcastPort, receiver->notifier);
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
 * @param[in]  bop_func          Function to call when the multicast layer has
 *                               seen a beginning-of-product.
 * @param[in]  eop_func          Function to call when the multicast layer has
 *                               completely received a product.
 * @param[in]  missed_prod_func  Function to call when a product is missed by the
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
 *                               0==missed_prod_func || 0==addr} or the
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
    const BopFunc               bop_func,
    const EopFunc               eop_func,
    const MissedProdFunc        missed_prod_func,
    const char* const           mcastAddr,
    const unsigned short        mcastPort,
    void* const                 obj)
{
    McastReceiver* rcvr = (McastReceiver*)LOG_MALLOC(sizeof(McastReceiver),
            "multicast receiver");

    if (0 == rcvr)
        return ENOMEM;

    try {
        mcastReceiver_init(rcvr, tcpAddr, tcpPort, bop_func, eop_func,
                missed_prod_func, mcastAddr, mcastPort, obj);
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
    delete receiver->notifier;
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
        receiver->receiver->Start();
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
    receiver->receiver->Stop();
}

/**
 * Returns a new multicast sender. Starts the sender's TCP server. This method
 * doesn't block.
 *
 * @param[out]    sender        Pointer to returned sender. Caller should call
 *                              `mcastSender_free(*sender)` when it's no longer
 *                              needed.
 * @param[in]     serverAddr    Dotted-decimal IPv4 address of the interface on
 *                              which the TCP server will listen for connections
 *                              from receivers for retrieving missed
 *                              data-blocks.
 * @param[in,out] serverPort    Port number for TCP server or 0, in which case
 *                              one is chosen by the operating system.
 * @param[in]     groupAddr     Dotted-decimal IPv4 address address of the
 *                              multicast group.
 * @param[in]     groupPort     Port number of the multicast group.
 * @param[in]     ttl           Time-to-live of outgoing packets.
 *                                    0  Restricted to same host. Won't be
 *                                       output by any interface.
 *                                    1  Restricted to the same subnet. Won't be
 *                                       forwarded by a router (default).
 *                                  <32  Restricted to the same site,
 *                                       organization or department.
 *                                  <64  Restricted to the same region.
 *                                 <128  Restricted to the same continent.
 *                                 <255  Unrestricted in scope. Global.
 * @param[in]     iProd         Initial product-index. The first multicast data-
 *                              product will have this as its index.
 * @param[in]     donwWithProd  Function to call when the VCMTP layer is done
 *                              with a data-product and its resources may be
 *                              released.
 * @retval        0             Success. `*sender` is set. `*serverPort` is set
 *                              if the initial port number was 0.
 * @retval        EINVAL        One of the address couldn't  be converted into a
 *                              binary IP address. `log_start()` called.
 * @retval        ENOMEM        Out of memory. \c log_start() called.
 * @retval        -1            Other failure. \c log_start() called.
 */
int
mcastSender_new(
    void** const           sender,
    const char* const      serverAddr,
    unsigned short* const  serverPort,
    const char* const      groupAddr,
    const unsigned short   groupPort,
    const unsigned         ttl,
    const VcmtpProdIndex   iProd,
    void                  (*doneWithProd)(VcmtpProdIndex iProd))
{
    int status;

    try {
        vcmtpSendv3* send = new vcmtpSendv3(serverAddr, *serverPort, groupAddr,
                groupPort, iProd);
        try {
            if (0 == *serverPort)
                *serverPort = send->getTcpPortNum();
            *sender = send;
            status = 0;
        }
        catch (const std::exception& e) {
            delete send;
            throw;
        }
    }
    catch (const std::invalid_argument& e) {
        LOG_START1("%s", e.what());
        status = EINVAL;
    }
    catch (const std::exception& e) {
        LOG_START1("%s", e.what());
        status = -1;
    }

    return status;      // Eclipse wants to see a return
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
    delete (vcmtpSendv3*)sender;
}

/**
 * Sends a product.
 *
 * @param[in]  sender  VCMTP sender.
 * @param[in]  data    Data to send.
 * @param[in]  nbytes  Amount of data in bytes.
 * @param[out] iProd   Index of the sent product.
 * @retval     0       Success.
 * @retval     EIO     Failure. `log_start()` called.
 */
int
mcastSender_send(
    void* const           sender,
    const void* const     data,
    const size_t          nbytes,
    const void* const     metadata,
    const unsigned        metaSize,
    VcmtpProdIndex* const iProd)
{
    try {
        /*
         * The signature of the product is sent to the receiver as metadata in
         * order to allow duplicate rejection.
         */
        *iProd = ((vcmtpSendv3*)sender)->sendProduct((char*)data, nbytes,
                (char*)metadata, metaSize);     //  safe to cast away `const`s
        return 0;
    }
    catch (const std::exception& e) {
        LOG_START1("%s", e.what());
        return EIO;
    }
}
