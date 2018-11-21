/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * This file defines the C API to the FMTP layer.
 *
 *   @file: fmtp.cpp
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "log.h"
#include "fmtpRecvv3.h"
#include "fmtpSendv3.h"
#include "fmtp.h"
#include "SendingNotifier.h"

#include <errno.h>
#include <exception>
#include <stdexcept>
#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <xdr.h>

/**
 * Recursively log exception "whats".
 */
void log_what(const std::exception& e)
{
    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception& nested) {
        log_what(nested);
    }
    log_add("%s", e.what());
}


/**
 * The FMTP receiver:
 */
struct fmtp_receiver {
    /**
     * The FMTP-layer receiver.
     */
    fmtpRecvv3*      fmtpReceiver;
    /**
     * The receiving application notifier.
     */
    RecvProxy*       notifier;
};

/**
 * Initializes an FMTP receiver.
 *
 * @param[out] receiver               The receiver to initialize.
 * @param[in]  hostId                 Address of the TCP server from which to
 *                                    retrieve missed data-blocks. May be
 *                                    hostname or IPv4 address.
 * @param[in]  tcpPort                Port number of the TCP server to which to
 *                                    connect.
 * @param[in]  notifier               Receiving application notifier. Freed by
 *                                    `fmtpReceiver_free()`.
 * @param[in]  mcastAddr              Address of the multicast group to receive.
 *                                    May be groupname or IPv4 address.
 * @param[in]  mcastPort              Port number of the multicast group.
 * @param[in]  iface                  IPv4 address of interface for receiving
 *                                    multicast and unicast packets. Caller may
 *                                    free.
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
 * @throws     std::exception         If the FMTP receiver can't be initialized.
 */
static void
fmtpReceiver_init(
    FmtpReceiver* const         receiver,
    const char* const           tcpAddr,
    const unsigned short        tcpPort,
    RecvProxy* const            notifier,
    const char* const           mcastAddr,
    const unsigned short        mcastPort,
    const char* const           iface)
{
    std::string             hostId(tcpAddr);
    std::string             groupId(mcastAddr);
    receiver->notifier = notifier;
    log_debug("Creating FMTP receiver: sendHost=%s, sendPort=%hu, "
            "groupId=%s, groupPort=%hu, iface=%s", tcpAddr, tcpPort, mcastAddr,
            mcastPort, iface);
    receiver->fmtpReceiver = new fmtpRecvv3(hostId, tcpPort, groupId,
            mcastPort, notifier, iface);
}

/**
 * Returns a new FMTP receiver.
 *
 * @param[out] receiver          Pointer to returned receiver.
 * @param[in]  tcpAddr           Address of the TCP server from which to
 *                               retrieve missed data-blocks. May be hostname or
 *                               IP address.
 * @param[in]  tcpPort           Port number of the TCP server to which to
 *                               connect.
 * @param[in]  notifier          Receiving application notifier. Freed by
 *                               `fmtpReceiver_free()`.
 * @param[in]  mcastAddr         Address of the multicast group to receive. May
 *                               be group name or formatted IP address.
 * @param[in]  mcastPort         Port number of the multicast group.
 * @param[in]  iface             IPv4 address of interface for receiving
 *                               multicast and unicast  packets. Caller may
 *                               free.
 * @retval     0                 Success. The client should call
 *                               `fmtpReceiver_free(*receiver)` when the
 *                               receiver is no longer needed.
 * @retval     EINVAL            if @code{0==buf_func || 0==eof_func ||
 *                               0==missed_prod_func || 0==addr} or the
 *                               multicast group address couldn't be converted
 *                               into a binary IP address.
 * @retval     ENOMEM            Out of memory. \c log_add() called.
 * @retval     -1                Other failure. \c log_add() called.
 */
