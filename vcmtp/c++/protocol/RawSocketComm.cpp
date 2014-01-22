/*
 * RawSocketComm.cpp
 *
 *  Created on: Jun 29, 2011
 *      Author: jie
 */
#include "RawSocketComm.h"

RawSocketComm::RawSocketComm(const char* if_name) {
	if ( (sock_fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
			SysError("Cannot create new socket.");
	}
	SetBufferSize(2048000);


	// get the index of the network device
	memset(&if_req, 0, sizeof(if_req));
	strncpy(if_req.ifr_name, if_name, sizeof(if_req.ifr_name));
	if (ioctl(sock_fd, SIOCGIFINDEX, &if_req)) {
		SysError("unable to get index");
	}
	if_index = if_req.ifr_ifindex;

	// get the MAC address of the interface
	if (ioctl(sock_fd, SIOCGIFHWADDR, &if_req) < 0) {
		SysError("Cannot get network interface address: ");
	}
	memcpy(mac_addr, if_req.ifr_addr.sa_data, ETH_ALEN);
	memcpy(bind_mac_addr, mac_addr, ETH_ALEN);

	//int if_index = 2;
	//if (if_indextoname(if_index, if_req.ifr_name) == NULL) {
	//	SysError("Cannot find network interface.");
	//}

	dest_address.sll_family = AF_PACKET;
	dest_address.sll_protocol = htons(ETH_P_ALL);
	dest_address.sll_ifindex = if_index;
	dest_address.sll_hatype = 1;  	//ARPHRD_ETHER;
	dest_address.sll_pkttype = PACKET_OTHERHOST;
	dest_address.sll_halen = ETH_ALEN;
	dest_address.sll_addr[6] = 0x00;/*not used*/
	dest_address.sll_addr[7] = 0x00;/*not used*/

	memcpy(send_frame.src_addr, mac_addr, ETH_ALEN);
	send_frame.proto = htons(VCMTP_PROTO_TYPE);
}


void RawSocketComm::SetBufferSize(size_t buf_size) {
	int size = buf_size;
	if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof(size)) < 0) {
		SysError("Cannot set receive buffer size for raw socket.");
	}
}

void RawSocketComm::Bind(const SA* sa, int sa_len, u_char* mac_addr) {
	memcpy(bind_mac_addr, mac_addr, ETH_ALEN);
}

void RawSocketComm::SetSendRate(int num_mbps) {
	send_rate_in_mbps = num_mbps;
	unit_size_token = (size_t)(RATE_CHECK_PERIOD / 1000.0 * num_mbps * 1024 * 1024 / 8.0);	// change to number of bytes
	current_size_token = unit_size_token;
	AccessCPUCounter(&last_checked_counter.hi, &last_checked_counter.lo);
	//gettimeofday(&last_check_time, NULL);
}

void RawSocketComm::WaitForNewToken() {
	//timeval cur_time;
	double time_diff;
	double diff_unit = RATE_CHECK_PERIOD / 1000.0;
	int  new_token = unit_size_token;
	bool isConstrained = false;

	//gettimeofday(&cur_time, NULL);
	//time_diff = (cur_time.tv_sec - last_check_time.tv_sec) * 1000000
	//		+ (cur_time.tv_usec - last_check_time.tv_usec);
	time_diff = GetElapsedSeconds(last_checked_counter);
	while (time_diff < diff_unit) {
		isConstrained = true;
		usleep(5000);
		time_diff = GetElapsedSeconds(last_checked_counter);
//		gettimeofday(&cur_time, NULL);
//		time_diff = (cur_time.tv_sec - last_check_time.tv_sec) * 1000000
//					+ (cur_time.tv_usec - last_check_time.tv_usec);
	}

	if (isConstrained) {
		new_token = (int)(new_token * 1.0 * time_diff / diff_unit);
	}

	current_size_token += new_token;
	AccessCPUCounter(&last_checked_counter.hi, &last_checked_counter.lo);
	//last_check_time = cur_time;

}


ssize_t RawSocketComm::SendData(const void* buff, size_t len, int flags, void* dst_addr) {
	// set the destination address
	memcpy(dest_address.sll_addr, dst_addr, ETH_ALEN);
	// set the frame header
	memcpy(send_frame.dst_addr, dst_addr, ETH_ALEN);

	size_t remained_len = len;
	u_char* pos = (u_char*)buff;
	while (remained_len > 0) {
		int payload_size = remained_len > ETH_DATA_LEN ? ETH_DATA_LEN : remained_len;
		memcpy(send_frame.data, pos, payload_size);
		SendFrame(send_frame.frame_buffer, payload_size + ETH_HLEN);
		pos += payload_size;
		remained_len -= payload_size;
	}

	return len;
}


ssize_t RawSocketComm::SendPacket(PacketBuffer* buffer, int flags, void* dst_addr) {
	memcpy(dest_address.sll_addr, dst_addr, ETH_ALEN);
	ethhdr* eth_header = (ethhdr*)buffer->eth_header;
	memcpy(eth_header->h_source, mac_addr, ETH_ALEN);
	memcpy(eth_header->h_dest, dst_addr, ETH_ALEN);
	eth_header->h_proto = htons(VCMTP_PROTO_TYPE);

	return SendFrame(buffer->eth_header, buffer->data_len + VCMTP_HLEN + ETH_HLEN);
}


int RawSocketComm::SendFrame(void* buffer, size_t length) {
	if (current_size_token < length) {
		WaitForNewToken();
	}

	int res;
	if ((res = sendto(sock_fd, buffer, length, 0,
				(SA*) &dest_address, sizeof(dest_address))) < 0) {
		SysError("RawSocketComm::SendFrame()::sendto() error.");
	}

	current_size_token -= res;
	return res;
}



ssize_t RawSocketComm::RecvData(void* buff, size_t len, int flags, SA* from, socklen_t* from_len) {
	size_t remained_len = len;
	size_t received_bytes = 0;
	char* ptr = (char*)buff;

	int bytes;
	while (remained_len > 0) {
		if ( (bytes = ReceiveFrame(recv_frame.frame_buffer)) < 0) {
			return bytes;
		}

		if (IsMyPacket()) {
			int data_len = bytes - ETH_HLEN;
			recv_frame.payload_size = data_len;
			memcpy(ptr, recv_frame.data, data_len);

//			cout << "One RAW frame received." << endl;
//			cout << "Source Addr: " << this->GetMacAddrString(recv_frame.src_addr) << endl;
//			cout << "Dest Addr: " << this->GetMacAddrString(recv_frame.dst_addr) << endl;
//			cout << "Payload Size: " << recv_frame.payload_size << endl;

			received_bytes += data_len;
			ptr += data_len;
			remained_len -= data_len;
			return received_bytes;
		}
	}

	return len;
}


int RawSocketComm::ReceiveFrame(void* buffer) {
	return recvfrom(sock_fd, buffer, ETH_FRAME_LEN, 0, NULL, NULL);
}


bool RawSocketComm::IsMyPacket() {
	if (memcmp(recv_frame.dst_addr, bind_mac_addr, 6) == 0
		&& recv_frame.proto == htons(VCMTP_PROTO_TYPE)) {
		return true;
	}
	else
		return false;
}


string RawSocketComm::GetMacAddrString(const unsigned char* addr) {
	char buff[50];
	sprintf(buff, "%#x:%#x:%#x:%#x:%#x:%#x", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
	return buff;
}
