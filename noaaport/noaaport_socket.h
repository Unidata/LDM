/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: noaaport_socket.h
 * @author: Steven R. Emmerson
 *
 * This file ...
 */

#ifndef NOAAPORT_SOCKET_H_
#define NOAAPORT_SOCKET_H_



#ifdef __cplusplus
    extern "C" {
#endif


/**
 * Initializes a socket for receiving a NOAAPORT multicast.
 *
 * @param[out] socket     The socket. The caller should close when it's no
 *                        longer needed.
 * @param[in]  nportSpec  IPv4 address of the NOAAPORT multicast.
 * @param[in]  ifaceSpec  IPv4 address of interface on which to listen for
 *                        multicast UDP packets or NULL to listen on all
 *                        available interfaces.
 * @retval     0          Success. `*socket` is set.
 * @retval     1          Usage failure. \c log_start() called.
 * @retval     2          O/S failure. \c log_start() called.
 */
int
nportSock_init(
    int* const restrict        socket,
    const char* const restrict nportSpec,
    const char* const restrict ifaceSpec);


#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_SOCKET_H_ */
