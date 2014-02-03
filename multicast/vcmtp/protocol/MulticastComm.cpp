/*
 * MulticastComm.cpp
 *
 *  Created on: Jun 2, 2011
 *      Author: jie
 */

#include "MulticastComm.h"

MulticastComm::MulticastComm() {
	if ( (sock_fd = socket(PF_INET, SOCK_DGRAM, 0)) < 0) {
		SysError("Cannot create new socket.");
	}

	//int x=fcntl(sock_fd,F_GETFL,0);
	//if (fcntl(sock_fd,F_SETFL,x | O_NONBLOCK) < 0)
	//	SysError("MulticastComm()::Cannot set socket to non-blocking mode.");
}


MulticastComm::~MulticastComm() {
	close(sock_fd);
}



int MulticastComm::JoinGroup(const SA* sa, int sa_len, const char *if_name) {
	switch (sa->sa_family) {
		case AF_INET:
			struct ifreq if_req;
			dst_addr = *sa;
			dst_addr_len = sa_len;

			memcpy(&mreq.imr_multiaddr, &((struct sockaddr_in *) sa)->sin_addr,
					sizeof(struct in_addr));

			if (if_name != NULL) {
				strncpy(if_req.ifr_name, if_name, IFNAMSIZ);
				if (ioctl(sock_fd, SIOCGIFADDR, &if_req) < 0) {
					return (-1);
				}

				memcpy(&mreq.imr_interface, &((struct sockaddr_in *) &if_req.ifr_addr)->sin_addr,
						sizeof(struct in_addr));

				setsockopt(sock_fd, SOL_SOCKET, SO_BINDTODEVICE, if_name, strlen(if_name));
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
	return sendto(sock_fd, buffer->vcmtp_header, buffer->data_len + VCMTP_HLEN,
					flags, &this->dst_addr, sizeof(sockaddr_in));
}


ssize_t MulticastComm::RecvData(void* buff, size_t len, int flags, SA* from, socklen_t* from_len) {
	return recvfrom(sock_fd, buff, len, flags, from, from_len);
}


