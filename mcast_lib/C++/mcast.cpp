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

#include "mylog.h"
#include "mcast.h"
#include "PerProdSendingNotifier.h"

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
 * Recursively log exception "whats".
 */
void log_what(const std::exception& e)
{
    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception& nested) {
        log_what(nested);
    }
    mylog_error(e.what());
}


/**
 * The multicast receiver:
 */
struct mcast_receiver {
    /**
     * The multicast-layer receiver.
     */
    vcmtpRecvv3*      vcmtpReceiver;
    /**
     * The receiving application notifier.
     */
    RecvAppNotifier*  notifier;
};

/**
 * Initializes a multicast receiver.
 *
 * @param[out] receiver               The receiver to initialize.
 * @param[in]  hostId                 Address of the TCP server from which to
 *                                    retrieve missed data-blocks. May be
 *                                    hostname or IPv4 address.
 * @param[in]  tcpPort                Port number of the TCP server to which to
 *                                    connect.
 * @param[in]  notifier               Receiving application notifier. Freed by
 *                                    `mcastReceiver_free()`.
 * @param[in]  mcastAddr              Address of the multicast group to receive.
 *                                    May be groupname or IPv4 address.
 * @param[in]  mcastPort              Port number of the multicast group.
 * @param[in]  mcastIface             IP address of interface for receiving
 *                                    multicast packets.
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
    RecvAppNotifier* const      notifier,
    const char* const           mcastAddr,
    const unsigned short        mcastPort,
    const char* const           mcastIface)
{
    std::string             hostId(tcpAddr);
    std::string             groupId(mcastAddr);
    receiver->notifier = notifier;
    receiver->vcmtpReceiver = new vcmtpRecvv3(hostId, tcpPort, groupId,
            mcastPort, notifier, mcastIface);
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
 * @param[in]  notifier          Receiving application notifier. Freed by
 *                               `mcastReceiver_free()`.
 * @param[in]  mcastAddr         Address of the multicast group to receive. May
 *                               be groupname or formatted IP address.
 * @param[in]  mcastPort         Port number of the multicast group.
 * @param[in]  mcastIface        IP address of interface for receiving multicast
 *                               packets.
 * @retval     0                 Success. The client should call \c
 *                               mcastReceiver_free(*receiver) when the
 *                               receiver is no longer needed.
 * @retval     EINVAL            if @code{0==buf_func || 0==eof_func ||
 *                               0==missed_prod_func || 0==addr} or the
 *                               multicast group address couldn't be converted
 *                               into a binary IP address.
 * @retval     ENOMEM            Out of memory. \c mylog_add() called.
 * @retval     -1                Other failure. \c mylog_add() called.
 */
