/*
 * SenderStatusProxy.h
 *
 *  Created on: Jun 28, 2011
 *      Author: jie
 */

#ifndef SENDERSTATUSPROXY_H_
#define SENDERSTATUSPROXY_H_

#include "../CommUtil/StatusProxy.h"
#include "FMTPSender.h"
#include "ExperimentManager.h"
#include "ExperimentManager2.h"
#include "LdmIntegrator.h"
#include <sys/time.h>


enum MsgTag {
	MSG_START = 1010101010,
	MSG_END = 1111111111
};

enum TransferMsgType {
	STRING_TRANSFER,
	MEMORY_TRANSFER,
	FILE_TRANSFER
};

struct TransferMessage {
	 TransferMsgType	msg_type;
	 size_t 			data_len;
	 char       		file_name[30];
};


class SenderStatusProxy : public StatusProxy {
public:
	SenderStatusProxy(string addr, int port, string group_addr, int fmtp_port, int buff_size);
	~SenderStatusProxy();

	virtual int HandleCommand(const char* command);
	virtual int SendMessageLocal(int msg_type, string msg);

	// public functions for experiments
	int 	GenerateDataFile(string file_name, ulong bytes);
	void 	TransferFile(string file_name);
	void 	TransferDirectory(string dir_name);
	void 	SetSendRate(int rate_mbps);
	void	SetTxQueueLength(int length);
	void    SetRetransmissionBufferSize(int size_mb);
	int		GetRetransmissionTimeoutRatio();

protected:
	int 	HandleSendCommand(list<string>& slist);
	int 	HandleTcpSendCommand(list<string>& slist);

	int 	TransferString(string str, bool send_out_packets);
	int 	TransferMemoryData(int size);
	int 	TcpTransferMemoryData(int size);
	void 	TcpTransferFile(string file_name);
	void 	SendMemoryData(void* buffer, size_t length);

	virtual void InitializeExecutionProcess();

private:
	FMTPSender* ptr_sender;
	string 		fmtp_group_addr;
	int			fmtp_port_num;
	int			buffer_size;
	LdmIntegrator* integrator;
	// experiment specific parameters
	int			file_retx_timeout_ratio;

	ExperimentManager  	exp_manager;
	ExperimentManager2	exp_manager2;

	void	ConfigureEnvironment();
};


#endif /* SENDERSTATUSPROXY_H_ */
