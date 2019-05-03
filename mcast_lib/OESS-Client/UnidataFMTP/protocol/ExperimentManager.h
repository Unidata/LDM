/*
 * ExperimentManager.h
 *
 *  Created on: Jan 2, 2012
 *      Author: jie
 */

#ifndef EXPERIMENTMANAGER_H_
#define EXPERIMENTMANAGER_H_

//#include "SenderStatusProxy.h"
#include <iostream>
#include <string>
#include <vector>
#include <stdio.h>

class SenderStatusProxy;
class FMTPSender;


const int HIGH_SPEED_EXP = 1;
const int HIGH_SPEED_RETRANS_EXP = 2;
const int LOW_SPEED_EXP = 3;

class ExperimentManager {
public:
	ExperimentManager();
	~ExperimentManager();

	void StartExperiment(SenderStatusProxy* sender_proxy, FMTPSender* sender);
	void StartExperimentRetrans(SenderStatusProxy* sender_proxy, FMTPSender* sender);
	void StartExperimentLowSpeed(SenderStatusProxy* sender_proxy, FMTPSender* sender);

	void HandleExpResults(string msg);

	ulong 	GetFileSize() {return file_size;}
	int 	GetSendRate() {return send_rate;}

private:
	ulong file_size;
	int send_rate;
	int txqueue_len;
	int retrans_buff_size;
	int buff_size;
	int num_test_nodes;
	int finished_node_count;
	ofstream result_file;
	int retrans_scheme;
	int num_retrans_thread;
	int exp_type;

	void DoSpeedTest(SenderStatusProxy* sender_proxy, FMTPSender* sender);
	void DoLowSpeedExperiment(SenderStatusProxy* sender_proxy, FMTPSender* sender);
};


#endif /* EXPERIMENTMANAGER_H_ */