int
mcastReceiver_new(
    McastReceiver** const receiver,
    const char* const     tcpAddr,
    const unsigned short  tcpPort,
    void* const           notifier,
    const char* const     mcastAddr,
    const unsigned short  mcastPort,
    const char* const     mcastIface)
{
    McastReceiver* rcvr = (McastReceiver*)mylog_malloc(sizeof(McastReceiver),
            "multicast receiver");

    if (0 == rcvr)
        return ENOMEM;

    try {
        mcastReceiver_init(rcvr, tcpAddr, tcpPort, (RecvAppNotifier*)notifier,
                mcastAddr, mcastPort, mcastIface);
        *receiver = rcvr;
        return 0;
    }
    catch (const std::invalid_argument& e) {
        mylog_add("%s", e.what());
        free(rcvr);
        return EINVAL;
    }
    catch (const std::exception& e) {
        mylog_add("%s", e.what());
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
    delete receiver->vcmtpReceiver;
    delete receiver->notifier;
    free(receiver);
}

/**
 * Executes a multicast receiver. Doesn't return until an error occurs or
 * `mcastReceiver_stop()` is called.
 *
 * @param[in,out] receiver      The receiver.
 * @retval        0             Success.
 * @retval        EINVAL        @code{receiver == NULL}. \c mylog_add() called.
 * @retval        -1            Other failure. \c mylog_add() called.
 */
int
mcastReceiver_execute(
    const McastReceiver* const receiver)
{
    int status;

    if (0 == receiver) {
        mylog_add("NULL receiver argument");
        status = EINVAL;
    }
    else {
        status = -1;
        try {
            // VCMTP call
            receiver->vcmtpReceiver->Start();
            status = 0;
        }
        catch (const std::exception& e) {
            log_what(e);
        }
    }
    return status;
}

/**
 * Stops a multicast receiver. Undefined behavior results if called from a
 * signal handler that was invoked by the delivery of a signal during execution
 * of an async-signal-unsafe function. Idempotent.
 *
 * @param[in] receiver  Pointer to the multicast receiver to be stopped.
 */
void
mcastReceiver_stop(
    McastReceiver* const receiver)
{
    receiver->vcmtpReceiver->Stop();
}

/**
 * The multicast sender:
 */
struct mcast_sender {
    /**
     * The VCMTP sender:
     */
    vcmtpSendv3*            vcmtpSender;
    /**
     * The per-product notifier passed to the VCMTP sender. Pointer kept so
     * that the object can be deleted when it's no longer needed.
     */
    PerProdSendingNotifier* notifier;
};

/**
 * Initializes a new multicast sender. The sender isn't active until
 * `mcastSender_start()` is called.
 *
 * @param[in]     sender        Pointer to sender to be initialized.
 * @param[in]     serverAddr    Dotted-decimal IPv4 address of the interface on
 *                              which the TCP server will listen for connections
 *                              from receivers for retrieving missed
 *                              data-blocks.
 * @param[in]     serverPort    Port number for TCP server or 0, in which case
 *                              one is chosen by the operating system.
 * @param[in]     groupAddr     Dotted-decimal IPv4 address address of the
 *                              multicast group.
 * @param[in]     groupPort     Port number of the multicast group.
 * @param[in]     ifaceAddr     IP address of the interface to use to send
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
 * @param[in]     timeoutFactor Ratio of the duration that a data-product will
 *                              be held by the VCMTP layer before being released
 *                              after being multicast to the duration to
 *                              multicast the product. If negative, then the
 *                              default timeout factor is used.
 * @param[in]     doneWithProd  Function to call when the VCMTP layer is done
 *                              with a data-product so that its resources may be
 *                              released.
 * @retval        0             Success. `*sender` is set.
 * @retval        1             Invalid argument. `mylog_add()` called.
 * @retval        2             Non-system runtime error. `mylog_add()` called.
 * @retval        3             System error. `mylog_add()` called.
 */
static int
mcastSender_init(
    McastSender* const     sender,
    const char* const      serverAddr,
    const unsigned short   serverPort,
    const char* const      groupAddr,
    const unsigned short   groupPort,
    const char* const      ifaceAddr,
    const unsigned         ttl,
    const VcmtpProdIndex   iProd,
    const float            timeoutFactor,
    void                  (*doneWithProd)(VcmtpProdIndex iProd))
{
    int status;

    try {
        PerProdSendingNotifier* notifier =
                new PerProdSendingNotifier(doneWithProd);

        try {
            vcmtpSendv3* vcmtpSender = timeoutFactor < 0
                    ? new vcmtpSendv3(serverAddr, serverPort, groupAddr,
                            groupPort, notifier, ttl, ifaceAddr, iProd)
                    : new vcmtpSendv3(serverAddr, serverPort, groupAddr,
                            groupPort, notifier, ttl, ifaceAddr, iProd,
                            timeoutFactor);
            sender->vcmtpSender = vcmtpSender;
            sender->notifier = notifier;
            status = 0;
        }
        catch (const std::invalid_argument& e) {
            log_what(e);
            delete notifier;
            status = 1;
        }
        catch (const std::exception& e) {
            delete notifier;
            throw;
        }
    }
    catch (const std::exception& e) {
        log_what(e);
        status = 3;
    }

    return status;      // Eclipse wants to see a return
}

/**
 * Returns a new multicast sender. The sender isn't active until
 * `mcastSender_start()` is called.
 *
 * @param[out]    sender        Pointer to returned sender. Caller should call
 *                              `mcastSender_free(*sender)` when it's no longer
 *                              needed.
 * @param[in]     serverAddr    Dotted-decimal IPv4 address of the interface on
 *                              which the TCP server will listen for connections
 *                              from receivers for retrieving missed
 *                              data-blocks.
 * @param[in]     serverPort    Port number for TCP server or 0, in which case
 *                              one is chosen by the operating system.
 * @param[in]     groupAddr     Dotted-decimal IPv4 address address of the
 *                              multicast group.
 * @param[in]     groupPort     Port number of the multicast group.
 * @param[in]     ifaceAddr     IP address of the interface to use to send
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
 * @param[in]     timeoutFactor Ratio of the duration that a data-product will
 *                              be held by the VCMTP layer before being released
 *                              after being multicast to the duration to
 *                              multicast the product. If negative, then the
 *                              default timeout factor is used.
 * @param[in]     doneWithProd  Function to call when the VCMTP layer is done
 *                              with a data-product so that its resources may be
 *                              released.
 * @retval        0             Success. `*sender` is set.
 * @retval        1             Invalid argument. `mylog_add()` called.
 * @retval        2             Non-system runtime error. `mylog_add()` called.
 * @retval        3             System error. `mylog_add()` called.
 */
static int
mcastSender_new(
    McastSender** const    sender,
    const char* const      serverAddr,
    const unsigned short   serverPort,
    const char* const      groupAddr,
    const unsigned short   groupPort,
    const char* const      ifaceAddr,
    const unsigned         ttl,
    const VcmtpProdIndex   iProd,
    const float            timeoutFactor,
    void                  (*doneWithProd)(VcmtpProdIndex iProd))
{
    McastSender* const send = (McastSender*)mylog_malloc(sizeof(McastSender),
            "multicast sender");
    int                status;

    if (send == NULL) {
        status = 3;
    }
    else {
        status = mcastSender_init(send, serverAddr, serverPort, groupAddr,
                groupPort, ifaceAddr, ttl, iProd, timeoutFactor, doneWithProd);

        if (0 == status)
            *sender = send;
    }

    return status;      // Eclipse wants to see a return
}

/**
 * Starts a multicast sender. Returns immediately.
 *
 * @param[in]  sender      The sender to be started.
 * @param[out] serverPort  Port number of the multicast TCP server in host
 *                         byte-order.
 * @retval     0           Success. `mcastSender_stop()` was called.
 *                         `*serverPort` is set.
 * @retval     2           Non-system runtime error. `mylog_add()` called.
 * @retval     3           System error. `mylog_add()` called.
 */
static int
mcastSender_start(
        McastSender* const    sender,
        unsigned short* const serverPort)
{
    int status;

    mylog_debug("Starting VCMTP sender");
    try {
        sender->vcmtpSender->Start();

        try {
            *serverPort = sender->vcmtpSender->getTcpPortNum();
            status = 0;
        }
        catch(std::system_error& e) {
            log_what(e);
            sender->vcmtpSender->Stop();
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
 * Stops a multicast sender. Blocks until the sender has stopped.
 *
 * @param[in] sender  The sender to be stopped.
 * @retval    0       Success.
 * @retval    2       Runtime error. `mylog_add()` called.
 * @retval    3       System error. `mylog_add()` called.
 */
static int
mcastSender_stop(
        McastSender* const sender)
{
    int status;

    try {
        sender->vcmtpSender->Stop();
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
 * Frees a multicast sender's resources.
 *
 * @param[in] sender  The multicast sender whose resources are to be freed.
 */
static void
mcastSender_free(
    McastSender* const sender)
{
    delete sender->vcmtpSender;
    delete sender->notifier;
    free(sender);
}

/**
 * Spawns an active multicast sender. Upon return, a multicast sender is
 * executing independently.
 *
 * @param[out]    sender        Pointer to returned sender. Caller should call
 *                              `mcastSender_terminate(*sender)` when it's no
 *                              longer needed.
 * @param[in]     serverAddr    Dotted-decimal IPv4 address of the interface on
 *                              which the TCP server will listen for connections
 *                              from receivers for retrieving missed
 *                              data-blocks.
 * @param[in,out] serverPort    Port number for TCP server or 0, in which case
 *                              one is chosen by the operating system.
 * @param[in]     groupAddr     Dotted-decimal IPv4 address address of the
 *                              multicast group.
 * @param[in]     groupPort     Port number of the multicast group.
 * @param[in]     ifaceAddr     IP address of the interface to use to send
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
 * @param[in]     timeoutFactor Ratio of the duration that a data-product will
 *                              be held by the VCMTP layer before being released
 *                              after being multicast to the duration to
 *                              multicast the product. If negative, then the
 *                              default timeout factor is used.
 * @param[in]     doneWithProd  Function to call when the VCMTP layer is done
 *                              with a data-product so that its resources may be
 *                              released.
 * @retval        0             Success. `*sender` is set. `*serverPort` is set
 *                              if the initial port number was 0.
 * @retval        1             Invalid argument. `mylog_add()` called.
 * @retval        2             Non-system runtime error. `mylog_add()` called.
 * @retval        3             System error. `mylog_add()` called.
 */
int
mcastSender_spawn(
    McastSender** const    sender,
    const char* const      serverAddr,
    unsigned short* const  serverPort,
    const char* const      groupAddr,
    const unsigned short   groupPort,
    const char* const      ifaceAddr,
    const unsigned         ttl,
    const VcmtpProdIndex   iProd,
    const float            timeoutFactor,
    void                  (*doneWithProd)(VcmtpProdIndex iProd))
{
    McastSender* send;
    int          status = mcastSender_new(&send, serverAddr, *serverPort,
            groupAddr, groupPort, ifaceAddr, ttl, iProd, timeoutFactor,
            doneWithProd);

    if (0 == status) {
        status = mcastSender_start(send, serverPort);

        if (status) {
            mcastSender_free(send);
        }
        else {
            *sender = send;
        }
    }

    return status;
}

/**
 * Returns the product-index of the next product to be sent.
 * @param[in]  sender  VCMTP sender.
 * @return the product-index of the next product to be sent.
 */
VcmtpProdIndex
mcastSender_getNextProdIndex(
    McastSender* const    sender)
{
  return sender->vcmtpSender->getNextProdIndex();
}

/**
 * Sends a product.
 *
 * @param[in]  sender  VCMTP sender.
 * @param[in]  data    Data to send.
 * @param[in]  nbytes  Amount of data in bytes.
 * @param[out] iProd   Index of the sent product.
 * @retval     0       Success.
 * @retval     EIO     Failure. `mylog_add()` called.
 */
int
mcastSender_send(
    McastSender* const    sender,
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
        *iProd = sender->vcmtpSender->sendProduct((void*)data, nbytes,
                (void*)metadata, metaSize);     //  safe to cast away `const`s
        return 0;
    }
    catch (const std::exception& e) {
        log_what(e);
        return EIO;
    }
}

/**
 * Terminates a multicast sender by stopping it and releasing its resources.
 *
 * @param[in] sender  The multicast sender to be terminated.
 * @retval    0       Success.
 * @retval    2       Non-system runtime error. `mylog_add()` called.
 * @retval    3       System error. `mylog_add()` called.
 */
int
mcastSender_terminate(
    McastSender* const sender)
{
    int status = mcastSender_stop(sender);
    mcastSender_free(sender);
    return status;
}
