/*
 * Tester.h
 *
 *  Created on: Jun 29, 2011
 *      Author: jie
 */

#ifndef TESTER_H_
#define TESTER_H_

#include "vcmtp.h"
#include "VCMTPSender.h"
#include "VCMTPReceiver.h"
#include "SenderStatusProxy.h"
#include "ReceiverStatusProxy.h"

class Tester {
public:
	Tester();
	~Tester();
	void StartTest();
	void SendMessage(int level, string msg);

private:
	StatusProxy* 	ptr_status_proxy;
	VCMTPSender* 	ptr_vcmtp_sender;
	VCMTPReceiver* 	ptr_vcmtp_receiver;

	bool IsSender();
	string ExecSysCommand(char* cmd);

};

#endif /* TESTER_H_ */
