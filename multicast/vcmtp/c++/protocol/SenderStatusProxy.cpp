/*
 * SenderStatusProxy.cpp
 *
 *  Created on: Jun 28, 2011
 *      Author: jie
 */

#include "SenderStatusProxy.h"

SenderStatusProxy::SenderStatusProxy(string addr, int port, string group_addr, int vcmtp_port, int buff_size)
			: StatusProxy(addr, port) {
	vcmtp_group_addr = group_addr;
	vcmtp_port_num = vcmtp_port;
	buffer_size = buff_size;
	ptr_sender = NULL;
	integrator = NULL;

	file_retx_timeout_ratio = 1000000; //almost infinite retransmission deadline

	ConfigureEnvironment();
}

SenderStatusProxy::~SenderStatusProxy() {
	delete ptr_sender;
	if (integrator != NULL)
		delete integrator;
}


void SenderStatusProxy::ConfigureEnvironment() {
	// adjust udp (for multicasting) and tcp (for retransmission) receive buffer sizes
	system("sudo sysctl -w net.ipv4.udp_mem=\"4096 8388608 16777216\"");
	system("sudo sysctl -w net.ipv4.tcp_mem=\"4096 8388608 16777216\"");
	system("sudo sysctl -w net.ipv4.tcp_rmem=\"4096 8388608 16777216\"");
	system("sudo sysctl -w net.ipv4.tcp_wmem=\"4096 8388608 16777216\"");
	system("sudo sysctl -w net.core.rmem_default=\"8388608\"");
	system("sudo sysctl -w net.core.rmem_max=\"16777216\"");
	system("sudo sysctl -w net.core.wmem_default=\"8388608\"");
	system("sudo sysctl -w net.core.wmem_max=\"16777216\"");
	system("sudo sysctl -w net.core.netdev_max_backlog=\"10000\"");
}


void SenderStatusProxy::InitializeExecutionProcess() {
	if (ptr_sender != NULL) {
		delete ptr_sender;
	}

	ptr_sender = new VCMTPSender(buffer_size);
	ptr_sender->SetStatusProxy(this);
	ptr_sender->JoinGroup(vcmtp_group_addr, vcmtp_port_num);

	SendMessageLocal(INFORMATIONAL, "I'm the sender. Just joined the multicast group.");

	SetTxQueueLength(10000);
}


void SenderStatusProxy::SetTxQueueLength(int length) {
	char command[256];
	sprintf(command, "sudo ifconfig %s txqueuelen %d", ptr_sender->GetInterfaceName().c_str(), length);
	system(command);
}


int SenderStatusProxy::SendMessageLocal(int msg_type, string msg) {
	if (msg_type == EXP_RESULT_REPORT) {
//		if (result_file.is_open()) {
//			cout << "I received exp report: " << msg << endl;
//			result_file << msg;
//			//result_file << exp_manager.GetFileSize() << "," << exp_manager.GetSendRate() << "," << msg;
//		}
		exp_manager2.HandleExpResults(msg);
		return 1;
	}
	else {
		return StatusProxy::SendMessageLocal(msg_type, msg);
	}
}


