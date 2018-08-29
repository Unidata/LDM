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
 * @brief     Define the interfaces and structures of sender side UDP layer
 *            abstracted funtions.
 *
 * The UdpSend class includes a set of transmission functions, which are
 * basically the encapsulation of udp system calls themselves. This abstracted
 * new layer acts as the sender side transmission library.
 */


#ifndef FMTP_SENDER_UDPSOCKET_H_
#define FMTP_SENDER_UDPSOCKET_H_


#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string>


class UdpSend {
public:
    UdpSend(const std::string& recvaddr, const unsigned short recvport,
            const unsigned char ttl, const std::string& ifAddr);
    ~UdpSend();

    void Init();  /*!< start point which caller should call */
    /**
     * SendData() sends the packet content separated in two different physical
     * locations, which is put together into a io vector structure, to the
     * destination identified by a socket file descriptor.
     */
    ssize_t SendData(void* header, size_t headerLen, void* data,
                     size_t dataLen);
    /**
     * SendTo() sends a piece of message to a destination identified by a
     * socket file descriptor.
     */
    ssize_t SendTo(const void* buff, size_t len);
    /**
     * Gather send a FMTP packet.
     *
     * @param[in] iovec  First I/O vector.
     * @param[in] nvec   Number of I/O vectors.
     */
    ssize_t SendTo(struct iovec* const iovec, const int nvec);

private:
    int                   sock_fd;
    struct sockaddr_in    recv_addr;
    const std::string     recvAddr;
    const unsigned short  recvPort;
    const unsigned short  ttl;
    const std::string     ifAddr;
};


#endif /* FMTP_SENDER_UDPSOCKET_H_ */
