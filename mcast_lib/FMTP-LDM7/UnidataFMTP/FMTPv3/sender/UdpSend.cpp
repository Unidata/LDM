/**
 * Copyright (C) 2021 University of Virginia. All rights reserved.
 *
 * @file      UdpSend.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @author    Steven R. Emmerson <emmerson@ucar.edu>
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

#include <cassert>
#include <cstdlib>
#include <cstring>
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

const char UdpSend::BlackHat::ENV_NAME[] = "FMTP_INVALID_PACKET_RATIO";

UdpSend::BlackHat::BlackHat(UdpSend& udpSend)
    : udpSend(udpSend)
    , validPacketIndex(-1)
    , invalidRatio(0)
    , indicator(0)
{
    const auto ratioStr = ::getenv(ENV_NAME);
    if (ratioStr == nullptr) {
#       ifdef LDM_LOGGING
            log_notice("Environment variable %s doesn't exist", ENV_NAME);
#       endif
    }
    else {
        invalidRatio = std::stof(ratioStr, nullptr);
        if (invalidRatio < 0)
            throw std::invalid_argument(std::string(
                    "UdpSend::BlackHat::BlackHat(): Invalid ") + ENV_NAME +
                    "value: " + ratioStr);

#       ifdef LDM_LOGGING
            log_notice("Invalid packet ratio set to %g from environment "
                    "variable %s", invalidRatio, ENV_NAME);
#       endif
    }
}

void UdpSend::BlackHat::maybeSend(const FmtpHeader& header)
{
    ++validPacketIndex;
    if (validPacketIndex != udpSend.packetIndex)
        throw std::logic_error("UdpSend::BlackHat::maybeSend(): Valid packet "
                "index didn't increase by 1");

    indicator += invalidRatio;
    if (indicator >= 1) {
        udpSend.packet.bytes[udpSend.msgLen] ^= 1; // Flip one bit in MAC
        for (; indicator >= 1; indicator -= 1)
            udpSend.write(header);
        udpSend.packet.bytes[udpSend.msgLen] ^= 1; // Restore MAC bit
    }
}


/**
 * Constructor, set the IP address and port of the receiver, TTL, and default
 * multicast egress interface.
 *
 * @param[in] recvAddr     IP address of the receiver.
 * @param[in] recvport     Port number of the receiver.
 * @param[in] ttl          Time to live.
 * @param[in] ifAddr       IP of interface for multicast egress.
 */
UdpSend::UdpSend(const std::string&   recvaddr,
                 const unsigned short recvport,
                 const unsigned char  ttl,
                 const std::string&   ifAddr)
    : recvAddr(recvaddr),
      recvPort(recvport),
      ttl(ttl),
      ifAddr(ifAddr),
      sock_fd(-1),
      recv_addr(),
      packetIndex(0),
      signer{},
      msgLen{0},
      MAC_LEN{signer.getSize()},
      sendBefore(false),
      blackHat(*this),
      maxPayload{MAX_FMTP_PACKET - FMTP_HEADER_LEN - MAC_LEN}
{}


/**
 * Destroys this instance.
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

const std::string UdpSend::getMacKey() const noexcept
{
    return signer.getKey();
}

void UdpSend::write(const FmtpHeader& header)
{
    #ifdef LDM_LOGGING
        log_debug("Multicasting: flags=%#x, prodindex=%s, seqnum=%s, "
                "payloadlen=%s, MAC_LEN=%u", header.flags,
                std::to_string(header.prodindex).data(),
                std::to_string(header.seqnum).data(),
                std::to_string(header.payloadlen).data(),
                MAC_LEN);
    #endif

    const size_t totLen = msgLen + MAC_LEN;
    const auto   nbytes = ::write(sock_fd, packet.bytes, totLen);
    if (nbytes != totLen)
    	throw std::system_error(errno, std::system_category(),
                "UdpSend::send(): write() failure: nbytes=" +
                std::to_string(nbytes));
}

void UdpSend::send(const FmtpHeader& header,
                   const void*       payload)
{
    if (header.payloadlen && payload == nullptr)
        throw std::invalid_argument(
                "Payload length is positive but payload is null");
    if (header.payloadlen > maxPayload)
        throw std::invalid_argument("FMTP payload is too large: nbytes=" +
                std::to_string(header.payloadlen));

    packet.header.flags      = htons(header.flags);
    packet.header.payloadlen = htons(header.payloadlen);
    packet.header.prodindex  = htonl(header.prodindex);
    packet.header.seqnum     = htonl(header.seqnum);

    if (payload)
        ::memcpy(packet.payload, payload, header.payloadlen);

    msgLen = FMTP_HEADER_LEN + header.payloadlen;
    const auto macLen = signer.getMac(packet.bytes, msgLen, packet.bytes+msgLen,
            MAC_LEN);
    assert(macLen == MAC_LEN);

    if (MAC_LEN && sendBefore)
        blackHat.maybeSend(header);
    write(header);
    if (MAC_LEN && !sendBefore)
        blackHat.maybeSend(header);

    sendBefore = !sendBefore;
    ++packetIndex;
}
