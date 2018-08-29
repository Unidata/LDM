/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: MulticastComm.cpp
 *
 * @history:
 *      Created on : Jul 21, 2011
 *      Author     : jie
 *      Modified   : Sep 13, 2014
 *      Author     : Shawn <sc7cq@virginia.edu>
 */

#include "MulticastComm.h"
#include <errno.h>
#include <exception>
#include <stdexcept>
#include <string>
#include <string.h>
#include <sys/socket.h>


/*****************************************************************************
 * Class Name: MulticastComm
 * Function Name: MulticastComm()
 *
 * Description: Constructing a UDP socket.
 *
 * Input:  none
 * Output: none
 ****************************************************************************/
MulticastComm::MulticastComm() {
    // PF_INET for IPv4, SOCK_DGRAM for connectionless udp socket.
	if ( (sock_fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0 ) {
		SysError("Cannot create new socket.");
	}
}


/*****************************************************************************
 * Class Name: MulticastComm
 * Function Name: ~MulticastComm()
 *
 * Description: Destructing a UDP socket.
 *
 * Input:  none
 * Output: none
 ****************************************************************************/
MulticastComm::~MulticastComm() {
	close(sock_fd);
}


/*****************************************************************************
 * Class Name: MulticastComm
 * Function Name: JoinGroup()
 *
 * Description: Joins an Internet multicast group, setting up the receiving
 * socket. Bind the socket to specified local interface and multicast group.
 *
 * Input:  *sa          sockaddr struct, contains address and port number.
 *         sa_len       length of sockaddr struct.
 *         *if_name     interface name (e.g. eth0)
 * Output: return       return value 0 means successful.
 ****************************************************************************/
int MulticastComm::JoinGroup(const SA* sa, int sa_len, const char *if_name) {
    // only supports AF_INET (IPv4) for now
    switch (sa->sa_family) {
    case AF_INET:
        struct ifreq if_req;
        dst_addr = *sa;
        dst_addr_len = sa_len;

        // converting sa of sockaddr type to sockaddr_in type. And copy
        // sa->sin_addr to mreq.imr_multiaddr as the multicast address.
        memcpy(&mreq.imr_multiaddr, &((struct sockaddr_in *) sa)->sin_addr,
                sizeof(struct in_addr));

        if (if_name != NULL) {
            // if interface name is not NULL, copy it into ifreq struct
            strncpy(if_req.ifr_name, if_name, IFNAMSIZ);
            // use ioctl() to get address of interface *if_name, store the
            // address in if_req.ifr_addr (ifr_addr is a sockaddr type struct)
            if (ioctl(sock_fd, SIOCGIFADDR, &if_req) < 0) {
                throw std::runtime_error(
                        std::string("Couldn't obtain PA address: ") +
                        strerror(errno));
            }
            // copy address of interface *if_name to the imr_interface field
            // of mreq
            memcpy(&mreq.imr_interface,
                    &((struct sockaddr_in *) &if_req.ifr_addr)->sin_addr,
                    sizeof(struct in_addr));
            // bind socket (described by sock_fd) to interface *if_name,
            // only listen to packets coming into interface *if_name
            if (setsockopt(sock_fd, SOL_SOCKET, SO_BINDTODEVICE, if_name,
                    strlen(if_name))) {
                throw std::runtime_error(std::string("Couldn't bind socket to "
                        "interface \"") + if_name + "\": " + strerror(errno));
            }
        }
        else {
            // if no interface specified, listen on all interfaces
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        }

        // bind socket to the multicast ip address and only listen to the
        // multicast ip address
        if (bind(sock_fd, &dst_addr, dst_addr_len)) {
            throw std::runtime_error(std::string("Couldn't bind socket "
                    "to IP address: ") + strerror(errno));
        }

        // add local interface into the multicast group (specified by mreq)
        if (setsockopt(sock_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq,
                sizeof(mreq))) {
            throw std::runtime_error(std::string("Couldn't add multicast "
                    "group to socket: ") + strerror(errno));
        }

        return 0;

    default:
        throw std::invalid_argument(
                std::string("Can only join AF_INET multicast groups"));
    }
}


/*****************************************************************************
 * Class Name: MulticastComm
 * Function Name: JoinGroup()
 *
 * Description: Joins an Internet multicast group, setting up the receiving
 * socket. Bind the socket to specified local interface and multicast group.
 * Same as another JoinGroup(), but use if_index instead of if_name.
 *
 * Input:  *sa          sockaddr struct, contains address and port number.
 *         sa_len       length of sockaddr struct.
 *         if_index     index of the interface
 * Output: return       return value 0 means successful.
 ****************************************************************************/
int MulticastComm::JoinGroup(const SA* sa, int sa_len, u_int if_index) {
	switch (sa->sa_family) {
			case AF_INET:
				struct ifreq if_req;
				dst_addr = *sa;
				dst_addr_len = sa_len;

				memcpy(&mreq.imr_multiaddr, &((struct sockaddr_in *) sa)->sin_addr,
						sizeof(struct in_addr));

				if (if_index > 0) {
					if (if_indextoname(if_index, if_req.ifr_name) == NULL) {
						return -1;
					}

					if (ioctl(sock_fd, SIOCGIFADDR, &if_req) < 0) {
						return (-1);
					}

					memcpy(&mreq.imr_interface, &((struct sockaddr_in *) &if_req.ifr_addr)->sin_addr,
							sizeof(struct in_addr));
				}
				else {
					mreq.imr_interface.s_addr = htonl(INADDR_ANY);
				}

				bind(sock_fd, &dst_addr, dst_addr_len);

				return (setsockopt(sock_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)));

			default:
				return -1;
		}
}


