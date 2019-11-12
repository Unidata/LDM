/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: send_recv_test.h
 * @author: Steven R. Emmerson
 *
 * This file contains common definitions for `send_test.c` and `recv_test.c`.
 */

#ifndef SEND_RECV_TEST_H_
#define SEND_RECV_TEST_H_

#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>

//#define HELLO_PORT  12345
//#define HELLO_PORT  1 // Requires superuser privileges
#define HELLO_PORT  38800

//#define HELLO_GROUP "239.0.0.37" // Works. 239/8 is "administratively scoped"

#if 0
/*
 * Unicast-based multicast address based on UCAR subnet.
 */
#define HELLO_GROUP "234.128.117.1"
#else
/*
 * Source-specific multicast (SSM) address. According to RFC4607:
 *   - Source-specific multicast address in the range 232.0.0/24 are reserved
 *     and must not be used
 *   - "The policy for allocating the rest of the SSM addresses to sending
 *     applications is strictly locally determined by the sending host."
 */
#define HELLO_GROUP "232.128.117.1"
#endif


#ifdef __cplusplus
    extern "C" {
#endif

const char* sockAddrIn_format(
        const struct sockaddr_in* const sockAddr,
        char* const restrict            buf,
        const size_t                    buflen);

#ifdef __cplusplus
    }
#endif

#endif /* SEND_RECV_TEST_H_ */
