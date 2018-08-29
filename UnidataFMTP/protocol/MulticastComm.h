/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: MulticastComm.h
 *
 * @history:
 *      Created on : Jul 21, 2011
 *      Author     : jie
 *      Modified   : Sep 13, 2014
 *      Author     : Shawn <sc7cq@virginia.edu>
 */

#ifndef MULTICASTCOMM_H_
#define MULTICASTCOMM_H_

#include "fmtp.h"
#include "InetComm.h"
#include <pthread.h>

using namespace std;

typedef struct sockaddr SA;

class MulticastComm : public InetComm {
public:
	MulticastComm();
	virtual ~MulticastComm();
	int JoinGroup(const SA* sa, int sa_len, const char *if_name);
	int JoinGroup(const SA* sa, int sa_len, u_int if_index);
	int SetLoopBack(int onoff);
	int LeaveGroup();
	ssize_t SendData(const void* buff, size_t len, int flags, void* dst_addr);
        ssize_t SendData(
                const void*  header,
                const size_t headerLen,
                const void*  data,
                const size_t dataLen);
	ssize_t SendPacket(PacketBuffer* buffer, int flags, void* dst_addr);
	ssize_t RecvData(void* buff, size_t len, int flags, SA* from, socklen_t* from_len);

private:
	SA dst_addr;
	int dst_addr_len;
    // ip_mreq is a Linux struct, containing in_addr type interface ip and
    // multicast ip.
	ip_mreq mreq;
};

#endif /* MULTICASTCOMM_H_ */
