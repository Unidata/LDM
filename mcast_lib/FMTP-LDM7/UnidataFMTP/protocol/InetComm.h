/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: InetComm.h
 *
 * @history:
 *      Created on : Jul 21, 2011
 *      Author     : jie
 *      Modified   : Sep 13, 2014
 *      Author     : Shawn <sc7cq@virginia.edu>
 */

#ifndef INETCOMM_H_
#define INETCOMM_H_

#include "fmtp.h"

class InetComm {
public:
	InetComm();
	virtual ~InetComm();

	virtual void SetBufferSize(size_t buf_size);
	virtual ssize_t SendData(const void* buff, size_t len, int flags, void* dst_addr) = 0;
	virtual ssize_t SendPacket(PacketBuffer* buffer, int flags, void* dst_addr) = 0;
	virtual ssize_t RecvData(void* buff, size_t len, int flags, SA* from, socklen_t* from_len) = 0;
	int		GetSocket();

protected:
	int sock_fd; // socket file descriptor

private:

};

#endif /* INETCOMM_H_ */
