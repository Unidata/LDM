/*
 * ReceiverStatusProxy.h
 *
 *  Created on: Jun 28, 2011
 *      Author: jie
 */

#ifndef RECEIVERSTATUSPROXY_H_
#define RECEIVERSTATUSPROXY_H_

#include "../CommUtil/StatusProxy.h"
#include "MVCTPReceiver.h"
#include <sys/time.h>

class ReceiverStatusProxy : public StatusProxy {
public:
	ReceiverStatusProxy(string addr, int port, string group_addr, int mvctp_port, int buff_size);
	~ReceiverStatusProxy();

	virtual int 	HandleCommand(const char* command);

protected:
	virtual void 	InitializeExecutionProcess();

private:
	MVCTPReceiver* ptr_receiver;
	string 		mvctp_group_addr;
	int			mvctp_port_num;
	int			buffer_size;

	pthread_t receiver_thread;
	static void* StartReceiverThread(void* ptr);
	void RunReceiverThread();

	void	ConfigureEnvironment();
};

#endif /* RECEIVERSTATUSPROXY_H_ */
