/*
 * InetComm.cpp
 *
 *  Created on: Jul 21, 2011
 *      Author: jie
 */

#include "InetComm.h"

InetComm::InetComm() {
	// TODO Auto-generated constructor stub

}

InetComm::~InetComm() {
	// TODO Auto-generated destructor stub
}

void InetComm::SetBufferSize(size_t buf_size) {
	int size = buf_size;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof(size)) < 0) {
		SysError("Cannot set receive buffer size for raw socket.");
	}
}

int InetComm::GetSocket() {
	return sock_fd;
}

void SysError(string s) {
	perror(s.c_str());
	exit(-1);
}
