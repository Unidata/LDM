/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      TcpRecv.cpp
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
 * @brief     Implement the interfaces of TcpRecv class.
 *
 * Underlying layer of the fmtpRecvv3 class. It handles communication over
 * TCP connections.
 */


#include "TcpRecv.h"
#ifdef LDM_LOGGING
    #include "log.h"
#endif

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <stdexcept>


TcpRecv::TcpRecv(
        const std::string& tcpaddr,
        unsigned short     tcpport,
        const in_addr_t    iface)
    : tcpAddr(tcpaddr), tcpPort(tcpport), servAddr(), iface{iface}
{
}

/*
 * A default `iface` argument isn't used because statement-expressions are not
 * allowed outside functions nor in template-argument lists
 */
TcpRecv::TcpRecv(
        const std::string& tcpaddr,
        unsigned short     tcpport)
    : TcpRecv(tcpaddr, tcpport, htonl(INADDR_ANY))
{
}


/**
 * Establishes a TCP connection to the sender.
 *
 * @param[in] none
 *
 * @throw std::invalid_argument if `tcpAddr` is invalid.
 * @throw std::system_error     if a TCP connection can't be established.
 */
void TcpRecv::Init()
{
    (void) memset((char *) &servAddr, 0, sizeof(servAddr));
    servAddr.sin_family = AF_INET;
    in_addr_t inAddr = inet_addr(tcpAddr.c_str());
    if ((in_addr_t)-1 == inAddr) {
        const struct hostent* hostEntry = gethostbyname(tcpAddr.c_str());
        if (hostEntry == NULL)
            throw std::invalid_argument(
                    std::string("Invalid TCP-server identifier: \"") +
                    tcpAddr + "\"");
        if (hostEntry->h_addrtype != AF_INET || hostEntry->h_length != 4)
            throw std::invalid_argument( std::string("TCP-server \"") + tcpAddr
                    + "\" doesn't have an IPv4 address");
        inAddr = *(in_addr_t*)hostEntry->h_addr_list[0];
    }
    servAddr.sin_addr.s_addr = inAddr;
    servAddr.sin_port = htons(tcpPort);
    initSocket();
}


bool TcpRecv::recvData(void* header, size_t headLen, char* payload,
                         size_t payLen)
{
    if (header && headLen && !recvall(header, headLen))
        return false;

    if (payload && payLen && !recvall(payload, payLen))
        return false;

    return true;
}


ssize_t TcpRecv::send(const FmtpHeader& header)
{
#if !defined(NDEBUG) && defined(LDM_LOGGING)
	log_debug("Unicasting: flags=%#x, prodindex=%s, seqnum=%s, "
			"payloadlen=%s", ntohs(header.flags),
			std::to_string(ntohl(header.prodindex)).data(),
			std::to_string(ntohl(header.seqnum)).data(),
			std::to_string(ntohs(header.payloadlen)).data());
#endif

    sendall(&header, sizeof(header));
    return (sizeof(header));
}


/**
 * Initializes the TCP connection. Blocks until the connection is established
 * or a severe error occurs.
 *
 * @throws std::system_error  if the socket is not created.
 * @throw  std::system_error  if SO_KEEPALIVE can't be enabled on the socket
 * @throws std::system_error  if connect() returns errors.
 */
void TcpRecv::initSocket()
{
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0)
        throw std::system_error(errno, std::system_category(),
                "TcpRecv::initSocket() error creating socket");

    const int yes = true;
    if (::setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes)) < 0) {
        close(sockfd);
        sockfd = -1;
        throw std::system_error(errno, std::system_category(),
                "TcpRecv::initSocket() Couldn't enable TCP keep-alive "
                "option");
    }

    /*
     * Binding the socket to the VLAN interface isn't necessary to ensure that
     * the unicast connection uses the VLAN *if* the network routing table maps
     * the sending FMTP server's IP address to the VLAN interface. The following
     * assumes this mapping doesn't exist if the interface is explicitly
     * specified.
     */
    if (iface != htonl(INADDR_ANY)) {
        struct sockaddr_in addr = {}; // Zeros content
        addr.sin_family = AF_INET;    // Same as socket domain
        addr.sin_addr.s_addr = iface; // `iface` already in network byte-order
        addr.sin_port = 0;            // Don't care about port number
        if (bind(sockfd, reinterpret_cast<struct sockaddr*>(&addr),
                sizeof(addr))) {
            close(sockfd);
            sockfd = -1;
            throw std::system_error(errno, std::system_category(),
                    "TcpRecv:initSocket() Couldn't bind socket to interface " +
                    addr);
        }
    }

    if (connect(sockfd, (struct sockaddr*)&servAddr, sizeof(servAddr))) {
        close(sockfd);
        sockfd = -1;
        throw std::system_error(errno, std::system_category(),
                "TcpRecv::initSocket() Error connecting to " + servAddr);
    }
#if 0
    std::cerr << "TcpRecv::initSocket(): Socket " + std::to_string(sockfd) +
            " connected to host " + servAddr +  " on interface " + iface << '\n';
#endif
}
