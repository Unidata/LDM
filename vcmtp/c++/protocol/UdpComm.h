/*
 * UdpComm.h
 *
 *  Created on: Jul 4, 2011
 *      Author: jie
 */

#ifndef UDPCOMM_H_
#define UDPCOMM_H_

#include "vcmtp.h"

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
