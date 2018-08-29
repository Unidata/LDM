/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: FMTPComm.h
 *
 * @history:
 *      Created  : Sep 15, 2011
 *      Author   : jie
 *      Modified : Seo 19, 2014
 *      Author   : Shawn <sc7cq@virginia.edu>
 */

#ifndef FMTPCOMM_H_
#define FMTPCOMM_H_

#include "fmtp.h"
#include "RawSocketComm.h"
#include "MulticastComm.h"
#include "NetInterfaceManager.h"

class FMTPComm {
public:
	FMTPComm();
	virtual ~FMTPComm();

	virtual int JoinGroup(string addr, u_short port);
	string 		GetInterfaceName();

	string		GetMulticastAddress();
	int 		GetPortNumber();


protected:
	NetInterfaceManager* if_manager;
	string if_name, if_ip;
    // what is RawSocketComm? why different from MulticastComm?
	RawSocketComm* ptr_raw_sock_comm;
	MulticastComm* ptr_multicast_comm;

	int			port_num;
	string 		group_addr;
	u_int32_t 	fmtp_group_id;
	u_char 		mac_group_addr[6];

private:
	// single FMTP packet send buffer
	char send_packet_buf[ETH_DATA_LEN];
	FMTP_HEADER* send_fmtp_header;
	char* send_data;

	// single MAC frame receive buffer
	char recv_frame_buf[ETH_FRAME_LEN];
	struct ethhdr* eth_header;
	FMTP_HEADER* recv_fmtp_header;
	u_char* recv_data;

	void GetMulticastMacAddressFromIP(u_char* mac_addr, u_int ip_addr);

};
#endif /* FMTPCOMM_H_ */
