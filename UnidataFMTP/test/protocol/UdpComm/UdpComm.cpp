/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: UdpComm.cpp
 *
 * @history:
 *      Created on : Jul 21, 2011
 *      Author     : jie
 *      Modified   : Sep 21, 2014
 *      Author     : Shawn <sc7cq@virginia.edu>
 */

#include "UdpComm.h"


/*****************************************************************************
 * Class Name: UdpComm
 * Function Name: UdpComm()
 *
 * Description: Constructor. Set up a udp socket listening on 0.0.0.0:port
 *
 * Input:  port     port number to listen.
 * Output: none
 ****************************************************************************/
UdpComm::UdpComm(ushort port) {
    // create a UDP datagram socket.
	if ( (sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		SysError("UdpComm::socket() creating error");
	}

    // clear struct server_addr.
	bzero(&server_addr, sizeof(server_addr));
    // set connection type to IPv4
	server_addr.sin_family = AF_INET;
    // listen on incoming address 0.0.0.0
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    // listen on specific port
	server_addr.sin_port = htons(port);

    // bind address and port to socket.
	if ( bind(sock_fd, (SA*)&server_addr, sizeof(server_addr)) < 0)
		SysError("UdpComm::bind() error");
}


/*****************************************************************************
 * Class Name: UdpComm
 * Function Name: ~UdpComm()
 *
 * Description: Default destructor.
 *
 * Input:  none
 * Output: none
 ****************************************************************************/
UdpComm::~UdpComm() {
}


/*****************************************************************************
 * Class Name: UdpComm
 * Function Name: SetSocketBufferSize()
 *
 * Description: Set socket buffer size to size, and allows privileged user to
 * override the size.
 *
 * Input:  size     recv buffer size
 * Output: none
 ****************************************************************************/
void UdpComm::SetSocketBufferSize(size_t size) {
    // set Socket-Level Options (SOL_SOCKET) RECVBUFF size to size,
    // SO_RCVBUFFORCE allows a privileged user to override the recvbuffer size
	if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof(size)) < 0) {
		SysError("Cannot set receive buffer size for raw socket.");
	}
}


/*****************************************************************************
 * Class Name: UdpComm
 * Function Name: SendTo()
 *
 * Description: Send data in buff to a specific dst addr.
 *
 * Input:  *buff       data buffer
 *         len         buffer length
 *         flags       transferring flags
 *         *to_addr    dst address
 *         to_len      address size
 * Output: none
 ****************************************************************************/
ssize_t UdpComm::SendTo(const void* buff, size_t len, int flags, SA* to_addr,
                        socklen_t to_len)
{
	return sendto(sock_fd, buff, len, flags, to_addr, to_len);
}


/*****************************************************************************
 * Class Name: UdpComm
 * Function Name: RecvFrom()
 *
 * Description: Receive data from socket and save it to buff.
 *
 * Input:  *buff         data buffer
 *         len           buffer length
 *         flags         transferring flags
 *         *from_addr    src address
 *         from_len      address size
 * Output: none
 ****************************************************************************/
ssize_t UdpComm::RecvFrom(void* buff, size_t len, int flags, SA* from_addr,
                          socklen_t* from_len)
{
	return recvfrom(sock_fd, buff, len, flags, from_addr, from_len);
}

void SysError(string s) {
	perror(s.c_str());
	exit(-1);
}
