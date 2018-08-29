/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: FMTPComm.cpp
 *
 * @history:
 *      Created  : Sep 15, 2011
 *      Author   : jie
 *      Modified : Seo 19, 2014
 *      Author   : Shawn <sc7cq@virginia.edu>
 */

#include "FMTPComm.h"
#include <errno.h>
#include <exception>
#include <stdexcept>
#include <string.h>

FMTPComm::FMTPComm()
:   port_num(0),
    fmtp_group_id(0)
{
    // create a new MulticastComm object.
	ptr_multicast_comm = new MulticastComm();

	send_fmtp_header = (PTR_FMTP_HEADER)send_packet_buf;
	send_data = send_packet_buf + sizeof(FMTP_HEADER);

	eth_header = (struct ethhdr *)recv_frame_buf;
	recv_fmtp_header = (PTR_FMTP_HEADER)(recv_frame_buf + ETH_HLEN);
	recv_data = (u_char*)recv_frame_buf + ETH_HLEN + sizeof(FMTP_HEADER);

	if_manager = new NetInterfaceManager();
	for (PTR_IFI_INFO ptr_ifi = if_manager->GetIfiHead(); ptr_ifi != NULL;
			ptr_ifi = ptr_ifi->ifi_next) {
		sockaddr_in* addr = (sockaddr_in*)ptr_ifi->ifi_addr;
		string ip = inet_ntoa(addr->sin_addr);
		if (ip.find("10.1.") != ip.npos) {
			if_name = ptr_ifi->ifi_name;
			if_ip = ip;
			cout << "Raw Socket Interface: " << if_name << endl;
			break;
		}
	}
	cout << "interface name: " << if_name << endl;
	ptr_raw_sock_comm = new RawSocketComm(if_name.c_str());
}

FMTPComm::~FMTPComm() {
	delete if_manager;
	delete ptr_raw_sock_comm;
	delete ptr_multicast_comm;
}


string FMTPComm::GetInterfaceName() {
	return if_name;
}

string FMTPComm::GetMulticastAddress() {
	return group_addr;
}

int FMTPComm::GetPortNumber() {
	return port_num;
}


/**
 * Joins an Internet multicast group. This configures the socket locally to
 * receive multicast packets destined to the given port and adds an Internet
 * multicast group to the socket.
 *
 * @param[in] addr                   IPv4 address of the multicast group in
 *                                   dotted-decimal format.
 * @param[in] port                   Port number of the multicast group in
 *                                   native byte order.
 * @returns   1                      Success.
 * @throws    std::invalid_argument  if \c addr couldn't be converted into a
 *                                   binary IPv4 address.
 * @throws    std::runtime_error     if the IP address of the PA interface
 *                                   couldn't be obtained. (The PA address seems
 *                                   to be specific to Linux and might cause
 *                                   problems.)
 * @throws    std::runtime_error     if the socket couldn't be bound to the
 *                                   interface.
 * @throws    std::runtime_error     if the socket couldn't be bound to the
 *                                   Internet address.
 * @throws    std::runtime_error     if the multicast group sa couldn't be added
 *                                   to the socket.
 */
int FMTPComm::JoinGroup(const string addr, const ushort port) {
	group_addr = addr;
	port_num = port;

	sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_port = htons(PORT_NUM);

	if (inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) != 1) {
	    throw std::invalid_argument(std::string("Couldn't convert Internet "
	            "address \"") + addr + "\" into binary IPv4 address: " +
	            strerror(errno));
	}

	ptr_multicast_comm->JoinGroup((SA *)&sa, sizeof(sa), if_name.c_str()); //(char*)NULL); //

	fmtp_group_id = sa.sin_addr.s_addr;
	GetMulticastMacAddressFromIP(mac_group_addr, fmtp_group_id);
	ptr_raw_sock_comm->Bind((SA *)&sa, sizeof(sa), mac_group_addr);
	send_fmtp_header->src_port = port;
	return 1;
}



void FMTPComm::GetMulticastMacAddressFromIP(u_char* mac_addr, u_int ip_addr) {
	u_char* ptr = (u_char*)&ip_addr;
	mac_addr[0] = 0x01;
	mac_addr[1]	= 0x00;
	mac_addr[2] = 0x5e;
	mac_addr[3] = ptr[1] & 0x7f;
	mac_addr[4] = ptr[2];
	mac_addr[5] = ptr[3];
}