int SenderStatusProxy::HandleCommand(const char* command) {
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
	if (parts.front().compare("Send") == 0) {
		parts.erase(parts.begin());
		HandleSendCommand(parts);
	}
	else if (parts.front().compare("TcpSend") == 0) {
			parts.erase(parts.begin());
			HandleTcpSendCommand(parts);
	}
	else if (parts.front().compare("SetRate") == 0) {
		if (parts.size() == 2) {
			int rate = atoi(parts.back().c_str());
			SetSendRate(rate); //ptr_sender->SetSendRate(rate);
		}
	}
	else if (parts.front().compare("SetRetxTimeoutRatio") == 0) {
		if (parts.size() == 2) {
			file_retx_timeout_ratio = atoi(parts.back().c_str());

			char buf[256];
			sprintf(buf, "Retransmission timeout ratio has been set to %d%%.", atoi(parts.back().c_str()));
			SendMessageLocal(COMMAND_RESPONSE, buf);
		}
	}
	else if (parts.front().compare("SetTCRate") == 0) {
		if (parts.size() == 2) {
			double rate = atoi(parts.back().c_str()) / 8.0;
			string dev = ptr_sender->GetInterfaceName();
			char buf[256];
			sprintf(buf, "sudo tc qdisc del dev %s root", dev.c_str());
			ExecSysCommand(buf);
			sprintf(buf, "sudo tc qdisc add dev %s handle 1: root htb", dev.c_str());
			ExecSysCommand(buf);
			sprintf(buf, "sudo tc class add dev %s parent 1: classid 1:1 htb rate %fMbps", dev.c_str(), rate);
			ExecSysCommand(buf);
			sprintf(buf, "sudo tc filter add dev %s parent 1: protocol ip prio 1 u32 match ip src 10.1.1.2/32 flowid 1:1", dev.c_str());
			ExecSysCommand(buf);

			sprintf(buf, "Send rate has been set to %d Mbps by TC.", atoi(parts.back().c_str()));
			SendMessageLocal(COMMAND_RESPONSE, buf);
		}
	}
	else if (parts.front().compare("CreateLogFile") == 0) {
		if (parts.size() == 2) {
			CreateNewLogFile(parts.back().c_str());
			SendMessageLocal(COMMAND_RESPONSE, "New log file created.");
		}
	}
	else if (parts.front().compare("SetLogSwitch") == 0) {
		if (parts.size() == 2) {
			if (parts.back().compare("On") == 0) {
				VCMTP::is_log_enabled = true;
				SendMessageLocal(COMMAND_RESPONSE, "Log switch set to ON.");
			}
			else if (parts.back().compare("Off") == 0) {
				VCMTP::is_log_enabled = false;
				SendMessageLocal(COMMAND_RESPONSE, "Log switch set to OFF.");
			}
		}
	}
	else if (parts.front().compare("CreateDataFile") == 0) {
		if (parts.size() == 3) {
			parts.pop_front();
			string file_name = parts.front();
			parts.pop_front();
			unsigned long size = strtoul(parts.front().c_str(), NULL, 0);
			GenerateDataFile(file_name, size);
			SendMessageLocal(COMMAND_RESPONSE, "Data file generated.");
		}
	}
	else if (parts.front().compare("StartExperiment") == 0) {
		SendMessageLocal(INFORMATIONAL, "Starting experiments...");
		//exp_manager.StartExperiment(this, ptr_sender);
		exp_manager2.StartExperiment2(this, ptr_sender);
		SendMessageLocal(INFORMATIONAL, "All experiments finished.");
	}
	else if (parts.front().compare("StartExperimentRetrans") == 0) {
		SendMessageLocal(INFORMATIONAL, "Starting low-speed experiments...");
		exp_manager.StartExperimentRetrans(this, ptr_sender);
		SendMessageLocal(INFORMATIONAL, "All experiments finished.");
	}
	else if (parts.front().compare("StartExperimentLS") == 0) {
		SendMessageLocal(INFORMATIONAL, "Starting low-speed experiments...");
		exp_manager.StartExperimentLowSpeed(this, ptr_sender);
		SendMessageLocal(INFORMATIONAL, "All experiments finished.");
	}
	else if (parts.front().compare("StartLDMIntegration") == 0) {
		if (parts.size() != 2)
			return -1;

		if (integrator != NULL) {
			integrator->Stop();
			delete integrator;
		}
		integrator = new LdmIntegrator(ptr_sender, parts.back(), this);
		integrator->Start();
		SendMessageLocal(INFORMATIONAL, "LDM Integrator has been started.");
	}
	else if (parts.front().compare("StopLDMIntegration") == 0) {
		if (integrator != NULL)
			integrator->Stop();

		SendMessageLocal(INFORMATIONAL, "LDM Integrator has been stopped.");
	}
	else if (parts.front().compare("SetSchedRR") == 0) {
		if (parts.size() == 2) {
			if (parts.back().compare("TRUE") == 0) {
				ptr_sender->SetSchedRR(true);
				SendMessageLocal(INFORMATIONAL, "Sending thread has been set to SCHED_RR mode.");
			}
			else {
				ptr_sender->SetSchedRR(false);
				SendMessageLocal(INFORMATIONAL, "Sending thread has been set to SCHED_RR mode.");
			}
		}
	}
	else {
		StatusProxy::HandleCommand(command);
	}

	return 1;
}


void SenderStatusProxy::SetSendRate(int rate_mbps) {
	char command[256];
	ptr_sender->SetSendRate(rate_mbps);

//	double MBps = rate_mbps / 8.0;
//	char rate[25];
//	sprintf(rate, "%.2fMbps", MBps);
//
//	char command[256];
//	sprintf(command, "sudo ./rate-limit.sh %s %d %d %s", ptr_sender->GetInterfaceName().c_str(),
//					PORT_NUM, BUFFER_TCP_SEND_PORT, rate);
//	ExecSysCommand(command);

	sprintf(command, "Data sending rate has been set to %d Mbps.", rate_mbps);
	SendMessageLocal(COMMAND_RESPONSE, command);
}

int SenderStatusProxy::GetRetransmissionTimeoutRatio() {
	return file_retx_timeout_ratio;
}

void SenderStatusProxy::SetRetransmissionBufferSize(int size_mb) {
	ptr_sender->SetRetransmissionBufferSize(size_mb);
	char command[256];
	sprintf(command, "Sender retransmission buffer size has been set to %d MB.", size_mb);
	SendMessageLocal(COMMAND_RESPONSE, command);
}

