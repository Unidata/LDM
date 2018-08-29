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

MulticastComm::MulticastComm() {
    // PF_INET for IPv4, SOCK_DGRAM for connectionless udp socket.
	if ( (sock_fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0 ) {
		SysError("Cannot create new socket.");
	}
}


MulticastComm::~MulticastComm() {
	close(sock_fd);
}


/**
 * Joins an Internet multicast group.
 *
 * @param  sa                     The socket address.
 * @param  sa_len                 The size of \c sa in bytes.
 * @param  if_name                The name of the interface to use or NULL.
 * @return 0                      Success.
 * @throws std::invalid_argument  if the address family of \c sa isn't AF_INET.
 * @throws std::runtime_error     if the IP address of the PA interface couldn't
 *                                be obtained. (The PA address seems to be
 *                                specific to Linux and might cause problems.)
 * @throws std::runtime_error     if the socket couldn't be bound to \c if_name.
 * @throws std::runtime_error     if the socket couldn't be bound to \c sa.
 * @throws std::runtime_error     if multicast group \c sa couldn't be added to
 *                                the socket.
 */
int MulticastComm::JoinGroup(const SA* sa, int sa_len, const char *if_name) {
    switch (sa->sa_family)
    {
        case AF_INET:
            struct ifreq if_req;
            dst_addr = *sa;
            dst_addr_len = sa_len;

            memcpy(&mreq.imr_multiaddr, &((struct sockaddr_in *) sa)->sin_addr,
                    sizeof(struct in_addr));

            if (if_name != NULL) {
                strncpy(if_req.ifr_name, if_name, IFNAMSIZ);
                /*
                 * NB: SIOCGIFADDR is non-standard.
                 */
                if (ioctl(sock_fd, SIOCGIFADDR, &if_req) < 0) {
                    throw std::runtime_error(
                            std::string("Couldn't obtain PA address: ") +
                            strerror(errno));
                }

                memcpy(&mreq.imr_interface,
                        &((struct sockaddr_in *) &if_req.ifr_addr)->sin_addr,
                        sizeof(struct in_addr));

                if (setsockopt(sock_fd, SOL_SOCKET, SO_BINDTODEVICE, if_name,
                        strlen(if_name))) {
                    throw std::runtime_error(std::string("Couldn't bind socket to "
                            "interface \"") + if_name + "\": " + strerror(errno));
                }
            }
            else {
                mreq.imr_interface.s_addr = htonl(INADDR_ANY);
            }

            if (bind(sock_fd, &dst_addr, dst_addr_len)) {
                throw std::runtime_error(std::string("Couldn't bind socket "
                        "to IP address: ") + strerror(errno));
            }

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


int MulticastComm::LeaveGroup() {
	return setsockopt(sock_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
}


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


ssize_t MulticastComm::SendData(const void* buff, size_t len, int flags, void* dst_addr) {
	return sendto(sock_fd, buff, len, flags, &this->dst_addr, sizeof(sockaddr_in));
}

ssize_t MulticastComm::SendPacket(PacketBuffer* buffer, int flags, void* dst_addr) {
	return sendto(sock_fd, buffer->fmtp_header, buffer->data_len + FMTP_HLEN,
					flags, &this->dst_addr, sizeof(sockaddr_in));
}


ssize_t MulticastComm::RecvData(void* buff, size_t len, int flags, SA* from, socklen_t* from_len) {
	return recvfrom(sock_fd, buff, len, flags, from, from_len);
}


