/*
 * VCMTPManager.cpp
 *
 *  Created on: Jun 29, 2011
 *      Author: jie
 */

#include "VCMTPComm.h"

VCMTPComm::VCMTPComm() {
	ptr_multicast_comm = new MulticastComm();

	send_vcmtp_header = (PTR_VCMTP_HEADER)send_packet_buf;
	send_data = send_packet_buf + sizeof(VCMTP_HEADER);

	eth_header = (struct ethhdr *)recv_frame_buf;
	recv_vcmtp_header = (PTR_VCMTP_HEADER)(recv_frame_buf + ETH_HLEN);
	recv_data = (u_char*)recv_frame_buf + ETH_HLEN + sizeof(VCMTP_HEADER);

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

VCMTPComm::~VCMTPComm() {
	delete if_manager;
	delete ptr_raw_sock_comm;
	delete ptr_multicast_comm;
}


string VCMTPComm::GetInterfaceName() {
	return if_name;
}

string VCMTPComm::GetMulticastAddress() {
	return group_addr;
}

int VCMTPComm::GetPortNumber() {
	return port_num;
}


// Not IGMP join
// Local register of multicast address
int VCMTPComm::JoinGroup(string addr, ushort port) {
	group_addr = addr;
	port_num = port;

	sockaddr_in sa;
	sa.sin_family = AF_INET;
	sa.sin_port = htons(PORT_NUM);
	inet_pton(AF_INET, addr.c_str(), &sa.sin_addr);
	ptr_multicast_comm->JoinGroup((SA *)&sa, sizeof(sa), if_name.c_str()); //(char*)NULL); //

	vcmtp_group_id = sa.sin_addr.s_addr;
	GetMulticastMacAddressFromIP(mac_group_addr, vcmtp_group_id);
	ptr_raw_sock_comm->Bind((SA *)&sa, sizeof(sa), mac_group_addr);
	send_vcmtp_header->src_port = port;
	return 1;
}



void VCMTPComm::GetMulticastMacAddressFromIP(u_char* mac_addr, u_int ip_addr) {
	u_char* ptr = (u_char*)&ip_addr;
	mac_addr[0] = 0x01;
	mac_addr[1]	= 0x00;
	mac_addr[2] = 0x5e;
	mac_addr[3] = ptr[1] & 0x7f;
	mac_addr[4] = ptr[2];
	mac_addr[5] = ptr[3];
}
