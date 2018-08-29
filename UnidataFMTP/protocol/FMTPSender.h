/*
 * FMTPSender.h
 *
 *  Created on: Jul 21, 2011
 *      Author: jie
 */

#ifndef FMTPSENDER_H_
#define FMTPSENDER_H_

#include "fmtp.h"
#include "FMTPComm.h"
#include "TcpServer.h"
#include "FmtpSenderMetadata.h"
#include "FmtpEventQueueManager.h"
#include "../CommUtil/PerformanceCounter.h"
#include "../CommUtil/StatusProxy.h"
#include "../CommUtil/RateShaper.h"
#include <pthread.h>

struct FmtpSenderStats {
	uint	cpu_usage;		// in percentage
	uint 	total_sent_packets;
	uint	total_sent_bytes;
	uint 	total_retrans_packets;
	uint	total_retrans_bytes;
	uint 	session_sent_packets;
	uint	session_sent_bytes;
	uint 	session_retrans_packets;
	uint	session_retrans_bytes;
	double	session_retrans_percentage;
	double	session_total_time;
	double	session_trans_time;
	double 	session_retrans_time;
};


struct FmtpSenderConfig {
	string 	multicast_addr;
	int 	send_rate;
	int 	max_num_receivers;
	int		tcp_port;
};

enum TransferType  {MEMORY_TO_MEMORY_TRANSFER = 1, DISK_TO_DISK_TRANSFER};

struct FmtpMulticastTaskInfo {
	TransferType	type;
	char*	ptr_memory_data;
	char 	file_name[256];
};


#define	BUFFER_PACKET_SIZE	5480
struct FmtpRetransBuffer {
	char 	buffer[BUFFER_PACKET_SIZE * FMTP_PACKET_LEN];  // 8MB buffer size
	char*	cur_pos;
	char*	end_pos;

	FmtpRetransBuffer() {
		cur_pos = buffer;
		end_pos = buffer + BUFFER_PACKET_SIZE * FMTP_PACKET_LEN;
	}
};


struct StartRetransThreadInfo {
	FMTPSender* sender_ptr;
	int	sock_fd;
	map<uint, int>* ptr_retrans_fd_map;		// Opened file descriptor map for the retransmission.  Format: <msg_id, file_descriptor>
	set<uint>* 		ptr_timeout_set;		// A set that includes the unique id of all timeout messages
};


class FMTPSender : public FMTPComm {
public:
	FMTPSender(int buf_size);
	FMTPSender(
            const string&        tcpAddr,
            const unsigned short tcpPort,
            const u_int32_t      fileId = 0);
	virtual ~FMTPSender();

	void 	SetSchedRR(bool is_rr);
	void 	SetStatusProxy(StatusProxy* proxy);
	void    SetRetransmissionBufferSize(int size_mb);
	void	SetRetransmissionScheme(int scheme);
	void	SetNumRetransmissionThreads(int num);
	int 	JoinGroup(string addr, u_short port);
	void	RemoveSlowNodes();
	int		RestartTcpServer();
	int		GetNumReceivers();
	list<int>	GetReceiverTCPSockets();
	void 	SetSendRate(int num_mbps);
	int		GetSendRate();

	void 	SendAllStatistics();
	void	ResetMetadata();
	void	ResetSessionID();
	void 	SendSessionStatistics();
	void	ResetSessionStatistics();
	void	ResetAllReceiverStats();
	void	SetReceiverLossRate(int recver_sock, int loss_rate);
	// For memory-to-memory data tranfer
	void 	SendMemoryData(void* data, size_t length);
	// For disk-to-disk data transfer
	uint 	SendFile(const char* file_name, int retx_timeout_ratio = 1000000);
	bool	IsTransferFinished(uint msg_id);
	void 	SendFileBufferedIO(const char* file_name);
	// Send data using TCP connections, for performance comparison
	void 	TcpSendMemoryData(void* data, size_t length);
	void 	TcpSendFile(const char* file_name);
	void 	CollectExpResults();
	void	ExecuteCommandOnReceivers(string command, int receiver_start, int receiver_end);
	void 	StartNewRetransThread(int sock_fd);

private:
	TcpServer*			retrans_tcp_server;
	u_int32_t			cur_session_id;		// the session ID for a new transfer
	FmtpSenderStats	send_stats;			// data transfer statistics
	CpuCycleCounter		cpu_counter, global_timer;		// counter for elapsed CPU cycles
	StatusProxy*		status_proxy;
	RateShaper			rate_shaper;
	int					max_num_retrans_buffs;
	int					retrans_scheme;
	int					num_retrans_threads;


	FmtpSenderMetadata			metadata;
	//map<uint, int> 				retrans_fd_map;		// Format: <msg_id, file_descriptor>
	FmtpMulticastTaskInfo 		multicast_task_info;
	map<int, StartRetransThreadInfo*> thread_info_map;
	//FmtpEventQueueManager* 	event_queue_manager;

	static void* StartRetransThread(void* ptr);
	void RunRetransThread(int sock_fd, map<uint, int>& retrans_fd_map, set<uint>& timeout_set);
	map<int, pthread_t*> retrans_thread_map;	//first: socket id;  second: pthread_t pointer
	map<int, bool>	retrans_switch_map; 		//first: socket_id;  second: swtich to allow/disallow retransmission on-the-fly
	map<int, bool>	retrans_finish_map;			//first: socket_id;  second: whether the message retransmission has finished


	void DoMemoryTransfer(void* data, size_t length, u_int32_t start_seq_num);
	void DoMemoryDataRetransmission(void* data);

	void DoFileRetransmissionSerial(int fd);
	void ReceiveRetransRequestsSerial(map<int, list<NACK_MSG> >* missing_packet_map);

	void DoFileRetransmissionSerialRR(int fd);
	void ReceiveRetransRequestsSerialRR(map <NACK_MSG, list<int> >* missing_packet_map);

	void DoFileRetransmissionParallel(const char* file_name);


	void SortSocketsByShortestJobs(int* ptr_socks, const map<int, list<NACK_MSG> >* missing_packet_map);

	void DoTcpMemoryTransfer(void* data, size_t length, u_int32_t start_seq_num);

	static void* StartRetransmissionThread(void* ptr);
	void	RunRetransmissionThread(const char* file_name, map<int, list<NACK_MSG> >* missing_packet_map);
	pthread_mutex_t sock_list_mutex;
	list<int>	retrans_sock_list;

	int send_rate_in_mbps;

	// Thread functions for TCP transfer
	static void* StartTcpSendThread(void* ptr);
	void	RunTcpSendThread(const char* file_name, int sock_fd);
};

#endif /* FMTPSENDER_H_ */
