/*
 * Tester.cpp
 *
 *  Created on: Jun 29, 2011
 *      Author: jie
 */

#include "Tester.h"

Tester::Tester() {
}

Tester::~Tester() {
	delete ptr_status_proxy;
}

void Tester::StartTest() {
	VCMTPInit();

	string serv_addr = ConfigInfo::GetInstance()->GetValue("Monitor_Server");
	string port = ConfigInfo::GetInstance()->GetValue("Monitor_Server_Port");

	int send_buf_size = atoi(ConfigInfo::GetInstance()->GetValue("Send_Buffer_Size").c_str());
	int recv_buf_size = atoi(ConfigInfo::GetInstance()->GetValue("Recv_Buffer_Size").c_str());

	if (IsSender()) {
		if (serv_addr.length() > 0) {
			ptr_status_proxy = new SenderStatusProxy(serv_addr, atoi(port.c_str()), group_id, vcmtp_port, send_buf_size);
			cout << "Connecting manager server..." << endl;
			ptr_status_proxy->ConnectServer();
			cout << "Server connected." << endl;
			ptr_status_proxy->StartService();
		}

		//this->SendMessage(INFORMATIONAL, "I'm the sender. Just joined the multicast group.");
		while (true) {
			sleep(1);
		}
	}
	else {  //Is receiver
		// Set the receiver process to real-time scheduling mode
//		struct sched_param sp;
//		sp.__sched_priority = sched_get_priority_max(SCHED_RR);
//		sched_setscheduler(0, SCHED_RR, &sp);

		if (serv_addr.length() > 0) {
			ptr_status_proxy = new ReceiverStatusProxy(serv_addr, atoi(port.c_str()), group_id, vcmtp_port, recv_buf_size);
			ptr_status_proxy->ConnectServer();
			ptr_status_proxy->StartService();
		}

		//this->SendMessage(INFORMATIONAL, "I'm a receiver. Just joined the multicast group.");
		while (true) {
			sleep(1);
		}
	}
}




void Tester::SendMessage(int level, string msg) {
	ptr_status_proxy->SendMessageToManager(level, msg);
}

bool Tester::IsSender() {
	struct utsname host_name;
	uname(&host_name);
	string nodename = host_name.nodename;
	if (nodename.find("node0") != string::npos ||
			nodename.find("zelda2") != string::npos) {
		return true;
	}
	else
		return false;
}

string Tester::ExecSysCommand(char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return "ERROR";
    char output[4096];
    string result = "";
    while(!feof(pipe)) {
    	int bytes = fread(output, 1, 4095, pipe);
    	output[bytes] = '\0';
    	result += output;
    }
    pclose(pipe);
    return result;
}

