/*
 * InetComm.h
 *
 *  Created on: Jul 21, 2011
 *      Author: jie
 */

#ifndef INETCOMM_H_
#define INETCOMM_H_

#include "vcmtp.h"

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
	int sock_fd;

private:

};

#endif /* INETCOMM_H_ */
