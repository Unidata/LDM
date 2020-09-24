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
#ifdef LDM_LOGGING
    #include "log.h"
#endif

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

bool TcpBase::recvall(const int sock, void* const buf, const size_t nbytes)
{
	/*
	 * Because a TCP connection is a byte-stream, it doesn't make sense to
	 * return the number of bytes read because it will either be the number
	 * requested or an EOF will have been encountered. Hence, this function
	 * returns a boolean.
	 */
    size_t nleft = nbytes;
    char*  ptr = (char*) buf;

    while (nleft > 0) {
        auto nread = recv(sock, ptr, nleft, 0);
        if (nread < 0)
            throw std::system_error(errno, std::system_category(),
                    "TcpBase::recvall() Error reading from socket " +
                    std::to_string(sock));
        if (nread == 0) {
#if 0
            std::cerr << "TcpBase::recvall(): Only read " <<
                    std::to_string(nread) << " of " << std::to_string(nbytes) <<
                    " requested bytes from TCP socket " << std::to_string(sock)
                    << '\n';
#endif
            return false; // EOF encountered
        }

        ptr += nread;
        nleft -= nread;
    }

    return true;
}

bool TcpBase::recvall(void* const buf, const size_t nbytes)
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
void TcpBase::sendallstatic(const int sock, const void* const buf, size_t nbytes)
{
    const char*  ptr = (const char*) buf;

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
 * @throws std::system_error  if an error is encountered writing to the
 *                            socket.
 */
void TcpBase::sendall(const void* const buf, const size_t nbytes)
{
    sendall(sockfd, buf, nbytes);
}

void TcpBase::write(const int          sd,
		            const std::string& string)
{
    #ifdef LDM_LOGGING
		log_debug("Sending %s-byte string on socket %d",
				std::to_string(string.size()).c_str(), sd);
    #endif
    uint32_t len = htonl(string.size());
    sendall(sd, &len, sizeof(len));
    sendall(sd, string.data(), string.size());
}

void TcpBase::write(const std::string& string)
{
	write(sockfd, string);
}

void TcpBase::read(const int    sd,
		           std::string& string)
{
    #ifdef LDM_LOGGING
		log_debug("Receiving string length on socket %d", sd);
    #endif
    uint32_t len;
    if (!recvall(sd, &len, sizeof(len)))
    	throw std::runtime_error("TcpBase::read(): EOF");
    len = ntohl(len);

    #ifdef LDM_LOGGING
		log_debug("Receiving %u-byte string content on socket %d", len, sd);
    #endif
    char buf[len];
    if (!recvall(sd, buf, len))
    	throw std::runtime_error("TcpBase::read(): EOF");

    string.assign(buf, len);
}

void TcpBase::read(std::string& string)
{
	read(sockfd, string);
}