//
int SenderStatusProxy::HandleSendCommand(list<string>& slist) {
	bool memory_transfer = false;
	bool file_transfer = false;
	bool directory_transfer = false;
	bool send_out_packets = true;

	int 	mem_transfer_size = 0;
	string	file_name, dir_name;

	string arg = "";
	list<string>::iterator it;
	for (it = slist.begin(); it != slist.end(); it++) {
		if ((*it)[0] == '-') {
			switch ((*it)[1]) {
			case 'm':
				memory_transfer = true;
				it++;
				mem_transfer_size = atoi((*it).c_str());	// in Megabytes
				break;
			case 'f':
				file_transfer = true;
				it++;
				file_name = *it;
				break;
			case 'd':
				directory_transfer = true;
				it++;
				dir_name = *it;
				break;
			case 'n':
				send_out_packets = false;
				break;
			default:
				break;
			}
		}
		else {
			arg.append(*it);
			arg.append(" ");
		}
	}

	//ptr_sender->IPSend(&command[index + 1], args.length(), true);
	if (memory_transfer) {
		TransferMemoryData(mem_transfer_size);
	}
	else if (file_transfer) {
		TransferFile(file_name);
	}
	else if (directory_transfer) {
		TransferDirectory(dir_name);
	}
	else {
		TransferString(arg, send_out_packets);
	}

	// Send result status back to the monitor
//	if (send_out_packets) {
//		SendMessage(COMMAND_RESPONSE, "Data sent.");
//	}
//	else {
//		SendMessage(COMMAND_RESPONSE, "Data recorded but not sent out (simulate packet loss).");
//	}

	return 1;
}

// Transfer memory-to-memory data to all receivers
// size: the size of data to transfer (in megabytes)
int SenderStatusProxy::TransferMemoryData(int size) {
	SendMessageLocal(INFORMATIONAL, "Transferring memory data...");

	char* buffer = (char*)malloc(size);
	ptr_sender->SendMemoryData(buffer, size);
	free(buffer);

	SendMessageLocal(COMMAND_RESPONSE, "Memory data transfer completed.");
	return 1;
}


// Transfer a disk file to all receivers
void SenderStatusProxy::TransferFile(string file_name) {
	system("sudo sync && sudo echo 3 > /proc/sys/vm/drop_caches");

	SendMessageLocal(INFORMATIONAL, "Transferring file...");
	ptr_sender->SendFile(file_name.c_str(), file_retx_timeout_ratio);
	//ptr_sender->SendFileBufferedIO(file_name.c_str());
	SendMessageLocal(COMMAND_RESPONSE, "File transfer completed.");
}


// Transfer all the disk files under a directory
void SenderStatusProxy::TransferDirectory(string dir_name) {
	system("sudo sync && sudo echo 3 > /proc/sys/vm/drop_caches");

}


// Multicast a string message to receivers
int SenderStatusProxy::TransferString(string str, bool send_out_packets) {
	SendMessageLocal(COMMAND_RESPONSE, "Specified string successfully sent.");
	return 1;
}


// Send data using TCP connections (for performance comparison)
int SenderStatusProxy::HandleTcpSendCommand(list<string>& slist) {
	bool memory_transfer = false;
	bool file_transfer = false;
	bool send_out_packets = true;

	int mem_transfer_size = 0;
	string file_name;

	string arg = "";
	list<string>::iterator it;
	for (it = slist.begin(); it != slist.end(); it++) {
		if ((*it)[0] == '-') {
			switch ((*it)[1]) {
			case 'm':
				memory_transfer = true;
				it++;
				mem_transfer_size = atoi((*it).c_str()); // in Megabytes
				break;
			case 'f':
				file_transfer = true;
				it++;
				file_name = *it;
				break;
			case 'n':
				send_out_packets = false;
				break;
			default:
				break;
			}
		} else {
			arg.append(*it);
			arg.append(" ");
		}
	}

	//ptr_sender->IPSend(&command[index + 1], args.length(), true);
	if (memory_transfer) {
		TcpTransferMemoryData(mem_transfer_size);
	} else if (file_transfer) {
		TcpTransferFile(file_name);
	} else {
	}
}


int SenderStatusProxy::TcpTransferMemoryData(int size) {
	SendMessageLocal(INFORMATIONAL, "Transferring memory data...\n");

	char* buffer = (char*)malloc(size);
	ptr_sender->TcpSendMemoryData(buffer, size);
	free(buffer);

	SendMessageLocal(COMMAND_RESPONSE, "Memory data transfer completed.\n\n");
	return 1;
}


void SenderStatusProxy::TcpTransferFile(string file_name) {
	SendMessageLocal(INFORMATIONAL, "Transferring file...\n");
	ptr_sender->TcpSendFile(file_name.c_str());
	SendMessageLocal(COMMAND_RESPONSE, "File transfer completed.\n\n");
}



// Generate a local data file for disk-to-disk transfer experiments
int SenderStatusProxy::GenerateDataFile(string file_name, ulong bytes) {
	int buf_size = 256;
	char buffer[buf_size];
	for (int i = 0; i < buf_size; i++) {
		buffer[i] = i;
	}

	ulong remained_size = bytes;
	ofstream myfile(file_name.c_str(), ios::out | ios::trunc);
	if (myfile.is_open()) {
		while (remained_size > 0) {
			ulong len = remained_size < buf_size ? remained_size : buf_size;
			myfile.write(buffer, len);
			remained_size -= len;
		}

		myfile.close();
		return 1;
	}
	else {
		return -1;
	}
}











