/*
 * TcpClient.cpp
 *
 *  Created on: Sep 15, 2011
 *      Author: jie
 */

#include "TcpClient.h"

#include <errno.h>
#include <string>
#include <stdexcept>

/**
 * Constructs a client-side TCP connection to the VCMTP Sender for the
 * Retransmission Requester.
 *
 * @param[in] serv_addr             Address of multicast sender. Can be hostname
 *                                  or dotted-quad IPv4 address
 * @param[in] port                  Port number on @code{serv_addr} to which to
 *                                  connect
 * @throws    std::invalid_argument Hostname @code{serv_addr} doesn't resolve
 *                                  to IP address
 */
TcpClient::TcpClient(const string serv_addr, const int port)
:   server_port(port),
    sock_fd(-1)
{
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);

	in_addr_t temp_addr = inet_addr(serv_addr.c_str());
	if (temp_addr == -1) {
		struct hostent* ptrhost = gethostbyname(serv_addr.c_str());
		if (ptrhost == NULL) {
		    throw std::invalid_argument(
		            "serv_addr=\"" + serv_addr + "\"");
		}
                temp_addr = ((in_addr*)ptrhost->h_addr_list[0])->s_addr;
	}
	server_addr.sin_addr.s_addr = temp_addr;
}

TcpClient::~TcpClient() {
	close(sock_fd);
}


int TcpClient::Connect() {
	if (sock_fd > 0)
		close(sock_fd);

	if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		SysError("TcpClient::TcpClient()::socket() error");
	}

	int optval = 1;
	setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval) );

	int res;
	while ((res = connect(sock_fd, (sockaddr *) &server_addr, sizeof(server_addr))) < 0) {
		cout << "TcpClient::Connect()::connect() error. Retry in 10 seconds..." << endl;
		sleep(10);
	}

	cout << "TCP server connected." << endl;
	return res;
}


int TcpClient::GetSocket() {
	return sock_fd;
}


int TcpClient::Send(const void* data, size_t length) {
	return send(sock_fd, data, length, 0);
}

// This receive function explicitly handles the zero length case
// to avoid the undefined hang problem of the Linux system call
int TcpClient::Receive(void* buffer, size_t length) {
	if (length == 0)
		return 0;
	else
		return recv(sock_fd, buffer, length, MSG_WAITALL);

//	size_t remained_size = length;
//	int recv_bytes;
//	while (remained_size > 0) {
//		if ((recv_bytes = recv(sock_fd, buffer, remained_size, 0)) < 0) {
//			SysError("TcpClient::Receive()::recv() error");
//		}
//		remained_size -= recv_bytes;
//	}
//
//	return length;
}

void TcpClient::SysError(const char* info) {
	perror(info);
	exit(-1);
}
