/**
 * Copyright 2017 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file:   mcast.h
 * @author: Steven R. Emmerson
 *
 * This file declares the C API for the Virtual Circuit Multicast Transport
 * Protocol, FMTP.
 */

#ifndef FMTP_C_API_H
#define FMTP_C_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
    extern "C" {
#endif

typedef uint32_t                 FmtpProdIndex;
#define xdr_McastFileId          xdr_u_long
typedef struct mcast_receiver    McastReceiver;
typedef struct mcast_sender      McastSender;

int mcastReceiver_new(
    McastReceiver**       receiver,
    const char* const     tcpAddr,
    unsigned short        tcpPort,
    void*                 notifier,
    const char* const     mcastAddr,
    const unsigned short  mcastPort,
    const char* const     iface);

void mcastReceiver_free(
    McastReceiver*              receiver);

int mcastReceiver_execute(
    const McastReceiver*        receiver);

void mcastReceiver_stop(
    McastReceiver* const        receiver);

/**
 * Creates an active multicast sender. Doesn't block.
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
 * @param[in]     retxTimeout   FMTP retransmission timeout in minutes. Duration
 *                              that a product will be held by the FMTP layer
 *                              before being released. If negative, then the
 *                              default timeout is used.
 * @param[in]     doneWithProd  Function to call when the FMTP layer is done
 *                              with a data-product so that its resources may be
 *                              released.
 * @param[in]     authorizer    Authorizer of remote clients.
 * @retval        0             Success. `*sender` is set. `*serverPort` is set
 *                              if the initial port number was 0.
 * @retval        1             Invalid argument. `log_add()` called.
 * @retval        2             Non-system runtime error. `log_add()` called.
 * @retval        3             System error. `log_add()` called.
 */
int
mcastSender_create(
    McastSender** const    sender,
    const char* const      serverAddr,
    unsigned short* const  serverPort,
    const char* const      groupAddr,
    const unsigned short   groupPort,
    const char* const      ifaceAddr,
    const unsigned         ttl,
    const FmtpProdIndex    iProd,
    const float            retxTimeout,
    void                 (*doneWithProd)(FmtpProdIndex iProd),
    void*                  authorizer);

/**
 * Returns the product-index of the next product to be sent.
 * @param[in]  sender  FMTP sender.
 * @return             The product-index of the next product to be sent.
 */
FmtpProdIndex
mcastSender_getNextProdIndex(
    McastSender* const    sender);

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
mcastSender_send(
    McastSender* const    sender,
    const void* const     data,
    const size_t          nbytes,
    const void* const     metadata,
    const unsigned        metaSize,
    FmtpProdIndex* const iProd);

/**
 * Terminates a multicast sender by stopping it and releasing its resources.
 *
 * @param[in] sender  The multicast sender to be terminated.
 * @retval    0       Success.
 * @retval    2       Runtime error. `log_add()` called.
 * @retval    3       System error. `log_add()` called.
 */
int
mcastSender_terminate(
    McastSender* const sender);

#ifdef __cplusplus
}
#endif

#endif
