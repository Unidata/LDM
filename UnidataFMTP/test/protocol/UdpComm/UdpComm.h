/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: UdpComm.h
 *
 * @history:
 *      Created on : Jul 21, 2011
 *      Author     : jie
 *      Modified   : Sep 21, 2014
 *      Author     : Shawn <sc7cq@virginia.edu>
 */

#ifndef UDPCOMM_H_
#define UDPCOMM_H_

#include "fmtp.h"

void SysError(string s);

class UdpComm {
public:
	UdpComm(ushort port);
	~UdpComm();

	void SetSocketBufferSize(size_t size);
	ssize_t SendTo(const void* buff, size_t len, int flags, SA* to_addr,  socklen_t to_len);
	ssize_t RecvFrom(void* buff, size_t len, int flags, SA* from_addr, socklen_t* from_len);

private:
	int sock_fd;
	struct sockaddr_in server_addr;
};

#endif /* UDPSCOMM_H_ */