/*****************************************************************************
 * Class Name: MulticastComm
 * Function Name: LeaveGroup()
 *
 * Description: Drop IP address of the interface out of the multicast group.
 *
 * Input:  none
 * Output: return       success number or -1
 ****************************************************************************/
int MulticastComm::LeaveGroup() {
	return setsockopt(sock_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
}


/*****************************************************************************
 * Class Name: MulticastComm
 * Function Name: SetLoopBack()
 *
 * Description: Enable or disable loopback in multicasting.
 *
 * Input:  onoff        accept bool value on (true) / off (false)
 * Output: return       success number or -1
 ****************************************************************************/
int MulticastComm::SetLoopBack(int onoff) {
	switch (dst_addr.sa_family) {
        case AF_INET:
            int flag;
            flag = onoff;
            return setsockopt(sock_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &flag, sizeof(flag));

        default:
            return -1;
	}
}


/*****************************************************************************
 * Class Name: MulticastComm
 * Function Name: SendData()
 *
 * Description: Send data inside a buffer to the multicast group.
 *
 * Input:  *buff        pointer to the data buffer
 *         len          data buffer size
 *         flags        sendto() socket flags
 *         *dst_addr    MulticastComm has its own dst_addr, use NULL here
 * Output: return       success number or -1
 ****************************************************************************/
ssize_t MulticastComm::SendData(const void* buff, size_t len, int flags, void* dst_addr) {
	return sendto(sock_fd, buff, len, flags, &this->dst_addr, sizeof(sockaddr_in));
}

ssize_t MulticastComm::SendData(
        const void*  header,
        const size_t headerLen,
        const void*  data,
        const size_t dataLen)
{
    struct iovec iov[2];

    iov[0].iov_base = (void*)header;    // safe cast because reading data
    iov[0].iov_len  = headerLen;
    iov[1].iov_base = (void*)data;      // safe cast because reading data
    iov[1].iov_len  = dataLen;

    return writev(sock_fd, iov, 2);
}


/*****************************************************************************
 * Class Name: MulticastComm
 * Function Name: SendPacket()
 *
 * Description: Send whole fmtp packet inside buffer to the multicast group.
 *
 * Input:  *buffer      pointer to the FMTP packet data buffer
 *         flags        sendto() socket flags
 *         *dst_addr    MulticastComm has its own dst_addr, use NULL here
 * Output: return       success number or -1
 ****************************************************************************/
ssize_t MulticastComm::SendPacket(PacketBuffer* buffer, int flags, void* dst_addr) {
	return sendto(sock_fd, buffer->fmtp_header, buffer->data_len + FMTP_HLEN,
					flags, &this->dst_addr, sizeof(sockaddr_in));
}


/*****************************************************************************
 * Class Name: MulticastComm
 * Function Name: RecvData()
 *
 * Description: Receive data from multicast group.
 *
 * Input:  *buff        pointer to the receiving buffer
 *         len          buffer size
 *         flags        recvfrom() socket flags
 *         *from        IP multicast group address
 *         from_len     IP multicast group address size
 * Output: return       success number or -1
 ****************************************************************************/
ssize_t MulticastComm::RecvData(void* buff, size_t len, int flags, SA* from, socklen_t* from_len) {
	return recvfrom(sock_fd, buff, len, flags, from, from_len);
}


