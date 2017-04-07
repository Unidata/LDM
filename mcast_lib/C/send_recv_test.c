/**
 * This file contains utility functions for the send_test(1) and recv_test(1)
 * programs.
 *
 * Copyright 2017 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYING in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: send_recv_test.c
 * @author: Steven R. Emmerson
 */

#include "send_recv_test.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

const char* sockAddrIn_format(
        const struct sockaddr_in* const sockAddrIn,
        char* const restrict            buf,
        size_t                          buflen)
{
    char* cp = buf;
    if (inet_ntop(AF_INET, &sockAddrIn->sin_addr.s_addr, cp, buflen) == NULL) {
        perror("Couldn't format IPv4 socket address");
        return NULL;
    }
    size_t nbytes = strlen(buf);
    cp += nbytes;
    buflen -= nbytes;
    nbytes = snprintf(cp, buflen, ":%d", ntohs(sockAddrIn->sin_port));
    if (nbytes >= buflen) {
        perror("Couldn't format IPv4 socket address");
        return NULL;
    }
    return buf;
}
