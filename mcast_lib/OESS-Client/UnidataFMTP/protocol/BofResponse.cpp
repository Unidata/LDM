/**
 * Copyright 2014 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 * @file BofResponse.cpp
 *
 * This file defines the response by a receiving application to a
 * beginning-of-file notification from the FMTP layer.
 *
 * @author: Steven R. Emmerson
 */

#include "BofResponse.h"
#include "fmtp.h"

#include <errno.h>
#include <stdexcept>
#include <string>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

/**
 * Returns a beginning-of-file response that will cause the file to be ignored.
 * @return A BOF response that will cause the file to be ignored.
 */
const BofResponse* BofResponse::getIgnore()
{
    static char                    ignoreBuf[FMTP_PACKET_LEN];
    static const MemoryBofResponse ignore(ignoreBuf, sizeof(ignoreBuf), false);

    return &ignore;
}

/**
 * Constructs from a memory buffer.
 *
 * @param[in] buf                       The buffer into which to copy the
 *                                      received data.
 * @param[in] size                      The size of the buffer in bytes.
 * @param[in] isWanted                  Whether or not the file is wanted.
 * @param[in] ptr                       Optional pointer to receiving
 *                                      application object. May be 0.
 * @throws    std::invalid_argument     if @code{buf == 0}.
 */
MemoryBofResponse::MemoryBofResponse(
    char*        buf,
    const size_t size,
    const bool   isWanted)
:
    BofResponse(isWanted),
    buf(buf),
    size(size)
{
    if (0 == buf)
        throw std::invalid_argument(std::string("NULL buffer argument"));
}

size_t MemoryBofResponse::dispose(
    const int           sock,
    const off_t         offset,
    const size_t        nbytes) const
{
    ssize_t len;

    if (offset < 0)
        throw std::invalid_argument("Offset argument is negative");

    if (offset+nbytes > size)
        throw std::invalid_argument("(Offset + number of bytes) > file size");

    len = recv(sock, buf+offset, nbytes, MSG_WAITALL);

    if (len < 0)
        throw std::runtime_error(std::string("Couldn't read from socket: ") +
                strerror(errno));

    return len;
}
