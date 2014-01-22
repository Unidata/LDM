/*
 * MVCTPManager.h
 *
 *  Created on: Jun 29, 2011
 *      Author: jie
 */

#ifndef MVCTPCOMM_H_
#define MVCTPCOMM_H_

#include "mvctp.h"
#include "RawSocketComm.h"
#include "MulticastComm.h"
#include "NetInterfaceManager.h"

class MVCTPComm {
public:
	MVCTPComm();
	~MVCTPComm();

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
	u_int32_t 	mvctp_group_id;
	u_char 		mac_group_addr[6];

private:
	// single MVCTP packet send buffer
	char send_packet_buf[ETH_DATA_LEN];
	MVCTP_HEADER* send_mvctp_header;
	char* send_data;

	// single MAC frame receive buffer
	char recv_frame_buf[ETH_FRAME_LEN];
	struct ethhdr* eth_header;
	MVCTP_HEADER* recv_mvctp_header;
	u_char* recv_data;

	void GetMulticastMacAddressFromIP(u_char* mac_addr, u_int ip_addr);

};
#endif /* MVCTPCOMM_H_ */
