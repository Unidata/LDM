/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      UdpSend.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Oct 23, 2014
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
 * @brief     Implement the interfaces and structures of sender side UDP layer
 *            abstracted funtions.
 *
 * The UdpSend class includes a set of transmission functions, which are
 * basically the encapsulation of udp system calls themselves. This abstracted
 * new layer acts as the sender side transmission library.
 */


#include "UdpSend.h"
#ifdef LDM_LOGGING
    #include "log.h"
#endif

#include <errno.h>
#include <netinet/in.h>
#include <string>
#include <string.h>
#include <stdexcept>
#include <stdlib.h>
#include <system_error>


#ifndef NULL
    #define NULL 0
#endif


/**
 * Constructor, set the IP address and port of the receiver, TTL and default
 * multicast ingress interface.
 *
 * @param[in] recvAddr     IP address of the receiver.
 * @param[in] recvport     Port number of the receiver.
 * @param[in] ttl          Time to live.
 * @param[in] ifAddr       IP of interface to listen for multicast.
 */
UdpSend::UdpSend(const std::string& recvaddr, const unsigned short recvport,
                 const unsigned char ttl, const std::string& ifAddr)
    : recvAddr(recvaddr), recvPort(recvport), ttl(ttl), ifAddr(ifAddr),
      sock_fd(-1), recv_addr(), hmacImpl(), netHead{}, iov{},
      macLen{HmacImpl::isDisabled() ? 0 : static_cast<unsigned>(sizeof(mac))}
{
    iov[0].iov_base = &netHead;
    iov[0].iov_len  = FMTP_HEADER_LEN;
    iov[2].iov_base = mac;
    iov[2].iov_len  = macLen;
}


/**
 * Destroys elements within the udpsend entity which need to be deleted.
 *
 * @param[in] none
 */
UdpSend::~UdpSend()
{}


/**
 * Initializer. It creates a new UDP socket and sets the address and port from
 * the pre-set parameters. Also it connects to the created socket.
 *
 * @throws std::runtime_error  if socket creation fails.
 * @throws std::runtime_error  if connecting to socket fails.
 * @throws std::runtime_error  if setting TTL fails.
 */
void UdpSend::Init()
{
    int newttl = ttl;
    struct in_addr interfaceIP;
    /** create a UDP datagram socket. */
    if((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        throw std::system_error(errno, std::system_category(),
                "UdpSend::Init() Couldn't create UDP socket");
    }

    /** clear struct recv_addr. */
    (void) memset(&recv_addr, 0, sizeof(recv_addr));
    /** set connection type to IPv4 */
    recv_addr.sin_family = AF_INET;
    /** set the address to the receiver address passed to the constructor */
    recv_addr.sin_addr.s_addr = inet_addr(recvAddr.c_str());
    /** set the port number to the port number passed to the constructor */
    recv_addr.sin_port = htons(recvPort);

    int reuseaddr = true;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr,
                   sizeof(reuseaddr)) < 0) {
        throw std::system_error(errno, std::system_category(),
                "UdpSend::Init() Couldn't enable IP address reuse");
    }

#ifdef SO_REUSEPORT
    int reuseport = true;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEPORT, &reuseport,
                   sizeof(reuseport)) < 0) {
        throw std::system_error(errno, std::system_category(),
                "UdpSend::Init() Couldn't enable port number reuse");
    }
#endif

    if (setsockopt(sock_fd, IPPROTO_IP, IP_MULTICAST_TTL, &newttl,
                sizeof(newttl)) < 0) {
        throw std::system_error(errno, std::system_category(),
                "UdpSend::Init() Couldn't set UDP socket time-to-live "
                "option to " + std::to_string(ttl));
    }

    interfaceIP.s_addr = inet_addr(ifAddr.c_str());
    if (setsockopt(sock_fd, IPPROTO_IP, IP_MULTICAST_IF, &interfaceIP,
                   sizeof(interfaceIP)) < 0) {
        throw std::system_error(errno, std::system_category(),
                std::string("UdpSend::Init() Couldn't set UDP socket multicast "
                        "interface to \"") + ifAddr.c_str() + "\"");
    }

    if (::connect(sock_fd, reinterpret_cast<struct sockaddr*>(&recv_addr),
    		sizeof(recv_addr)))
        throw std::system_error(errno, std::system_category(),
        		"Couldn't connect() socket " +
                std::to_string(sock_fd) + " to " + recvAddr + ":" +
				std::to_string(recvPort));

}

const std::string& UdpSend::getMacKey() const noexcept
{
    return hmacImpl.getKey();
}

void UdpSend::send(const FmtpHeader& header,
                   const void*       payload)
{
    if (header.payloadlen && payload == nullptr)
        throw std::logic_error("Inconsistent header and payload");

    if (macLen)
        hmacImpl.getMac(header, payload, mac);

    netHead.flags      = htons(header.flags);
    netHead.payloadlen = htons(header.payloadlen);
    netHead.prodindex  = htonl(header.prodindex);
    netHead.seqnum     = htonl(header.seqnum);

    iov[1].iov_base = const_cast<void*>(payload);
    iov[1].iov_len  = header.payloadlen;

    #ifdef LDM_LOGGING
        log_debug("Multicasting: flags=%#x, prodindex=%s, seqnum=%s, "
                "payloadlen=%s, payload=%p, mac=%s", header.flags,
                std::to_string(header.prodindex).data(),
                std::to_string(header.seqnum).data(),
                std::to_string(header.payloadlen).data(),
                payload,
                HmacImpl::to_string(mac).c_str());
    #endif

    const auto nbytes = ::writev(sock_fd, iov, sizeof(iov)/sizeof(iov[0]));
    if (nbytes != sizeof(netHead) + header.payloadlen + macLen)
    	throw std::system_error(errno, std::system_category(),
    			"UdpSend::send(): writev() failure: nbytes=" +
				std::to_string(nbytes));
}
