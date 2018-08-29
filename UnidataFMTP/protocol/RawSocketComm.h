/*
 * RawSocketComm.h
 *
 *  Created on: Jun 29, 2011
 *      Author: jie
 */

#ifndef RAWSOCKETCOMM_H_
#define RAWSOCKETCOMM_H_

#include "fmtp.h"
#include "InetComm.h"
#include <cstdio>

#define RATE_CHECK_PERIOD 	25		// in milliseconds

struct MacFrame {
	u_char 		dst_addr[ETH_ALEN];
	u_char 		src_addr[ETH_ALEN];
	u_int16_t  	proto;
	u_char 		data[ETH_DATA_LEN];
	int			payload_size;

#define frame_buffer	dst_addr
};


class RawSocketComm : public InetComm {
public:
	RawSocketComm(const char* if_name);
	//int SendData(const u_char* dest_addr, void* buffer, size_t length);
	//int ReceiveData(void* buffer, size_t length);

	void SetBufferSize(size_t buf_size);
	virtual ssize_t SendData(const void* buff, size_t len, int flags, void* dst_addr);
	virtual ssize_t SendPacket(PacketBuffer* buffer, int flags, void* dst_addr);
	virtual ssize_t RecvData(void* buff, size_t len, int flags, SA* from, socklen_t* from_len);

	void Bind(const SA* sa, int sa_len, u_char* mac_addr);
	int ReceiveFrame(void* buffer);
	void SetSendRate(int num_mbps);
	int SendFrame(void* buffer, size_t length);

private:
	int if_index;
	struct ifreq if_req;
	unsigned char mac_addr[6];			// source MAC address
	unsigned char bind_mac_addr[6];			// accepted dest MAC address for received frames
	struct sockaddr_ll dest_address; 	// target address
	MacFrame send_frame, recv_frame;

	// parameters for rate throttling
	int send_rate_in_mbps;
	size_t unit_size_token;
	size_t current_size_token;
	//timeval last_check_time;
	CpuCycleCounter last_checked_counter;
	void WaitForNewToken();
	bool IsMyPacket();
	string GetMacAddrString(const unsigned char* addr);
};

#endif /* RAWSOCKETCOMM_H_ */
