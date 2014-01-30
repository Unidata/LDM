/*
 * UDPComm.cpp
 *
 *  Created on: Jul 4, 2011
 *      Author: jie
 */

#include "UdpComm.h"

UdpComm::UdpComm(ushort port) {
	if ( (sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		SysError("UdpComm::socket() error");
	}

	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(port);

	if ( bind(sock_fd, (SA*)&server_addr, sizeof(server_addr)) < 0)
		SysError("UdpComm::bind() error");

}


UdpComm::~UdpComm() {

}


void UdpComm::SetSocketBufferSize(size_t size) {
	if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof(size)) < 0) {
		SysError("Cannot set receive buffer size for raw socket.");
	}
}


ssize_t UdpComm::SendTo(const void* buff, size_t len, int flags, SA* to_addr,  socklen_t to_len) {
	return sendto(sock_fd, buff, len, flags, to_addr, to_len);
}


ssize_t UdpComm::RecvFrom(void* buff, size_t len, int flags, SA* from, socklen_t* from_len) {
	return recvfrom(sock_fd, buff, len, flags, from, from_len);
}
