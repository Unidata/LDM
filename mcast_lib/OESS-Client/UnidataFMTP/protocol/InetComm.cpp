/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: InetComm.cpp
 *
 * @history:
 *      Created  : Sep 15, 2011
 *      Author   : jie
 *      Modified : Sep 21, 2014
 *      Author   : Shawn <sc7cq@virginia.edu>
 */

#include "InetComm.h"

InetComm::InetComm() {
	// TODO Auto-generated constructor stub

}

InetComm::~InetComm() {
	// TODO Auto-generated destructor stub
}


/*****************************************************************************
 * Class Name: InetComm
 * Function Name: SetBufferSize()
 *
 * Description: Set receive buffer size to buf_size.
 *
 * Input:  buf_size    size of the receiver buffer to be set.
 * Output: none
 ****************************************************************************/
void InetComm::SetBufferSize(size_t buf_size) {
	int size = buf_size;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof(size)) < 0) {
		SysError("Cannot set receive buffer size for raw socket.");
	}
}


/*****************************************************************************
 * Class Name: InetComm
 * Function Name: GetSocket()
 *
 * Description: return socket file descriptor.
 *
 * Input:  none
 * Output: none
 ****************************************************************************/
int InetComm::GetSocket() {
	return sock_fd;
}
