/*
 * FMTPReceiver.h
 *
 *  Created on: Jul 21, 2011
 *      Author: jie
 */

#ifndef FMTPRECEIVER_H_
#define FMTPRECEIVER_H_

#include "ReceivingApplicationNotifier.h"
#include "fmtp.h"
#include "FMTPComm.h"
#include "TcpClient.h"
#include "FmtpSenderMetadata.h"
#include "../CommUtil/PerformanceCounter.h"
#include "../CommUtil/StatusProxy.h"

#include <exception>

// statistic information of FMTPReceiver
struct FmtpReceiverStats {
	uint	current_msg_id;
	uint 	total_recv_packets;
	uint	total_recv_bytes;
	uint 	total_retrans_packets;
	uint	total_retrans_bytes;
	uint 	session_recv_packets;
	uint	session_recv_bytes;
	uint 	session_retrans_packets;
	uint	session_retrans_bytes;
	double	session_retrans_percentage;
	double	session_total_time;
	double	session_trans_time;
	double 	session_retrans_time;

	// new stat fields
	PerformanceCounter	cpu_monitor;
	CpuCycleCounter		reset_cpu_timer;
	int					num_recved_files;
	int					num_failed_files;
	double				last_file_recv_time;
	vector<string>		session_stats_vec;
};


// receiver side status (not statistic info)
struct MessageReceiveStatus {
	uint 		msg_id;
	string		msg_name;
	union {
			void* 	mem_buffer;
			int		file_descriptor;
		};
	int			retx_file_descriptor;
	bool		is_multicast_done;
	long long 	msg_length;
	uint		current_offset;
	long long	multicast_packets;
	long long	multicast_bytes;
	long long 	retx_packets;
	long long 	retx_bytes;
	bool		recv_failed;
	CpuCycleCounter 	start_time_counter;
	double		send_time_adjust;
	double		multicast_time;
};

/****************************************************************************
 * Struct Name: FmtpReceiverConfig
 *
 * Description: Configuration structure providing information for user
 * application (e.g. LDM) which contains configs of the receiver object.
 ***************************************************************************/
struct FmtpReceiverConfig {
	string 	multicast_addr;
	string  sender_ip_addr;
	int		sender_tcp_port;
	int		receive_mode;
};


// FMTPReceiver is the main class that communicates with the FMTP Sender
class FMTPReceiver : public FMTPComm {
public:
	FMTPReceiver(int buf_size);
	FMTPReceiver(
	        std::string&                  tcpAddr,
	        const unsigned short          tcpPort,
	        RecvAppNotifier* notifier);
	~FMTPReceiver();

	int 	JoinGroup(string addr, u_short port);
	int	ConnectSenderOnTCP();
	void 	Start();
	void 	StartReceivingThread();
	void	SetSchedRR(bool is_rr);

	void 	SetPacketLossRate(int rate);
	int 	GetPacketLossRate();
	void	SetBufferSize(size_t size);
	void 	SendHistoryStats();
	void	ResetHistoryStats();
	void	SendHistoryStatsToSender();
	void 	SendSessionStatistics();
	void	ResetSessionStatistics();
	void 	AddSessionStatistics(uint msg_id);
	void	SendSessionStatisticsToSender();
	void	ExecuteCommand(char* command);
	void 	SetStatusProxy(StatusProxy* proxy);
	const   struct FmtpReceiverStats GetBufferStats();
	void	RunReceivingThread();
	void	stop();


private:
	TcpClient*	        retrans_tcp_client;
	// used in the select() system call
	int		            max_sock_fd;        // the maximum file descriptors allowed
	int 		        multicast_sock;
	int		            retrans_tcp_sock;
	fd_set		        read_sock_set;
	ofstream 	        retrans_info;

	int 		        packet_loss_rate;
	uint			    session_id;
	FmtpReceiverStats 	recv_stats;
	CpuCycleCounter		cpu_counter, global_timer;
	StatusProxy*		status_proxy;

	PerformanceCounter 	cpu_info;
	bool			    time_diff_measured;
	double 			    time_diff;

	/**
	 * Initializes this instance.
	 */
	void    init();

	void 	ReconnectSender();

