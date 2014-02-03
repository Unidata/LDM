/*
 * ReceiverStatusProxy.cpp
 *
 *  Created on: Jun 28, 2011
 *      Author: jie
 */

#include "ReceiverStatusProxy.h"

ReceiverStatusProxy::ReceiverStatusProxy(string addr, int port, string group_addr, int vcmtp_port, int buff_size)
		: StatusProxy(addr, port) {
	vcmtp_group_addr = group_addr;
	vcmtp_port_num = vcmtp_port;
	buffer_size = buff_size;
	ptr_receiver = NULL;

	ConfigureEnvironment();
}


ReceiverStatusProxy::~ReceiverStatusProxy() {
	delete ptr_receiver;
}


void ReceiverStatusProxy::ConfigureEnvironment() {
	// adjust udp (for multicasting) and tcp (for retransmission) receive buffer sizes
	//system("sudo sysctl -w net.ipv4.tcp_rmem=\"4096 8388608 36777216\"");
	//system("sudo sysctl -w net.ipv4.tcp_wmem=\"4096 8388608 36777216\"");

	system("sudo sysctl -w net.ipv4.udp_mem=\"4096 8388608 36777216\""); // 16777216
	system("sudo sysctl -w net.core.rmem_default=\"8388608\""); //8388608
	system("sudo sysctl -w net.core.rmem_max=\"16777216\""); // 16777216
	system("sudo sysctl -w net.core.wmem_default=\"16777216\""); //8388608
	system("sudo sysctl -w net.core.wmem_max=\"36777216\""); //
	system("sudo sysctl -w net.ipv4.tcp_mem=\"4096 8388608 16777216\"");
	system("sudo sysctl -w net.ipv4.tcp_rmem=\"4096 8388608 16777216\"");
	system("sudo sysctl -w net.ipv4.tcp_wmem=\"4096 8388608 16777216\"");
	system("sudo sysctl -w net.core.netdev_max_backlog=\"10000\"");
}


//
void ReceiverStatusProxy::InitializeExecutionProcess() {
	if (ptr_receiver != NULL)
		delete ptr_receiver;

	ptr_receiver = new VCMTPReceiver(buffer_size);
	ptr_receiver->SetStatusProxy(this);
	ptr_receiver->JoinGroup(vcmtp_group_addr, vcmtp_port_num);

	SendMessageLocal(INFORMATIONAL, "I'm a receiver. Just joined the multicast group.");

	char command[256];
	sprintf(command, "sudo ifconfig %s txqueuelen 10000",
	ptr_receiver->GetInterfaceName().c_str());
	system(command);

	pthread_create(&receiver_thread, NULL, &ReceiverStatusProxy::StartReceiverThread, this);
}


void* ReceiverStatusProxy::StartReceiverThread(void* ptr) {
	((ReceiverStatusProxy*)ptr)->RunReceiverThread();
	return NULL;
}


void ReceiverStatusProxy::RunReceiverThread() {
	ptr_receiver->Start();
}

int ReceiverStatusProxy::HandleCommand(const char* command) {
	string s = command;
	/*int length = s.length();
	int index = s.find(' ');
	string comm_name = s.substr(0, index);
	string args = s.substr(index + 1, length - index - 1);
	 */

	list<string> parts;
	Split(s, ' ', parts);
	if (parts.size() == 0)
		return 0;

	char msg[512];
	if (parts.front().compare("SetLossRate") == 0) {
		if (parts.size() == 2) {
			int rate = atoi(parts.back().c_str());
			ptr_receiver->SetPacketLossRate(rate);
		}
		else {
			SendMessageLocal(COMMAND_RESPONSE, "Usage: SetLossRate lost_packets_per_thousand)");
		}
	}
	else if (parts.front().compare("GetLossRate") == 0) {
		int rate = ptr_receiver->GetPacketLossRate();
		sprintf(msg, "Packet loss rate: %d per thousand.", rate);
		SendMessageLocal(COMMAND_RESPONSE, msg);
	}
	else if (parts.front().compare("ResetStatistics") == 0) {
		ptr_receiver->ResetHistoryStats();
		SendMessageLocal(COMMAND_RESPONSE, "Statistics has been reset.");
	}
	else if (parts.front().compare("GetStatistics") == 0) {
		ptr_receiver->SendHistoryStats();
	}
	else if (parts.front().compare("SetBufferSize") == 0) {
		if (parts.size() == 2) {
			int buf_size = atoi(parts.back().c_str());
			ptr_receiver->SetBufferSize(buf_size);
			sprintf(msg, "Receive buffer size has been set to %d.", buf_size);
			SendMessageLocal(COMMAND_RESPONSE, msg);
		}
		else {
			SendMessageLocal(COMMAND_RESPONSE, "Usage: SetBufferSize size_in_bytes");
		}
	}
	else if (parts.front().compare("SetTCRate") == 0) {
		if (parts.size() == 2) {
			int rate = atoi(parts.back().c_str());
			string dev = ptr_receiver->GetInterfaceName();
			char buf[256];
			sprintf(buf, "sudo tc qdisc del dev %s handle ffff: ingress", dev.c_str());
			ExecSysCommand(buf);
			sprintf(buf, "sudo tc qdisc add dev %s handle ffff: ingress", dev.c_str());
			ExecSysCommand(buf);
			sprintf(buf, "sudo tc filter add dev %s parent ffff: protocol ip prio 50 u32 match ip src 10.1.1.2/32 "
						 "police rate %dMbit burst 10m drop flowid :1", dev.c_str(), rate);
			ExecSysCommand(buf);

			sprintf(buf, "Receive rate has been set to %d Mbps by TC.", rate);
			SendMessageLocal(COMMAND_RESPONSE, buf);
		}
	} else if (parts.front().compare("CreateLogFile") == 0) {
		if (parts.size() == 2) {
			CreateNewLogFile(parts.back().c_str());
			SendMessageLocal(COMMAND_RESPONSE, "New log file created.");
		}
	} else if (parts.front().compare("SetLogSwitch") == 0) {
		if (parts.size() == 2) {
			if (parts.back().compare("On") == 0) {
				VCMTP::is_log_enabled = true;
			} else if (parts.back().compare("Off") == 0) {
				VCMTP::is_log_enabled = false;
			}
			SendMessageLocal(COMMAND_RESPONSE, "Log switch set.");
		}
	} else if (parts.front().compare("SetSchedRR") == 0) {
		ptr_receiver->SetSchedRR(true);
		SendMessageLocal(COMMAND_RESPONSE, "Receiver process has been set to SCHED_RR mode.");
	} else if (parts.front().compare("SetNoSchedRR") == 0) {
			ptr_receiver->SetSchedRR(false);
			SendMessageLocal(COMMAND_RESPONSE, "Receiver process has been set to SCHED_OTHER (normal) mode.");
	} else {
		StatusProxy::HandleCommand(command);
	}

	return 1;
}
