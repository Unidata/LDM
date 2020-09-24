/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      TcpRecv.h
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
 * @brief     Define the interfaces of TcpRecv class.
 *
 * Underlying layer of the fmtpRecvv3 class. It handles communication over
 * TCP connections.
 */


#ifndef FMTP_RECEIVER_TCPRECV_H_
#define FMTP_RECEIVER_TCPRECV_H_


#include "TcpBase.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <mutex>
#include <string>
#include <sys/types.h>


class TcpRecv: public TcpBase
{
public:
    /**
     * Constructs.
     * @param[in] tcpaddr  The address of the TCP server: either an IPv4
     *                     address in dotted-decimal format or an Internet
     *                     host name.
     * @param[in] tcpport  The port number of the TCP connection in host
     *                     byte-order.
     * @param[in] iface    IPv4 address of local interface to use in network
     *                     byte-order
     */
    TcpRecv(const std::string& tcpaddr,
            unsigned short     tcpport,
            const in_addr_t    iface);

    /**
     * Constructs. The IPv4 address of the local interface to use will be
     * `htonl(INADDR_ANY)`.
     * @param[in] tcpaddr  The address of the TCP server: either an IPv4
     *                     address in dotted-decimal format or an Internet
     *                     host name.
     * @param[in] tcpport  The port number of the TCP connection in host
     *                     byte-order.
     * @param[in] iface    IPv4 address of local interface to use in network
     *                     byte-order. Default is `htonl(INADDR_ANY)`.
     */
    TcpRecv(const std::string& tcpaddr,
            unsigned short     tcpport);

    void Init();  /*!< the start point which upper layer should call */

    /**
     * Receives a header and a payload on the TCP connection. Blocks until the
     * packet is received or a severe error occurs. Re-establishes the TCP
     * connection if necessary.
     *
     * @param[in] header         Header.
     * @param[in] headLen        Length of the header in bytes.
     * @param[in] payload        Payload.
     * @param[in] payLen         Length of the payload in bytes.
     * @retval    `true`         Success
     * @retval    `false`        EOF encountered
     * @throw std::system_error  I/O error
     */
    bool recvData(void* header, size_t headLen, char* payload,
                     size_t payLen);
    /**
     * Sends a header and a payload on the TCP connection. Blocks until the packet
     * is sent or a severe error occurs. Re-establishes the TCP connection if
     * necessary.
     *
     * @param[in] header   Header.
     * @param[in] headLen  Length of the header in bytes.
     * @param[in] payload  Payload.
     * @param[in] payLen   Length of the payload in bytes.
     * @retval    -1       O/S failure.
     * @return             Number of bytes sent.
     */
    ssize_t sendData(void* header, size_t headLen, char* payload,
                     size_t payLen);

private:
    /**
     * Initializes the TCP connection.
     *
     * @throws std::system_error  if a system error occurs.
     */
    void initSocket();

    struct sockaddr_in      servAddr;
    std::string             tcpAddr;  ///< a copy of the passed-in tcpAddr
    unsigned short          tcpPort;  ///< a copy of the passed-in tcpPort
    /// Local interface to use in network byte-order
    in_addr_t               iface;
};


inline std::string operator+(const std::string& lhs, const struct sockaddr_in& rhs)
{
    return lhs + inet_ntoa(rhs.sin_addr) + ":" +
            std::to_string(static_cast<long long>(ntohs(rhs.sin_port)));
}

inline std::string operator+(const std::string& lhs, const in_addr_t rhs)
{
    struct in_addr inAddr = {rhs};
    return lhs + inet_ntoa(inAddr);
}


#endif /* FMTP_RECEIVER_TCPRECV_H_ */