	// Memory-to-memory data tranfer
	void 	ReceiveMemoryData(const FmtpSenderMessage & msg, char* mem_data);
	void 	DoMemoryDataRetransmission(char* mem_data, const list<FmtpNackMessage>& nack_list);
	// Disk-to-disk data transfer
	void 	ReceiveFileBufferedIO(const FmtpSenderMessage & transfer_msg);
	void 	ReceiveFileMemoryMappedIO(const FmtpSenderMessage & transfer_msg);
	void 	DoFileRetransmission(int fd, const list<FmtpNackMessage>& nack_list);

	void 	DoAsynchronousWrite(int fd, size_t offset, char* data_buffer, size_t length);
	static void HandleAsyncWriteCompletion(sigval_t sigval);
	void	CheckReceivedFile(const char* file_name, size_t length);
	void	SendNackMessages(const list<FmtpNackMessage>& nack_list);
	void 	HandleMissingPackets(list<FmtpNackMessage>& nack_list, uint current_offset, uint received_seq);

	// Functions related to TCP data transfer
	void 	TcpReceiveMemoryData(const FmtpSenderMessage & msg, char* mem_data);
	void 	TcpReceiveFile(const FmtpSenderMessage & transfer_msg);


	// Receive status map for all active files
	map<uint, MessageReceiveStatus> recv_status_map;

	// File descriptor map for the main RECEIVING thread. Format: <msg_id, file_descriptor>
	map<uint, int> 	recv_file_map;

	char read_ahead_buffer[FMTP_PACKET_LEN];
	FmtpHeader* read_ahead_header;
	char* read_ahead_data;

	/**
	 * Notifies the receiving application about file events.
	 */
	RecvAppNotifier* const notifier;

	/**
	 * This class implements the default method for notifying the receiving
	 * application about file events by using the notification queue within
	 * the FMTPReceiver.
	 */
	class BatchedNotifier : public RecvAppNotifier {
	public:
	    BatchedNotifier(FMTPReceiver& receiver) : receiver(receiver) {};
            void notify_of_bof(FmtpMessageInfo& info);
            void notify_of_bomd(FmtpMessageInfo& info);
            void notify_of_eof(FmtpMessageInfo& info);
            void notify_of_eomd(FmtpMessageInfo& info);
            void notify_of_missed_product(uint32_t prodId);
	private:
            FMTPReceiver&      receiver;
	};

    /************************************************************************
     * Class Name: PerFileNotifier
     *
     * Description: Notifier for per-file mode. It sends a status report msg
     * to user application (e.g. LDM) and then waits for a response from user
     * application about how to proceed.
     ***********************************************************************/
	class PerFileNotifier : public RecvAppNotifier {
	public:
	    PerFileNotifier(FMTPReceiver& receiver) : receiver(receiver) {};
            bool notify_of_bof(FmtpSenderMessage& msg);
            void notify_of_eof(FmtpSenderMessage& msg);
            void notify_of_missed_file(FmtpSenderMessage& msg);
	private:
            FMTPReceiver&      receiver;
	};

	//*********************** Main receiving thread functions ***********************
	pthread_t	recv_thread;
	static void* StartReceivingThread(void* ptr);
	void	HandleMulticastPacket();
	void	HandleUnicastPacket();
	void	HandleBofMessage(FmtpSenderMessage& sender_msg);
	void  	HandleEofMessage(uint msg_id);
	void	PrepareForFileTransfer(FmtpSenderMessage& sender_msg);
	void	HandleSenderMessage(FmtpSenderMessage& sender_msg);
	void	AddRetxRequest(uint msg_id, uint current_offset, uint received_seq);

	//*********************** Retransmission thread functions ***********************
	void 	StartRetransmissionThread();
	static void* StartRetransmissionThread(void* ptr);
	void	RunRetransmissionThread();
	pthread_t			retrans_thread;
	pthread_mutex_t 	retrans_list_mutex;
	bool				keep_retrans_alive;
	list<FmtpRetransRequest> 	retrans_list;

	std::string     tcpAddr; /* Address of TCP server for missed data */
	unsigned short  tcpPort; /* Port number of TCP server for missed data */
	int		fmtp_seq_num;
	size_t	        total_missing_bytes;
	size_t	        received_retrans_bytes;
	bool	        is_multicast_finished;
	bool	        retrans_switch;		// a switch that allows/disallows on-the-fly retransmission
};

#endif /* FMTPRECEIVER_H_ */
