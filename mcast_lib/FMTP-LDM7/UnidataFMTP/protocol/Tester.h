/*
 * Tester.h
 *
 *  Created on: Jun 29, 2011
 *      Author: jie
 */

#ifndef TESTER_H_
#define TESTER_H_

#include "fmtp.h"
#include "FMTPSender.h"
#include "FMTPReceiver.h"
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
	FMTPSender* 	ptr_fmtp_sender;
	FMTPReceiver* 	ptr_fmtp_receiver;

	bool IsSender();
	string ExecSysCommand(char* cmd);

};

#endif /* TESTER_H_ */
