/*
 * Tester.h
 *
 *  Created on: Jun 29, 2011
 *      Author: jie
 */

#ifndef TESTER_H_
#define TESTER_H_

#include "mvctp.h"
#include "MVCTPSender.h"
#include "MVCTPReceiver.h"
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
	MVCTPSender* 	ptr_mvctp_sender;
	MVCTPReceiver* 	ptr_mvctp_receiver;

	bool IsSender();
	string ExecSysCommand(char* cmd);

};

#endif /* TESTER_H_ */
