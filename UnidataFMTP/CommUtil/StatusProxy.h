/*
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 * @licence: Published under GPLv3
 *
 * @filename: StatusProxy.h
 *
 * @history:
 *      Created  : Sep 15, 2011
 *      Author   : jie
 *      Modified : Sep 22, 2014
 *      Author   : Shawn <sc7cq@virginia.edu>
 */

#ifndef STATUSPROXY_H_
#define STATUSPROXY_H_

#include "CommUtil.h"
#include <list>
#include <signal.h>
#include <sys/types.h>

using namespace std;

typedef struct sockaddr SA;


class StatusProxy {
public:
	StatusProxy(string addr, int port);
	virtual ~StatusProxy();

	int ConnectServer(); // connect to the server ip given to the constructor.
	int StartService();
	void StopService();
	string GetNodeId(); // get nodename of local machine.
	int SendMessageToManager(int msg_type, string msg);
	int ReadMessageFromManager(int& msg_type, string& msg);
	virtual int SendMessageLocal(int msg_type, string msg);
	int ReadMessageLocal(int& msg_type, string& msg);

	virtual int HandleCommand(const char* command);

	void SetQuiet(bool quiet);

protected:
	string node_id;
	int sockfd;
	struct sockaddr_in servaddr;

	bool isConnected;
	bool proxy_started;
	bool keep_alive;
	bool keep_quiet;
	bool is_connected;
	bool is_restarting;

	// pipes used for message communication
	int		read_pipe_fds[2];
	int		write_pipe_fds[2];
	int		read_pipe_fd;
	int		write_pipe_fd;

	int 	execution_pid;

	pthread_t manager_send_thread;
	static void* StartManagerSendThread(void* ptr);
	void RunManagerSendThread();

	pthread_t manager_recv_thread;
	static void* StartManagerReceiveThread(void* ptr);
	void RunManagerReceiveThread();

	pthread_t proc_exec_thread;
	static void* StartProcessExecutionThread(void* ptr);
	void RunProcessExecutionThread();
	void StartExecutionProcess();
	virtual void InitializeExecutionProcess();

	void ReconnectServer();
	void HandleRestartCommand();
	int ExecSysCommand(const char* command);

	int SendNodeInfo();
	void Split(string s, char c, list<string>& slist);
	void SysError(string s);


private:

};


#endif /* STATUSPROXY_H_ */