int
fmtpReceiver_new(
    FmtpReceiver** const  receiver,
    const char* const     tcpAddr,
    const unsigned short  tcpPort,
    void* const           notifier,
    const char* const     mcastAddr,
    const unsigned short  mcastPort,
    const char* const     iface)
{
    FmtpReceiver* rcvr = (FmtpReceiver*)log_malloc(sizeof(FmtpReceiver),
            "FMTP receiver");

    if (0 == rcvr)
        return ENOMEM;

    try {
        fmtpReceiver_init(rcvr, tcpAddr, tcpPort, (RecvProxy*)notifier,
                mcastAddr, mcastPort, iface);
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
 * Frees the resources of an FMTP receiver.
 *
 * @param[in,out] receiver      The FMTP receiver.
 */
void
fmtpReceiver_free(
    FmtpReceiver* const       receiver)
{
    // FMTP call
    delete receiver->fmtpReceiver;
    delete receiver->notifier;
    free(receiver);
}

/**
 * Executes an FMTP receiver. Doesn't return until an error occurs or
 * `fmtpReceiver_stop()` is called.
 *
 * @param[in,out] receiver      The receiver.
 * @retval        0             Success.
 * @retval        EINVAL        @code{receiver == NULL}. \c log_add() called.
 * @retval        -1            Other failure. \c log_add() called.
 */
int
fmtpReceiver_execute(
    const FmtpReceiver* const receiver)
{
    int status;

    if (0 == receiver) {
        log_add("NULL receiver argument");
        status = EINVAL;
    }
    else {
        status = -1;
        try {
            // FMTP call
            receiver->fmtpReceiver->Start();
            status = 0;
        }
        catch (const std::exception& e) {
            log_what(e);
        }
    }
    return status;
}

void
fmtpReceiver_stop(
    FmtpReceiver* const receiver)
{
    receiver->fmtpReceiver->Stop();
}

/**
 * The FMTP sender:
 */
struct fmtp_sender {
    /**
     * The FMTP sender:
     */
    fmtpSendv3*            fmtpSender;
    /**
     * The per-product notifier passed to the FMTP sender. Pointer kept so
     * that the object can be deleted when it's no longer needed.
     */
    SendingNotifier* notifier;
};

/**
 * Initializes a new FMTP sender. The sender isn't active until
 * `fmtpSender_start()` is called.
 *
 * @param[in]     sender        Pointer to sender to be initialized.
 * @param[in]     serverAddr    Internet address of the sending FMTP
 *                              server
 * @param[in]     serverPort    Port number of the sending FMTP server in host
 *                              byte order
 * @param[in]     groupAddr     Internet socket address of the multicast group.
 * @param[in]     groupPort     Port number of the multicast group in host byte
 *                              order
 * @param[in]     mcastIface    IP address of the interface to use to send
 *                              multicast packets. "0.0.0.0" obtains the default
 *                              multicast interface. Caller may free.
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
 * @param[in]     retxTimeout   FMTP retransmission timeout in minutes. Duration
 *                              that a product will be held by the FMTP layer
 *                              before being released. If negative, then the
 *                              default timeout is used.
 * @param[in]     doneWithProd  Function to call when the FMTP layer is done
 *                              with a data-product so that its resources may be
 *                              released.
 * @param[in]     authorizer    Authorizer of remote clients
 * @retval        0             Success. `*sender` is set.
 * @retval        1             Invalid argument. `log_add()` called.
 * @retval        2             Non-system runtime error. `log_add()` called.
 * @retval        3             System error. `log_add()` called.
 */
static int
fmtpSender_init(
    FmtpSender* const      sender,
    const char* const      serverAddr,
    const unsigned short   serverPort,
    const char* const      groupAddr,
    const unsigned short   groupPort,
    const char* const      mcastIface,
    const unsigned         ttl,
    const FmtpProdIndex    iProd,
    const float            retxTimeout,
    void                 (*doneWithProd)(FmtpProdIndex iProd),
    void*                  authorizer)
{
    int status;

    try {
        SendingNotifier* notifier =
                new SendingNotifier(doneWithProd,
                        *static_cast<Authorizer*>(authorizer));

        try {
            fmtpSendv3* fmtpSender = retxTimeout < 0
                    ? new fmtpSendv3(serverAddr, serverPort, groupAddr,
                            groupPort, notifier, ttl, mcastIface, iProd)
                    : new fmtpSendv3(serverAddr, serverPort, groupAddr,
                            groupPort, notifier, ttl, mcastIface, iProd,
                            retxTimeout);
            sender->fmtpSender = fmtpSender;
            sender->notifier = notifier;
            status = 0;
        }
        catch (const std::invalid_argument& e) {
            log_what(e);
            status = 1;
        }
        catch (const std::exception& e) {
            log_what(e);
            status = 3;
        }
        if (status) {
            log_add("Couldn't create new FMTP sender");
            delete notifier;
        }
    }
    catch (const std::exception& e) {
        log_what(e);
        log_add("Couldn't create new per-product sending-notifier");
        status = 3;
    }

    return status;
}

/**
 * Returns a new FMTP sender. The sender isn't active until `fmtpSender_start()`
 * is called.
 *
 * @param[out]    sender        Pointer to returned sender. Caller should call
 *                              `fmtpSender_free(*sender)` when it's no longer
 *                              needed.
 * @param[in]     serverAddr    Internet address of sending FMTP server
 * @param[in]     serverPort    Port number of the sending FMTP server
 * @param[in]     groupAddr     Internet address of the multicast group.
 * @param[in]     groupPort     Port number of the multicast group
 * @param[in]     mcastIface    IP address of the interface to use to send
 *                              multicast packets. "0.0.0.0" obtains the default
 *                              multicast interface. Caller may free.
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
 * @param[in]     retxTimeout   FMTP retransmission timeout in minutes. Duration
 *                              that a product will be held by the FMTP layer
 *                              before being released. If negative, then the
 *                              default timeout is used.
 * @param[in]     doneWithProd  Function to call when the FMTP layer is done
 *                              with a data-product so that its resources may be
 *                              released.
 * @param[in]     authorizer    Authorizer of remote clients
 * @retval        0             Success. `*sender` is set.
 * @retval        1             Invalid argument. `log_add()` called.
 * @retval        2             Non-system runtime error. `log_add()` called.
 * @retval        3             System error. `log_add()` called.
 */
static int
fmtpSender_new(
    FmtpSender** const     sender,
    const char* const      serverAddr,
    const in_port_t        serverPort,
    const char* const      groupAddr,
    const in_port_t        groupPort,
    const char* const      mcastIface,
    const unsigned         ttl,
    const FmtpProdIndex    iProd,
    const float            retxTimeout,
    void                 (*doneWithProd)(FmtpProdIndex iProd),
    void*                  authorizer)
{
    FmtpSender* const send = (FmtpSender*)log_malloc(sizeof(FmtpSender),
            "FMTP sender");
    int                status;

    if (send == NULL) {
        status = 3;
    }
    else {
        status = fmtpSender_init(send, serverAddr, serverPort, groupAddr,
                groupPort, mcastIface, ttl, iProd, retxTimeout, doneWithProd,
                authorizer);

        if (status) {
            log_add("Couldn't initialize FMTP sender");
        }
        else {
            *sender = send;
        }
    }

    return status;      // Eclipse wants to see a return
}

/**
 * Starts an FMTP sender. Returns immediately.
 *
 * @param[in]  sender      The sender to be started.
 * @param[out] serverPort  Port number of the FMTP TCP server in host
 *                         byte-order.
 * @retval     0           Success. `fmtpSender_stop()` was called.
 *                         `*serverPort` is set.
 * @retval     2           Non-system runtime error. `log_add()` called.
 * @retval     3           System error. `log_add()` called.
 */
static int
fmtpSender_start(
        FmtpSender* const     sender,
        unsigned short* const serverPort)
{
    int status;

    log_debug("Starting FMTP sender");
    try {
        sender->fmtpSender->Start(); // Doesn't block

        try {
            *serverPort = sender->fmtpSender->getTcpPortNum();
            status = 0;
        }
        catch(std::system_error& e) {
            log_what(e);
            log_add("Couldn't get TCP port number of FMTP sender");
            sender->fmtpSender->Stop();
            status = 3;
        }
    }
    catch (std::runtime_error& e) {
        log_what(e);
        status = 2;
    }
    catch (std::exception& e) {
        log_what(e);
        status = 3;
    }

    return status;
}

/**
 * Stops an FMTP sender. Blocks until the sender has stopped.
 *
 * @param[in] sender  The sender to be stopped.
 * @retval    0       Success.
 * @retval    2       Runtime error. `log_add()` called.
 * @retval    3       System error. `log_add()` called.
 */
static int
fmtpSender_stop(
        FmtpSender* const sender)
{
    int status;

    try {
        sender->fmtpSender->Stop();
        status = 0;
    }
    catch (std::runtime_error& e) {
        log_what(e);
        status = 2;
    }
    catch (std::exception& e) {
        log_what(e);
        status = 3;
    }

    return status;
}

/**
 * Frees an FMTP sender's resources.
 *
 * @param[in] sender  The FMTP sender whose resources are to be freed.
 */
static void
fmtpSender_free(
    FmtpSender* const sender)
{
    delete sender->fmtpSender;
    delete sender->notifier;
    free(sender);
}

int
fmtpSender_create(
    FmtpSender** const     sender,
    const char* const      serverAddr,
    in_port_t* const       serverPort,
    const char* const      groupAddr,
    const in_port_t        groupPort,
    const char* const      mcastIface,
    const unsigned         ttl,
    const FmtpProdIndex    iProd,
    const float            retxTimeout,
    void                 (*doneWithProd)(FmtpProdIndex iProd),
    void*                  authorizer)
{
    FmtpSender*  send;
    int          status = fmtpSender_new(&send, serverAddr, *serverPort,
            groupAddr, groupPort, mcastIface, ttl, iProd, retxTimeout,
            doneWithProd, authorizer);

    if (status) {
        log_add("Couldn't create new FMTP sender");
    }
    else {
        status = fmtpSender_start(send, serverPort); // Doesn't block

        if (status) {
            log_add("Couldn't start FMTP sender");
            fmtpSender_free(send);
        }
        else {
            *sender = send;
        }
    }

    return status;
}

/**
 * Returns the product-index of the next product to be sent.
 * @param[in]  sender  FMTP sender.
 * @return the product-index of the next product to be sent.
 */
FmtpProdIndex
fmtpSender_getNextProdIndex(
    FmtpSender* const    sender)
{
  return sender->fmtpSender->getNextProdIndex();
}

/**
 * Sends a product.
 *
 * @param[in]  sender  FMTP sender.
 * @param[in]  data    Data to send.
 * @param[in]  nbytes  Amount of data in bytes.
 * @param[out] iProd   Index of the sent product.
 * @retval     0       Success.
 * @retval     EIO     Failure. `log_add()` called.
 */
int
fmtpSender_send(
    FmtpSender* const    sender,
    const void* const     data,
    const size_t          nbytes,
    const void* const     metadata,
    const unsigned        metaSize,
    FmtpProdIndex* const iProd)
{
    try {
        /*
         * The signature of the product is sent to the receiver as metadata in
         * order to allow duplicate rejection.
         */
        *iProd = sender->fmtpSender->sendProduct((void*)data, nbytes,
                (void*)metadata, metaSize);     //  safe to cast away `const`s
        return 0;
    }
    catch (const std::exception& e) {
        log_what(e);
        return EIO;
    }
}

/**
 * Terminates an FMTP sender by stopping it and releasing its resources.
 *
 * @param[in] sender  The FMTP sender to be terminated.
 * @retval    0       Success.
 * @retval    2       Non-system runtime error. `log_add()` called.
 * @retval    3       System error. `log_add()` called.
 */
int
fmtpSender_terminate(
    FmtpSender* const sender)
{
    int status = fmtpSender_stop(sender);
    fmtpSender_free(sender);
    return status;
}
