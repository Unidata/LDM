/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      Tcp.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Nov 17, 2014
 *
 * @section   LICENSE
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief     Implement the TcpBase class.
 *
 * Base class for the TcpRecv and TcpSend classes. It handles TCP connections.
 */

#include "TcpBase.h"

#include <errno.h>
#include <iostream>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include <system_error>


/**
 * Constructor of Tcp.
 */
TcpBase::TcpBase()
    : sockfd(-1)
{
}


/**
 * Destructor.
 *
 * @param[in] none
 */
TcpBase::~TcpBase()
{
    close(sockfd);
}

/**
 * Attempts to read a given number of bytes from a streaming socket. Returns
 * when that number is read, the end-of-file is encountered, or an error occurs.
 *
 * @param[in] sock     The streaming socket.
 * @param[in] buf      Pointer to a buffer.
 * @param[in] nbytes   Number of bytes to attempt to read.
 * @return             Number of bytes read. If less than `nbytes` and
 *                     `nbytes > 0`, then EOF was encountered.
 * @throws std::system_error  if an error is encountered reading from the
 *                            socket.
 */
size_t TcpBase::recvall(const int sock, void* const buf, const size_t nbytes)
{
    size_t nleft = nbytes;
    char*  ptr = (char*) buf;

    while (nleft > 0) {
        int nread = recv(sock, ptr, nleft, 0);
        if (nread < 0) {
            throw std::system_error(errno, std::system_category(),
                    "TcpBase::recvall() Error receiving from socket " +
                    std::to_string(sock));
        }
        if (nread == 0) {
#if 0
            std::cerr << "TcpBase::recvall(): Only read " <<
                    std::to_string(nread) << " of " << std::to_string(nbytes) <<
                    " requested bytes from TCP socket " << std::to_string(sock)
                    << '\n';
#endif
            break; // EOF encountered
        }
        ptr += nread;
        nleft -= nread;
    }

    return nbytes - nleft; // >= 0
}

/**
 * Attempts to read a given number of bytes. Returns when that number is read,
 * the end-of-file is encountered, or an error occurs.
 *
 * @param[in] buf      Pointer to a buffer.
 * @param[in] nbytes   Number of bytes to attempt to read.
 * @return             Number of bytes read. If less than `nbytes` and
 *                     `nbytes > 0`, then EOF was encountered.
 * @throws std::runtime_error  if an error is encountered reading from the
 *                            socket.
 */
size_t TcpBase::recvall(void* const buf, const size_t nbytes)
{
    return recvall(sockfd, buf, nbytes);
}


/**
 * Writes a given number of bytes to a given streaming socket. Returns when that
 * number is written or an error occurs.
 *
 * @param[in] sock    The streaming socket.
 * @param[in] buf     Pointer to a buffer.
 * @param[in] nbytes  Number of bytes to write.
 * @throws std::system_error  if an error is encountered writing to the
 *                            socket.
 */
void TcpBase::sendall(const int sock, const void* const buf, size_t nbytes)
{
    char*  ptr = (char*) buf;

    while (nbytes > 0) {
        int nwritten = send(sock, ptr, nbytes, 0);
        if (nwritten <= 0) {
            throw std::system_error(errno, std::system_category(),
                    "TcpBase::sendall() Error sending to socket " +
                    std::to_string(sock));
        }
        ptr += nwritten;
        nbytes -= nwritten;
    }
}


/**
 * Writes a given number of bytes to a given streaming socket. Returns when that
 * number is written or an error occurs. This is the static member function in
 * alternative to the member function sendall().
 *
 * @param[in] sock    The streaming socket.
 * @param[in] buf     Pointer to a buffer.
 * @param[in] nbytes  Number of bytes to write.
 * @throws std::system_error  if an error is encountered writing to the
 *                            socket.
 */
void TcpBase::sendallstatic(const int sock, void* const buf, size_t nbytes)
{
    char*  ptr = (char*) buf;

    while (nbytes > 0) {
        int nwritten = send(sock, ptr, nbytes, 0);
        if (nwritten <= 0) {
            throw std::system_error(errno, std::system_category(),
                    "TcpBase::sendall() Error sending to socket " +
                    std::to_string(sock));
        }
        ptr += nwritten;
        nbytes -= nwritten;
    }
}


/**
 * Writes a given number of bytes. Returns when that number is written or an
 * error occurs.
 *
 * @param[in] buf     Pointer to a buffer.
 * @param[in] nbytes  Number of bytes to write.
 * @throws std::runtime_error  if an error is encountered writing to the
 *                            socket.
 */
void TcpBase::sendall(void* const buf, const size_t nbytes)
{
    sendall(sockfd, buf, nbytes);
}
