/*
 * VCMTPManager.h
 *
 *  Created on: Jun 29, 2011
 *      Author: jie
 */

#ifndef VCMTPCOMM_H_
#define VCMTPCOMM_H_

#include "vcmtp.h"
#include "RawSocketComm.h"
#include "MulticastComm.h"
#include "NetInterfaceManager.h"

class VCMTPComm {
public:
	VCMTPComm();
	~VCMTPComm();

	virtual int JoinGroup(string addr, u_short port);
	string 		GetInterfaceName();

	string		GetMulticastAddress();
	int 		GetPortNumber();


protected:
	NetInterfaceManager* if_manager;
	string if_name, if_ip;
	RawSocketComm* ptr_raw_sock_comm;
	MulticastComm* ptr_multicast_comm;

	int			port_num;
	string 		group_addr;
	u_int32_t 	vcmtp_group_id;
	u_char 		mac_group_addr[6];

private:
	// single VCMTP packet send buffer
	char send_packet_buf[ETH_DATA_LEN];
	VCMTP_HEADER* send_vcmtp_header;
	char* send_data;

	// single MAC frame receive buffer
	char recv_frame_buf[ETH_FRAME_LEN];
	struct ethhdr* eth_header;
	VCMTP_HEADER* recv_vcmtp_header;
	u_char* recv_data;

	void GetMulticastMacAddressFromIP(u_char* mac_addr, u_int ip_addr);

};
#endif /* VCMTPCOMM_H_ */
