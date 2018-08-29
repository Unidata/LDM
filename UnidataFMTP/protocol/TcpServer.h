/*
 * TcpServer.h
 *
 *  Created on: Sep 15, 2011
 *      Author: jie
 */

#ifndef TCPSERVER_H_
#define TCPSERVER_H_

#include <iostream>
#include <list>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

using namespace std;

class FMTPSender;

class TcpServer {
public:
	TcpServer(int port, FMTPSender* sender);
	~TcpServer();

	void 	Start();
	void 	Listen();
	int 	Accept();
	void 	SendToAll(const void* data, size_t length);
	int		SelectSend(int conn_sock, const void* data, size_t length);
	int 	SelectReceive(int* conn_sock, void* buffer, size_t length);
	int		Receive(int sock_fd, void* buffer, size_t length);
	const list<int>& GetSocketList();

private:
	struct sockaddr_in	server_addr;
	int 		port_num;
	int 		server_sock;
	list<int> 	conn_sock_list;
	int 		max_conn_sock;
	fd_set 		master_read_fds;
	FMTPSender* ptr_sender;

	pthread_t server_thread;
	pthread_mutex_t sock_list_mutex;

	static void* StartServerThread(void* ptr);
	void AcceptClients();
	void SysError(const char* info);
};

#endif /* TCPSERVER_H_ */
