/*
 * StatusProxy.cpp
 *
 *  Created on: Jun 25, 2011
 *      Author: jie
 */

#include "StatusProxy.h"

StatusProxy::StatusProxy(string addr, int port) {
	in_addr_t server_addr = inet_addr(addr.c_str());
	if (server_addr == -1) {
		struct hostent* ptrhost = gethostbyname(addr.c_str());
		if (ptrhost != NULL) {
			server_addr = ((in_addr*)ptrhost->h_addr_list[0])->s_addr;
		}
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family 	 = AF_INET;
	servaddr.sin_addr.s_addr = server_addr;
	servaddr.sin_port 		 = htons(port);

	sockfd = -1;
	isConnected = false;
	proxy_started = false;
	keep_alive = false;
	keep_quiet = false;
	is_restarting = false;
	execution_pid = 0;

	struct utsname host_name;
	uname(&host_name);
	node_id = host_name.nodename;
}

StatusProxy::~StatusProxy() {

}


int StatusProxy::ConnectServer() {
	if ((sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		cout << "socket() error" << endl;
		return -1;
	}

	int res;
	while ((res = connect(sockfd, (SA *) &servaddr, sizeof(servaddr))) < 0) {
		cout << "connect() error" << endl;
		sleep(10);
		//return -1;
	}

	isConnected = true;
	return 1;
}


// Send a message to the remote manager
int StatusProxy::SendMessageToManager(int msg_type, string msg) {
	int res;
	int type = msg_type;
	int length = msg.length();
	if (length == 0)
		return 1;

	if ( (res = write(sockfd, &type, sizeof(type))) < 0) {
		cout << "Error sending message to remote manager. " << endl;
		ReconnectServer();
		return -1;
	}


	if ( (res = write(sockfd, &length, sizeof(length))) < 0) {
		cout << "Error sending message to remote manager. " << endl;
		ReconnectServer();
		return -1;
	}

	if ( (res = write(sockfd, msg.c_str(), length)) < 0) {
		cout << "Error sending message to remote manager. " << endl;
		ReconnectServer();
		return -1;
	}
	else
		return 1;
}


// Read a message from the remote manager
int StatusProxy::ReadMessageFromManager(int& msg_type, string& msg) {
	int res;
	if ((res = read(sockfd, &msg_type, sizeof(msg_type))) <= 0) {
		cout << "read() error." << endl;
		ReconnectServer();
		return -1;
	}

	int msg_length;
	if ((res = read(sockfd, &msg_length, sizeof(msg_length))) <= 0) {
		cout << "read() error." << endl;
		ReconnectServer();
		return -1;
	}

	char buffer[BUFFER_SIZE];
	if ((res = read(sockfd, buffer, msg_length)) < 0) {
		cout << "read() error." << endl;
		ReconnectServer();
		return -1;
	} else {
		buffer[res] = '\0';
		msg = buffer;
		return 1;
	}
}


// Send a message to local parent/child process
int StatusProxy::SendMessageLocal(int msg_type, string msg) {
	int res;
	int type = msg_type;
	int length = msg.length();
	if (length == 0)
		return 1;

	if ((res = write(write_pipe_fd, &type, sizeof(type))) < 0) {
		cout << "Error sending message to local. " << endl;
		return res;
	}


	if ((res = write(write_pipe_fd, &length, sizeof(length))) < 0) {
		cout << "Error sending message to local. " << endl;
		return res;
	}

	if ((res = write(write_pipe_fd, msg.c_str(), length)) < 0) {
		cout << "Error sending message to local. " << endl;
	}
	return res;
}


int StatusProxy::ReadMessageLocal(int& msg_type, string& msg) {
	int res;
	if ((res = read(read_pipe_fd, &msg_type, sizeof(msg_type))) <= 0) {
		cout << "read() error." << endl;
		return res;
	}

	int msg_length;
	if ((res = read(read_pipe_fd, &msg_length, sizeof(msg_length))) <= 0) {
		cout << "read() error." << endl;
		return res;
	}

	char buffer[BUFFER_SIZE];
	if ((res = read(read_pipe_fd, buffer, msg_length)) < 0) {
		cout << "read() error." << endl;
	} else {
		buffer[res] = '\0';
		msg = buffer;
	}
	return res;
}


void StatusProxy::InitializeExecutionProcess() {

}

void StatusProxy::StartExecutionProcess() {
	if (proxy_started) {
		close(read_pipe_fd);
		close(write_pipe_fd);
	}

	if (pipe(read_pipe_fds) < 0) {
		SysError("StatusProxy::StartService()::create read pipe error");
	}

	if (pipe(write_pipe_fds) < 0)
		SysError("StatusProxy::StartService()::create write pipe error");

	if ((execution_pid = fork()) < 0)
		SysError("StatusProxy::StartService()::fork error");
	else if (execution_pid > 0) { // parent
		read_pipe_fd = read_pipe_fds[0];
		write_pipe_fd = write_pipe_fds[1];
		close(read_pipe_fds[1]);
		close(write_pipe_fds[0]);

		keep_alive = true;
		// Start the command execution thread
		if (!proxy_started) {
			proxy_started = true;
			pthread_create(&manager_send_thread, NULL, &StatusProxy::StartManagerSendThread, this);
			pthread_create(&manager_recv_thread, NULL, &StatusProxy::StartManagerReceiveThread, this);

			// Send node identity to the server
			SendNodeInfo();
		}
	} else { //child
		read_pipe_fd = write_pipe_fds[0];
		write_pipe_fd = read_pipe_fds[1];
		close(read_pipe_fds[0]);
		close(write_pipe_fds[1]);

		keep_alive = true;
		proxy_started = true;
		InitializeExecutionProcess();
		pthread_create(&proc_exec_thread, NULL, &StatusProxy::StartProcessExecutionThread, this);
	}
}


void StatusProxy::StopService() {
	keep_alive = false;
	proxy_started = false;
	is_connected = false;
	close(sockfd);
}


void StatusProxy::StartService() {
	if (!isConnected)
		return;
	else
		StartExecutionProcess();

}


// start running the main process execution thread in the child process
void* StatusProxy::StartProcessExecutionThread(void* ptr) {
	((StatusProxy*)ptr)->RunProcessExecutionThread();
	return NULL;
}


void StatusProxy::RunProcessExecutionThread() {
	while (keep_alive) {
		int msg_type;
		string msg;
		ReadMessageLocal(msg_type, msg);

		switch (msg_type) {
		case COMMAND:
		case PARAM_SETTING:
			HandleCommand(msg.c_str());
			break;
		default:
			break;
		}
	}
}



int StatusProxy::SendNodeInfo() {
	SendMessageToManager(NODE_NAME, node_id);
	return 1;
}

string StatusProxy::GetNodeId() {
	return node_id;
}


void* StatusProxy::StartManagerSendThread(void* ptr) {
	((StatusProxy*)ptr)->RunManagerSendThread();
	return NULL;
}


void StatusProxy::RunManagerSendThread() {
	while (keep_alive) {
		// Read the response from the local process
		int msg_type;
		string msg;
		if (ReadMessageLocal(msg_type, msg) <= 0) {
			if (!is_restarting) {
				SendMessageToManager(INFORMATIONAL, "The execution process has crashed. Restarting the process...");
				StartExecutionProcess();
				SendMessageToManager(INFORMATIONAL, "The execution process has been restarted.");
				is_restarting = false;
			}
			continue;
		}

		if (!keep_quiet)
			SendMessageToManager(msg_type, msg);
	}
}


void StatusProxy::SetQuiet(bool quiet) {
	keep_quiet = quiet;
}


void* StatusProxy::StartManagerReceiveThread(void* ptr) {
	((StatusProxy*)ptr)->RunManagerReceiveThread();
	return NULL;
}



void StatusProxy::RunManagerReceiveThread() {
	while (keep_alive) {
		if (!keep_quiet) {
			int msg_type;
			string msg;
			ReadMessageFromManager(msg_type, msg);
			if (msg.compare("Restart") == 0) {
				HandleRestartCommand();
			}
			else if (msg.compare("KeepQuiet") == 0)
				keep_quiet = true;
			else if (msg.compare("BreakQuiet") == 0)
				keep_quiet = false;
			else {
				SendMessageLocal(msg_type, msg);
			}
		}
		else {
			sleep(1);
		}
	}
}


void StatusProxy::ReconnectServer() {
	//if (errno == EINTR)
	//	return;

	close(sockfd);
	is_connected = false;
	ConnectServer();
	SendNodeInfo();
	SendMessageToManager(INFORMATIONAL, "Socket error. Service reconnected.");
}


int StatusProxy::HandleCommand(const char* command) {
	string s = command;
	list<string> parts;
	Split(s, ' ', parts);
	if (parts.size() == 0)
		return 0;
	else
		ExecSysCommand(command);
	return 1;
}


//
void StatusProxy::HandleRestartCommand() {
//	//TODO: Ugly way to close all opened file descriptors (sockets).
//	//How to fix it?
//	for (int i = 3; i < 1000; i++) {
//		close(i);
//	}
//
//	// Restart the emulab test
//	chdir("/users/jieli/bin");
//	execl("/bin/sh", "sh", "/users/jieli/bin/run_starter.sh", (char *) 0);
//	exit(0);

	SendMessageToManager(INFORMATIONAL, "Restarting the execution process...");
	is_restarting = true;
	kill(execution_pid, SIGINT);
	// Restart the process
	chdir("/users/jieli/bin");
	execl("/bin/sh", "sh", "/users/jieli/bin/run_starter.sh", (char *) 0);
	exit(0);
}



int StatusProxy::ExecSysCommand(const char* command) {
	static char output[BUFFER_SIZE];
	FILE* pFile = popen(command, "r");
	if (pFile != NULL) {
		int bytes = fread(output, 1, BUFFER_SIZE, pFile);
		output[bytes] = '\0';
		pclose(pFile);
		SendMessageLocal(COMMAND_RESPONSE, output);
	} else {
		cout << "Cannot get output from execution." << endl;
	}
	return 1;
}


// Divide string s into sub strings separated by the character c
void StatusProxy::Split(string s, char c, list<string>& slist) {
	const char* ptr = s.c_str();
	int start = 0;
	int cur_pos = 0;
	for (; *ptr != '\0'; ptr++) {
		if (*ptr == c) {
			if (cur_pos != start) {
				string subs = s.substr(start, cur_pos - start);
				slist.push_back(subs);
			}
			start = cur_pos + 1;
		}

		cur_pos++;
	}

	if (cur_pos != start) {
		string subs = s.substr(start, cur_pos - start);
		slist.push_back(subs);
	}
}



void StatusProxy::SysError(string s) {
	perror(s.c_str());
	exit(-1);
}
