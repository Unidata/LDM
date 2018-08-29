/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: TcpClient.cpp
 *
 * @history:
 *      Created  : Sep 15, 2011
 *      Author   : jie
 *      Modified : Aug 31, 2014
 *      Author   : Shawn <sc7cq@virginia.edu>
 */

#include "TcpClient.h"

#include <errno.h>
#include <string>
#include <stdexcept>


/*****************************************************************************
 * Class Name: TcpClient
 * Function Name: TcpClient()
 *
 * Description: Constructing a client-side TCP structure with address and port
 * number and sin_family set.
 *
 * Input:  serv_addr    Address of multicast sender. Can be hostname or dotted
 *                      -decimal IPv4 address.
 *         port         Port number of server which to connect with.
 * Output: none
 ****************************************************************************/
TcpClient::TcpClient(const string serv_addr, const int port)
:   server_port(port),
    sock_fd(-1)
{
    // clear server_addr structure. And be aware that serv_addr is a string
    // type variant passed in when constructing a new object. server_addr is
    // the sockaddr_in type structure containing not only a server address.
	bzero(&server_addr, sizeof(server_addr));

    // sin_family should always be set to AF_INET for network sockets.
	server_addr.sin_family = AF_INET;

    // convert a port number in host order to network order
	server_addr.sin_port = htons(server_port);

    // c_str() converts a string into a constant char array and returns the
    // base address. inet_addr() converts the Internet host address from
    // numbers-and-dots notation into binary data in network byte order.
	in_addr_t temp_addr = inet_addr(serv_addr.c_str());

    // If inet_addr() returns -1, the host address is not ip but hostname.
	if (temp_addr == -1) {
        // lookup the hostname for ip address and save the returning results
        // in a hostent structure.
		struct hostent* ptrhost = gethostbyname(serv_addr.c_str());

		if (ptrhost == NULL) {
            // throws an exception. serv_addr is not a valid hostname and
            // doesn't resolve to an IP address.
		    throw std::invalid_argument(
		            "serv_addr=\"" + serv_addr + "\"");
		}

        // take the first ip address resolved in hostent h_addr_list as its
        // default ip address.
        temp_addr = ((in_addr*)ptrhost->h_addr_list[0])->s_addr;
        //temp_addr = ((in_addr*)ptrhost->h_addr;
	}

    // set server address with the resolved ip address.
	server_addr.sin_addr.s_addr = temp_addr;
}


/*****************************************************************************
 * Class Name: TcpClient
 * Function Name: ~TcpClient()
 *
 * Description: Deconstructing the TcpClient object and remove existing TCP
 * connection socket.
 *
 * Input:  none
 * Output: none
 ****************************************************************************/
TcpClient::~TcpClient() {
	close(sock_fd);
}


/*****************************************************************************
 * Class Name: TcpClient
 * Function Name: Connect()
 *
 * Description: Connecting to TCP server socket (e.g. FMTP sender) to
 * establish a connection.
 *
 * Input:  none
 * Output: none
 ****************************************************************************/
int TcpClient::Connect() {
    // Upon construction finished, the new TcpClient object should contain an
    // initialized sock_fd = -1. If sock_fd is a value > 0, there is probably
    // an existing socket connection occupying this descriptor, should remove
    // the connection first.
	if (sock_fd > 0)
		close(sock_fd);

    // Use server_addr pre-configured by the constructor to create a socket.
	if ((sock_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		//SysError("TcpClient::TcpClient()::socket() error");
		SysError("TcpClient::Connect() creating socket failed");
	}

	int optval = 1;
    // setsockopt() manipulates options for the socket referred to by the file
    // descriptor sock_fd. SOL_SOCKET specifies the level to manipulate. The
    // option name is SO_REUSEADDR and its value should be set to optval. By
    // setting SO_REUSEADDR, more than 1 instance is enabled to bind to the
    // same port, but different local address.
	setsockopt( sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval) );

	int res;
    // Connect to server-side socket to establish retransmission connection.
    // Keep trying to connect in every 10 seconds if failed.
	while ( ( res = connect(sock_fd,
                            (sockaddr *) &server_addr,
                            sizeof(server_addr)) ) < 0 ) {
		cout << "TcpClient::Connect() connecting to sender failed. Retry in 10 seconds..." << endl;
		sleep(10);
	}

	//cout << "TCP server connected." << endl;
	cout << "Successfully connected to TCP server." << endl;
	return res;
}


/*****************************************************************************
 * Class Name: TcpClient
 * Function Name: GetSocket()
 *
 * Description: Getting sock_fd
 *
 * Input:  none
 * Output: none
 ****************************************************************************/
int TcpClient::GetSocket() {
	return sock_fd;
}


/*****************************************************************************
 * Class Name: TcpClient
 * Function Name: Send()
 *
 * Description: Send data to the other side of the connection (server-side)
 *
 * Input:  *data        Pointer to the message content to be sent
 *         length       Length of the message content
 * Output: none
 ****************************************************************************/
int TcpClient::Send(const void* data, size_t length) {
	return send(sock_fd, data, length, 0);
}


/*****************************************************************************
 * Class Name: TcpClient
 * Function Name: Receive()
 *
 * Description: Send data to the other side of the connection (server-side)
 *
 * Input:  *buffer      The buffer where to store received data
 *         length       Length of received data
 * Output: none
 *
 * Notes: This receive function explicitly handles the zero length case to
 * avoid the undefined hang problem of the Linux system call.
 ****************************************************************************/
int TcpClient::Receive(void* buffer, size_t length) {
	if (length == 0)
		return 0;
	else
		return recv(sock_fd, buffer, length, MSG_WAITALL);

// commented out by Jie
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

// throw a System Error message and print on stderr.
void TcpClient::SysError(const char* info) {
	perror(info);
	exit(-1);
}
