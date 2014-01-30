/*
 * MulticastComm.h
 *
 *  Created on: Jun 2, 2011
 *      Author: jie
 */

#ifndef MULTICASTCOMM_H_
#define MULTICASTCOMM_H_

#include "vcmtp.h"
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
	ssize_t SendPacket(PacketBuffer* buffer, int flags, void* dst_addr);
	ssize_t RecvData(void* buff, size_t len, int flags, SA* from, socklen_t* from_len);

private:
	SA dst_addr;
	int dst_addr_len;
	ip_mreq mreq;
};

#endif /* MULTICASTCOMM_H_ */
