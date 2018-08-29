/*
 * FMTPSender.cpp
 *
 *  Created on: Jul 21, 2011
 *      Author: jie
 */

#include "FMTPSender.h"


/**
 * This is Jie's original constructor (with the effect of some statements moved
 * to the initialization list). It's kept because it's referenced by
 * `SenderStatusProxy::InitializeExecutionProcess()`. If that method is deleted,
 * then so can this constructor.
 *
 * @param buf_size  Ignored.
 */
FMTPSender::FMTPSender(
    int buf_size)
:
    FMTPComm(),
    retrans_tcp_server(new TcpServer(BUFFER_TCP_SEND_PORT, this)),
    cur_session_id(0),
    /*
     * Set the initial maximum size of the retransmission buffer to be 32 * 8 MB
     * = 256 MB
     */
    max_num_retrans_buffs(32),
    retrans_scheme(RETRANS_SERIAL), // set default retransmission scheme
    num_retrans_threads(1),
    status_proxy(0),
    send_rate_in_mbps(10000) // 10 Gbps initial send rate limit
{
    bzero(&send_stats, sizeof(send_stats));

    rate_shaper.SetRate(send_rate_in_mbps * 1000000.0);

    AccessCPUCounter(&global_timer.hi, &global_timer.lo);

    // Set CPU affinity
    /*cpu_set_t cpu_mask;
    CPU_SET(1, &cpu_mask);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_mask) == -1)
            SysError("FMTPSender::FMTPSender()::sched_setaffinity() error");
     */

    //event_queue_manager = new FmtpEventQueueManager();
}

/**
 * This new constructor takes specifications of the TCP server.
 *
 * @param[in] tcpAddr    Dotted-decimal IPv4 address of the TCP server. The
 *                       caller may delete.
 * @param[in] tcpPort    Port number of the TCP server.
 * @param[in] fileId     Initial product-index. The first multicast file will
 *                       have this as its product-index.
 */
FMTPSender::FMTPSender(
    const string&        tcpAddr,
    const unsigned short tcpPort,
    const u_int32_t      fileId)
:
    FMTPComm(),
    retrans_tcp_server(new TcpServer(BUFFER_TCP_SEND_PORT, this)),
    cur_session_id(fileId),
    /*
     * Set the initial maximum size of the retransmission buffer to 32 * 8 MB =
     * 256 MB
     */
    max_num_retrans_buffs(32),
    retrans_scheme(RETRANS_SERIAL), // set default retransmission scheme
    num_retrans_threads(1),
    status_proxy(0),
    send_rate_in_mbps(10000) // 10 Gbps initial send rate limit
{
    // This constructor is unfinished.

    bzero(&send_stats, sizeof(send_stats));

    rate_shaper.SetRate(send_rate_in_mbps * 1000000.0);

    AccessCPUCounter(&global_timer.hi, &global_timer.lo);
}

FMTPSender::~FMTPSender() {
	delete retrans_tcp_server;
	//delete event_queue_manager;

	map<int, pthread_t*>::iterator it = retrans_thread_map.begin();
	for (; it != retrans_thread_map.end(); it++) {
		delete (it->second);
	}


	map<int, StartRetransThreadInfo*>::iterator it2;
	for (it2 = thread_info_map.begin(); it2 != thread_info_map.end(); it2++) {
		map<uint, int>::iterator fd_it;
		map<uint, int>& fd_map = *(it2->second->ptr_retrans_fd_map);
		for (fd_it = fd_map.begin(); fd_it != fd_map.end(); fd_it++) {
			close(fd_it->second);
		}
		delete it2->second->ptr_retrans_fd_map;
		delete it2->second->ptr_timeout_set;
		delete it2->second;
	}
}


// Set the process scheduling mode to SCHED_RR or SCHED_OTHER (the default process scheduling mode in linux)
void FMTPSender::SetSchedRR(bool is_rr) {
	static int normal_priority = getpriority(PRIO_PROCESS, 0);

	struct sched_param sp;
	if (is_rr) {
		sp.__sched_priority = sched_get_priority_max(SCHED_RR);
		sched_setscheduler(0, SCHED_RR, &sp);

		struct sched_param sp_normal;
		sp_normal.__sched_priority = normal_priority;
		map<int, pthread_t*>::iterator it;
		for (it = retrans_thread_map.begin(); it != retrans_thread_map.end(); it++) {
			pthread_setschedparam(*it->second, SCHED_OTHER, &sp_normal);
		}
	}
	else {
		sp.__sched_priority = normal_priority;
		sched_setscheduler(0, SCHED_OTHER, &sp);
	}
}


void FMTPSender::SetSendRate(int num_mbps) {
	send_rate_in_mbps = num_mbps;
	rate_shaper.SetRate(num_mbps * 1000000.0);
}

int	FMTPSender::GetSendRate() {
	return send_rate_in_mbps;
}


bool FMTPSender::IsTransferFinished(uint msg_id) {
	return metadata.IsTransferFinished(msg_id);
}


void FMTPSender::SetRetransmissionBufferSize(int size_mb) {
	max_num_retrans_buffs = size_mb / 8;
	if (max_num_retrans_buffs == 0)
		max_num_retrans_buffs = 1;
}


void FMTPSender::SetRetransmissionScheme(int scheme) {
	retrans_scheme = scheme;
}

void FMTPSender::SetNumRetransmissionThreads(int num) {
	num_retrans_threads = num;
}

void FMTPSender::SetStatusProxy(StatusProxy* proxy) {
	status_proxy = proxy;
}

void FMTPSender::ResetMetadata() {
	metadata.ClearAllMetadata();

	map<int, StartRetransThreadInfo*>::iterator it;
	for (it = thread_info_map.begin(); it != thread_info_map.end(); it++) {
		map<uint, int>::iterator fd_it;
		map<uint, int>& fd_map = *(it->second->ptr_retrans_fd_map);
		for (fd_it = fd_map.begin(); fd_it != fd_map.end(); fd_it++) {
			close(fd_it->second);
		}
		fd_map.clear();
		it->second->ptr_timeout_set->clear();
	}
}


void FMTPSender::ResetSessionID() {
	cur_session_id = 0;
}

void FMTPSender::SendAllStatistics() {
	char buf[512];
	sprintf(buf, "***** Sender Statistics *****\nTotal Sent Packets:\t\t%u\nTotal Retrans. Packets:\t\t%d\t"
			"Session Sent Packets:\t\t%d\nSession Retrans. Packets:\t\t%d\t"
			"Retrans. Percentage:\t\t%.4f\nTotal Trans. Time:\t\t%.2f sec\nMulticast Trans. Time:\t\t%.2f sec\n"
			"Retrans. Time:\t\t\t%.2f sec\n", send_stats.total_sent_packets, send_stats.total_retrans_packets,
			send_stats.session_sent_packets, send_stats.session_retrans_packets,
			send_stats.session_retrans_percentage, send_stats.session_total_time, send_stats.session_trans_time,
			send_stats.session_retrans_time);

	status_proxy->SendMessageLocal(INFORMATIONAL, buf);
}

void FMTPSender::SendSessionStatistics() {
	char buf[512];
	double send_rate = send_stats.session_sent_bytes / 1000.0 / 1000.0 * 8 / send_stats.session_total_time * SEND_RATE_RATIO;
	sprintf(buf, "***** Session Statistics *****\nTotal Sent Bytes: %u\nTotal Sent Packets: %d\nTotal Retrans. Packets: %d\n"
			"Retrans. Percentage: %.4f\nTotal Trans. Time: %.2f sec\nMulticast Trans. Time: %.2f sec\n"
			"Retrans. Time: %.2f sec\nOverall Throughput: %.2f Mbps\nAvg. CPU Usage: %d\%\n\n", send_stats.session_sent_bytes + send_stats.session_retrans_bytes,
			send_stats.session_sent_packets, send_stats.session_retrans_packets,
			send_stats.session_retrans_percentage, send_stats.session_total_time, send_stats.session_trans_time,
			send_stats.session_retrans_time, send_rate, send_stats.cpu_usage);
	status_proxy->SendMessageLocal(INFORMATIONAL, buf);
}


// Collect experiment results for file transfer from all receivers
void FMTPSender::CollectExpResults() {
	// Send requests to all receivers
	char msg_packet[500];
	FmtpHeader* header = (FmtpHeader*)msg_packet;
	header->session_id = cur_session_id;
	header->seq_number = 0;
	header->data_len = sizeof(FmtpSenderMessage);
	header->flags = FMTP_SENDER_MSG_EXP;

	FmtpSenderMessage* msg = (FmtpSenderMessage*)(msg_packet + FMTP_HLEN);
	msg->msg_type = COLLECT_STATISTICS;
	msg->session_id = cur_session_id;
	msg->data_len = 0;
	retrans_tcp_server->SendToAll(&msg_packet, FMTP_HLEN + sizeof(FmtpSenderMessage));
}


void FMTPSender::ExecuteCommandOnReceivers(string command, int receiver_start, int receiver_end) {
	struct FmtpSenderMessage msg;
	msg.msg_type = EXECUTE_COMMAND;
	msg.session_id = cur_session_id;
	msg.data_len = command.length();
	memcpy(msg.text, command.c_str(), msg.data_len);
	msg.text[msg.data_len] = '\0';

	list<int> sock_list = retrans_tcp_server->GetSocketList();
	list<int>::iterator it;
	int sock_id = 0;
	for (it = sock_list.begin(); it != sock_list.end(); it++) {
		sock_id++;
		if (sock_id >= receiver_start && sock_id <= receiver_end)
			retrans_tcp_server->SelectSend(*it, &msg, sizeof(msg));
		else if (sock_id > receiver_end)
			break;
	}
}


// Clear session related statistics
void FMTPSender::ResetSessionStatistics() {
	send_stats.session_sent_packets = 0;
	send_stats.session_sent_bytes = 0;
	send_stats.session_retrans_packets = 0;
	send_stats.session_retrans_bytes = 0;
	send_stats.session_retrans_percentage = 0.0;
	send_stats.session_total_time = 0.0;
	send_stats.session_trans_time = 0.0;
	send_stats.session_retrans_time = 0.0;
}

void FMTPSender::ResetAllReceiverStats() {
	AccessCPUCounter(&global_timer.hi, &global_timer.lo);

	char msg_packet[500];
	FmtpHeader* header = (FmtpHeader*)msg_packet;
	header->session_id = cur_session_id;
	header->seq_number = 0;
	header->data_len = sizeof(FmtpSenderMessage);
	header->flags = FMTP_SENDER_MSG_EXP;

	FmtpSenderMessage* msg = (FmtpSenderMessage*)(msg_packet + FMTP_HLEN);
	msg->msg_type = RESET_HISTORY_STATISTICS;
	msg->session_id = cur_session_id;
	msg->data_len = 0;
	retrans_tcp_server->SendToAll(&msg_packet, FMTP_HLEN + sizeof(FmtpSenderMessage));
}


void FMTPSender::SetReceiverLossRate(int recver_sock, int loss_rate) {
	char msg_packet[500];
	FmtpHeader* header = (FmtpHeader*) msg_packet;
	header->session_id = cur_session_id;
	header->seq_number = 0;
	header->data_len = sizeof(FmtpSenderMessage);
	header->flags = FMTP_SENDER_MSG_EXP;

	FmtpSenderMessage* msg = (FmtpSenderMessage*) (msg_packet + FMTP_HLEN);
	msg->msg_type = SET_LOSS_RATE;
	msg->session_id = cur_session_id;
	msg->data_len = 0;
	sprintf(msg->text, "%d", loss_rate);
	retrans_tcp_server->SelectSend(recver_sock, &msg_packet, FMTP_HLEN + sizeof(FmtpSenderMessage));
}


/*
 * I don't think it makes sense to have this method be publicly visible
 * because a `FMTPSender` only sends to one one multicast group -- so this
 * method could and should only be called by the constructor to obviate the
 * possibility of it being called multiple times. Note, however, that
 * `SenderStatusProxy::InitializeExecutionProcess()` calls this method *after*
 * constructing a `FMTPSender` and calling `FMTPSender::SetStatusProxy()`.
 * --Steve Emmerson 2014-07-29
 */
/**
 * Joins a multicast group for sending. Starts the TCP server on a separate
 * thread.
 *
 * @param[in] addr  IPv4 address of the multicast group in dotted-decimal
 *                  format.
 * @param[in] port  Port number of the multicast group in native byte order.
 * @retval    1     Always.
 */
// After binding the multicast address, the sender also needs to
// start the thread to accept incoming connection requests
int FMTPSender::JoinGroup(string addr, u_short port) {
	FMTPComm::JoinGroup(addr, port);
	retrans_tcp_server->Start();
	return 1;
}

int	FMTPSender::RestartTcpServer() {
	delete retrans_tcp_server;
	retrans_tcp_server = new TcpServer(BUFFER_TCP_SEND_PORT, this);
	retrans_tcp_server->Start();
	return 1;
}


int	FMTPSender::GetNumReceivers() {
	return retrans_tcp_server->GetSocketList().size();
}

list<int> FMTPSender::GetReceiverTCPSockets() {
	return retrans_tcp_server->GetSocketList();
}


void FMTPSender::RemoveSlowNodes() {
	char buf[FMTP_PACKET_LEN];
	FmtpHeader* header = (FmtpHeader*)buf;
	header->session_id = cur_session_id;
	header->data_len = sizeof(FmtpSenderMessage);
	header->flags = FMTP_SENDER_MSG_EXP;

	FmtpSenderMessage* sender_msg = (FmtpSenderMessage*)(buf + FMTP_HLEN);
	sender_msg->msg_type = SPEED_TEST;
	sender_msg->session_id = cur_session_id;
	sender_msg->data_len = 0;
	retrans_tcp_server->SendToAll(buf, FMTP_HLEN + sizeof(FmtpSenderMessage));
}



void FMTPSender::SendMemoryData(void* data, size_t length) {
	ResetSessionStatistics();
	AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);
	// Send a notification to all receivers before starting the memory transfer
	struct FmtpSenderMessage msg;
	msg.msg_type = MEMORY_TRANSFER_START;
	msg.session_id = cur_session_id;
	msg.data_len = length;
	retrans_tcp_server->SendToAll(&msg, sizeof(msg));

	DoMemoryTransfer(data, length, 0);

	// Record memory data multicast time
	send_stats.session_trans_time = GetElapsedSeconds(cpu_counter);

	// Sleep for a few milliseconds to allow receivers to
	// empty their multicast socket buffers
	//usleep(50000);

	// Send a notification to all receivers to start retransmission
	msg.msg_type = MEMORY_TRANSFER_FINISH;
	cout << "Memory to memory transfer finished." << endl;
	retrans_tcp_server->SendToAll(&msg, sizeof(msg));
	cout << "Start retransmission..." << endl;
	DoMemoryDataRetransmission(data);

	// collect experiment results from receivers
	CollectExpResults();

	// Record total transfer and retransmission time
	send_stats.session_total_time = GetElapsedSeconds(cpu_counter);
	send_stats.session_retrans_time = send_stats.session_total_time - send_stats.session_trans_time;
	send_stats.session_retrans_percentage = send_stats.session_retrans_packets  * 1.0
								/ (send_stats.session_sent_packets + send_stats.session_retrans_packets);
	// Increase the session id for the next transfer
	cur_session_id++;

	SendSessionStatistics();
}



// TODO: Add scheduling logic
void FMTPSender::DoMemoryDataRetransmission(void* data) {
	// first: client socket; second: list of NACK_MSG info
	map<int, list<NACK_MSG> >* missing_packet_map = new map<int, list<NACK_MSG> >();
	ReceiveRetransRequestsSerial(missing_packet_map);
	cout << "Retransmission requests received." << endl;

	int num_socks = missing_packet_map->size();
	if (num_socks == 0)
		return;

	int* sorted_socks = new int[num_socks];
	SortSocketsByShortestJobs(sorted_socks, missing_packet_map);

	char buffer[FMTP_PACKET_LEN];
	char* packet_data = buffer + FMTP_HLEN;
	FmtpHeader* header = (FmtpHeader*) buffer;
	bzero(header, FMTP_HLEN);
	header->session_id = cur_session_id;

	for (int i = 0; i < num_socks; i++) {
		int sock = sorted_socks[i];

		list<NACK_MSG>* retrans_list = &(*missing_packet_map)[sock];
		cout << "Socket " << sock << " has " << retrans_list->size() << " retransmission requests." << endl;

		list<NACK_MSG>::iterator list_it;
		for (list_it = retrans_list->begin(); list_it != retrans_list->end(); list_it++) {
			header->seq_number = list_it->seq_num;
			header->data_len = list_it->data_len;
			memcpy(packet_data, (char*)data + list_it->seq_num, list_it->data_len);
			retrans_tcp_server->SelectSend(sock, buffer, FMTP_HLEN + list_it->data_len);

			// Update statistics
			send_stats.total_retrans_packets++;
			send_stats.total_retrans_bytes += header->data_len;
			send_stats.session_retrans_packets++;
			send_stats.session_retrans_bytes += header->data_len;

			//cout << "Retransmission packet sent. Seq No.: " << list_it->seq_num <<
			//	"    Length: " << list_it->data_len << endl;
		}
	}

	delete missing_packet_map;
	delete[] sorted_socks;
}

// Multicast data in a memory buffer, given the specific start sequence number
void FMTPSender::DoMemoryTransfer(void* data, size_t dataLen, u_int32_t start_seq_num) {
	char 	     buffer[FMTP_HLEN ];
	FmtpHeader* header = (FmtpHeader*) buffer;

	// TODO: replace this with packing of `buffer` IN NETWORK FORM
	header->session_id = cur_session_id;
	header->seq_number = start_seq_num;
	header->flags = FMTP_DATA;

	size_t remained_size = dataLen;
	size_t offset = 0;
	while (remained_size > 0) {
		uint data_size = remained_size < FMTP_DATA_LEN ? remained_size
				: FMTP_DATA_LEN;
		// TODO: replace this with packing of `buffer` IN NETWORK FORM
		header->seq_number = offset + start_seq_num;
		header->data_len = data_size;

		//Get tokens from the rate controller
		rate_shaper.RetrieveTokens(22 + FMTP_HLEN + data_size);
		if (ptr_multicast_comm->SendData(buffer, FMTP_HLEN, data,
		        dataLen) < 0) {
			SysError("FMTPSender::DoMemoryTransfer()::SendPacket() error");
		}

		remained_size -= data_size;
		offset += data_size;

		// Update statistics
		send_stats.total_sent_packets++;
		send_stats.total_sent_bytes += data_size;
		send_stats.session_sent_packets++;
		send_stats.session_sent_bytes += data_size;
	}
}


#define MIN_RETX_TIMEOUT 0.01
uint FMTPSender::SendFile(const char* file_name, int retx_timeout_ratio) {
	ResetSessionStatistics();
	// Increase the session id for new file transfer
	cur_session_id++;

	struct stat file_status;
	stat(file_name, &file_status);
	ulong file_size = file_status.st_size;
	ulong remained_size = file_size;

	// Create metadata for the new file
	FileMessageMetadata* meta = new FileMessageMetadata();
	if (meta == NULL)
		SysError("FMTPSender::SendFile()::cannot allocate memory for FileMessageMetadata");
	meta->msg_id = cur_session_id;
	meta->msg_length = file_size;
	meta->file_name = file_name;
	meta->retx_timeout_ratio = retx_timeout_ratio;

	// add all current receiver sockets into the unfinished receiver map
	list<int> sock_list = retrans_tcp_server->GetSocketList();
	for (list<int>::iterator it = sock_list.begin(); it != sock_list.end(); it++) {
		meta->unfinished_recvers.insert(*it);
	}
	metadata.AddMessageMetadata(meta);

	AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);
	meta->multicast_start_cpu_time = cpu_counter;

	// Send the BOF message to all receivers before starting the file transfer
	char msg_packet[500];
	FmtpHeader* header = (FmtpHeader*)msg_packet;
	header->session_id = cur_session_id;
	header->seq_number = 0;
	header->data_len = sizeof(FmtpSenderMessage);
	header->flags = FMTP_BOF;

	FmtpSenderMessage* msg = (FmtpSenderMessage*)(msg_packet + FMTP_HLEN);
	msg->session_id = cur_session_id;
	msg->msg_type = FILE_TRANSFER_START;
	msg->data_len = file_size;
	msg->time_stamp = GetElapsedSeconds(global_timer);
	strcpy(msg->text, file_name);

	//retrans_tcp_server->SendToAll(&msg_packet, FMTP_HLEN + sizeof(FmtpSenderMessage));
	if (ptr_multicast_comm->SendData(&msg_packet, FMTP_HLEN + sizeof(FmtpSenderMessage), 0, NULL) < 0) {
		SysError("FMTPSender::SendFile()::SendData() error");
	}


	//PerformanceCounter cpu_info(100);
	//cpu_info.SetCPUFlag(true);
	//cpu_info.Start();

	//cout << "Start file transferring..." << endl;
	// Transfer the file using memory mapped I/O
	int fd = open(file_name, O_RDONLY);
	if (fd < 0) {
		SysError("FMTPSender()::SendFile(): File open error!");
	}
	char* buffer;
	off_t offset = 0;
	while (remained_size > 0) {
		uint map_size = remained_size < MAX_MAPPED_MEM_SIZE ? remained_size
				: MAX_MAPPED_MEM_SIZE;
		buffer = (char*) mmap(0, map_size, PROT_READ, MAP_FILE | MAP_SHARED, fd,
				offset);
		if (buffer == MAP_FAILED) {
			SysError("FMTPSender::SendFile()::mmap() error");
		}

		DoMemoryTransfer(buffer, map_size, offset);

		munmap(buffer, map_size);

		offset += map_size;
		remained_size -= map_size;
	}

	// Record memory data multicast time
	send_stats.session_trans_time = meta->msg_length * 8.0 / 100000000.0; //GetElapsedSeconds(cpu_counter);
	meta->stats.session_trans_time = send_stats.session_trans_time;
	double default_timeout = meta->stats.session_trans_time * (meta->retx_timeout_ratio / 100.0 + 1.0);
	meta->retx_timeout_seconds = default_timeout > (MIN_RETX_TIMEOUT + meta->stats.session_retrans_time) ?
									default_timeout : (MIN_RETX_TIMEOUT + meta->stats.session_retrans_time);


	// TODO: remove this in real implementation
	// For test ONLY: clear system cache before doing retransmission
	// system("sudo sync && sudo echo 3 > /proc/sys/vm/drop_caches");


	// Send a notification to all receivers to start retransmission
	header->flags = FMTP_EOF;
	header->data_len = 0;
	//retrans_tcp_server->SendToAll(header, FMTP_HLEN);
	if (ptr_multicast_comm->SendData(header, FMTP_HLEN, 0, NULL) < 0) {
		SysError("FMTPSender::SendFile()::SendData() error");
	}

	close(fd);

	//SendSessionStatistics();
	return meta->msg_id;
}





//==================== Parallel Retransmission Threads Management Functions ===================
void FMTPSender::StartNewRetransThread(int sock_fd) {
	pthread_t * t = new pthread_t();

	retrans_thread_map[sock_fd] = t;
	retrans_switch_map[sock_fd] = true;
	retrans_finish_map[sock_fd] = false;

	StartRetransThreadInfo* retx_thread_info = new StartRetransThreadInfo();
	retx_thread_info->sender_ptr 			= this;
	retx_thread_info->sock_fd 				= sock_fd;
	retx_thread_info->ptr_retrans_fd_map 	= new map<uint, int>();
	retx_thread_info->ptr_timeout_set 		= new set<uint>();
	thread_info_map[sock_fd] = retx_thread_info;
	pthread_create(t, NULL, &FMTPSender::StartRetransThread, retx_thread_info);
}

void* FMTPSender::StartRetransThread(void* ptr) {
	StartRetransThreadInfo* info = (StartRetransThreadInfo*)ptr;
	info->sender_ptr->RunRetransThread(info->sock_fd, *info->ptr_retrans_fd_map, *info->ptr_timeout_set);
	return NULL;
}


// The execution function for the retransmission thread
void FMTPSender::RunRetransThread(int sock, map<uint, int>& retrans_fd_map, set<uint>& timeout_set) {
	int sock_fd = sock;

	map<uint, int>::iterator it;

	char recv_buf[FMTP_PACKET_LEN];
	FmtpHeader* recv_header = (FmtpHeader*)recv_buf;
	char* recv_packet_data = recv_buf + FMTP_HLEN;
	FmtpRetransRequest* retx_request = (FmtpRetransRequest*)recv_packet_data;

	char send_buf[FMTP_PACKET_LEN];
	FmtpHeader* send_header = (FmtpHeader*)send_buf;
	char* send_packet_data = send_buf + FMTP_HLEN;

	while (true) {
		if (retrans_tcp_server->Receive(sock_fd, recv_header, FMTP_HLEN) <= 0) {
			SysError("FMTPSender::RunRetransThread()::receive header error");
		}

		// Handle a retransmission request
		if (recv_header->flags & FMTP_RETRANS_REQ) {
			if (retrans_tcp_server->Receive(sock_fd, retx_request, recv_header->data_len) < 0) {
				SysError("FMTPSender::RunRetransThread()::receive retx request data error");
			}

			MessageMetadata* meta = metadata.GetMetadata(retx_request->msg_id);
			if (meta == NULL) {
				//cout << "Error: could not find metadata for file " << retx_request->msg_id << endl;
				continue;
			} else if (timeout_set.find(retx_request->msg_id) != timeout_set.end()) {
				continue;
			}

			// check whether the retransmission for the file has already time out
			if (GetElapsedSeconds(meta->multicast_start_cpu_time) > meta->retx_timeout_seconds) {
				//cout << "Retx timeout for file " << retx_request->msg_id << ".  Elapsed Time: "
				//		<< GetElapsedSeconds(meta->multicast_start_cpu_time) << "    Timeout: " << meta->retx_timeout_seconds << endl;
				send_header->session_id = retx_request->msg_id;
				send_header->flags = FMTP_RETRANS_TIMEOUT;
				send_header->data_len = 0;
				retrans_tcp_server->SelectSend(sock_fd, send_buf, FMTP_HLEN);

				timeout_set.insert(retx_request->msg_id);
			}
			else if (meta->is_disk_file) {	// is disk file transfer
				FileMessageMetadata* file_meta = (FileMessageMetadata*)meta;

				// get the file descriptor to read data from the file
				int fd;
				if ( (it = retrans_fd_map.find(recv_header->session_id)) != retrans_fd_map.end()) {
					fd = it->second;
				}
				else {
					if ( (fd = open(file_meta->file_name.c_str(), O_RDONLY)) < 0)
						SysError("FMTPSender::RunRetransThread() file open error");
					else
						retrans_fd_map[recv_header->session_id] = fd;
				}


				// send the missing blocks to the receiver
				lseek(fd, retx_request->seq_num, SEEK_SET);
				size_t remained_size = retx_request->data_len;
				size_t curr_pos = retx_request->seq_num;
				send_header->session_id = recv_header->session_id;
				send_header->flags = FMTP_RETRANS_DATA;
				while (remained_size > 0) {
					size_t data_length =
							remained_size > FMTP_DATA_LEN ? FMTP_DATA_LEN
									: remained_size;
					send_header->seq_number = curr_pos;
					send_header->data_len = data_length;

					read(fd, send_packet_data, send_header->data_len);
					retrans_tcp_server->SelectSend(sock_fd, send_buf, FMTP_HLEN + send_header->data_len);

					curr_pos += data_length;
					remained_size -= data_length;

					// Update statistics
					send_stats.total_retrans_packets++;
					send_stats.total_retrans_bytes += send_header->data_len;
					//file_meta->stats.session_retrans_packets++;
					//file_meta->stats.session_retrans_bytes += header->data_len;
				}
			}
			else {	// is memory data transfer

			}
		}
		else if (recv_header->flags & FMTP_RETRANS_END) {
			if (retrans_tcp_server->Receive(sock_fd, retx_request, recv_header->data_len) < 0) {
				SysError("FMTPSender::RunRetransThread()::receive retx end msg error");
			}

			// send back the retransmission end message to the receiver
			send_header->session_id = recv_header->session_id;
			send_header->seq_number = 0;
			send_header->data_len = 0;
			send_header->flags = FMTP_RETRANS_END;
			retrans_tcp_server->SelectSend(sock_fd, send_header, FMTP_HLEN);

			map<uint, int>::iterator it = retrans_fd_map.find(recv_header->session_id);
			if ( it != retrans_fd_map.end() ) {
				close(it->second);
				retrans_fd_map.erase(it);
			}

			if ( timeout_set.find(recv_header->session_id) != timeout_set.end() )
				timeout_set.erase(recv_header->session_id);

			// mark the completion of retransmission to one receiver
			metadata.RemoveFinishedReceiver(recv_header->session_id, sock_fd);
		}
		else if (recv_header->flags & FMTP_HISTORY_STATISTICS) {
			char* buf = new char[recv_header->data_len + 1];
			if (retrans_tcp_server->Receive(sock_fd, buf, recv_header->data_len) < 0) {
				break;
			}

			buf[recv_header->data_len] = '\0';
			status_proxy->SendMessageLocal(EXP_RESULT_REPORT, buf);
			delete[] buf;
			cout << "Received a history statistics from socket " << sock_fd << endl;
		}
	}

	cout << "Retransmission thread exited for socket " << sock_fd  << endl;
}









//==================================== Old Functions ====================================
void FMTPSender::SendFileBufferedIO(const char* file_name) {
	PerformanceCounter cpu_info(50);
	cpu_info.SetCPUFlag(true);
	cpu_info.Start();

	ResetSessionStatistics();
	AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);

	struct stat file_status;
	stat(file_name, &file_status);
	ulong file_size = file_status.st_size;
	ulong remained_size = file_size;

	// Send a notification to all receivers before starting the memory transfer
	struct FmtpSenderMessage msg;
	msg.session_id = cur_session_id;
	msg.msg_type = FILE_TRANSFER_START;
	msg.data_len = file_size;
	strcpy(msg.text, file_name);
	retrans_tcp_server->SendToAll(&msg, sizeof(msg));

	//cout << "Start file transferring..." << endl;
	// Transfer the file using memory mapped I/O
	int fd = open(file_name, O_RDWR);
	if (fd < 0) {
		SysError("FMTPSender()::SendFile(): File open error!");
	}
	char* buffer = (char *)malloc(FMTP_DATA_LEN);
	off_t offset = 0;
	while (remained_size > 0) {
		uint read_size = remained_size < FMTP_DATA_LEN ? remained_size
				: FMTP_DATA_LEN;
		ssize_t res = read(fd, buffer, read_size);
		if (res < 0) {
			SysError("FMTPSender::SendFileBufferedIO()::read() error");
		}

		DoMemoryTransfer(buffer, read_size, offset);
		offset += read_size;
		remained_size -= read_size;
	}
	free(buffer);

	// Record memory data multicast time
	send_stats.session_trans_time = GetElapsedSeconds(cpu_counter);

	AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);
	// Send a notification to all receivers to start retransmission
	msg.msg_type = FILE_TRANSFER_FINISH;
	retrans_tcp_server->SendToAll(&msg, sizeof(msg));

	//cout << "File transfer finished. Start retransmission..." << endl;

	if (retrans_scheme == RETRANS_SERIAL)
		DoFileRetransmissionSerial(fd);
	else if (retrans_scheme == RETRANS_SERIAL_RR)
		DoFileRetransmissionSerialRR(fd);
	else if (retrans_scheme == RETRANS_PARALLEL)
		DoFileRetransmissionParallel(file_name);


	close(fd);

	// collect experiment results from receivers
	CollectExpResults();

	// Record total transfer and retransmission time
	send_stats.session_retrans_time = GetElapsedSeconds(cpu_counter); //send_stats.session_total_time - send_stats.session_trans_time;
	send_stats.session_total_time = send_stats.session_trans_time + send_stats.session_retrans_time; //GetElapsedSeconds(cpu_counter);
	send_stats.session_retrans_percentage = send_stats.session_retrans_packets  * 1.0
									/ (send_stats.session_sent_packets + send_stats.session_retrans_packets);
	// Increase the session id for the next transfer
	cur_session_id++;
	SendSessionStatistics();

	cpu_info.Stop();
}




//=========== Functions related to TCP file transfer ================
void FMTPSender::TcpSendMemoryData(void* data, size_t length) {
	AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);
	// Send a notification to all receivers before starting the memory transfer
	char msg_packet[500];
	FmtpHeader* header = (FmtpHeader*)msg_packet;
	header->session_id = cur_session_id;
	header->seq_number = 0;
	header->data_len = sizeof(FmtpSenderMessage);
	header->flags = FMTP_SENDER_MSG_EXP;

	FmtpSenderMessage* msg = (FmtpSenderMessage*)(msg_packet + FMTP_HLEN);
	msg->msg_type = TCP_MEMORY_TRANSFER_START;
	msg->session_id = cur_session_id;
	msg->data_len = length;
	retrans_tcp_server->SendToAll(&msg_packet, FMTP_HLEN + sizeof(FmtpSenderMessage));

	retrans_tcp_server->SendToAll(data, length);

	// Record memory data multicast time
	double trans_time = GetElapsedSeconds(cpu_counter);
	double send_rate = length / 1024.0 / 1024.0 * 8.0 * 1514.0 / 1460.0 / trans_time;
	char str[256];
	sprintf(str, "***** TCP Send Info *****\nTotal transfer time: %.2f\nThroughput: %.2f\n", trans_time, send_rate);
	status_proxy->SendMessageLocal(EXP_RESULT_REPORT, str);


	cur_session_id++;
}


struct TcpThreadInfo {
	FMTPSender* ptr;
	int	 sock_fd;
	char file_name[256];
};


void FMTPSender::TcpSendFile(const char* file_name) {
	AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);

	struct stat file_status;
	stat(file_name, &file_status);
	ulong file_size = file_status.st_size;
	ulong remained_size = file_size;

	// Send a notification to all receivers before starting the memory transfer
	char msg_packet[500];
	FmtpHeader* header = (FmtpHeader*)msg_packet;
	header->session_id = cur_session_id;
	header->seq_number = 0;
	header->data_len = sizeof(FmtpSenderMessage);
	header->flags = FMTP_SENDER_MSG_EXP;

	FmtpSenderMessage* msg = (FmtpSenderMessage*)(msg_packet + FMTP_HLEN);
	msg->msg_type = TCP_FILE_TRANSFER_START;
	msg->session_id = cur_session_id;
	msg->data_len = file_size;
	strcpy(msg->text, file_name);
	retrans_tcp_server->SendToAll(&msg_packet, FMTP_HLEN + sizeof(FmtpSenderMessage));


	PerformanceCounter cpu_info(100);
	cpu_info.SetCPUFlag(true);
	cpu_info.Start();


	cout << "Start TCP file transferring..." << endl;
	list<int> sock_list = retrans_tcp_server->GetSocketList();
	list<TcpThreadInfo*> thread_info_list;
	list<pthread_t*> thread_list;
	int file_name_len = strlen(file_name);
	for (list<int>::iterator it = sock_list.begin(); it != sock_list.end(); it++) {
		TcpThreadInfo* info = new TcpThreadInfo();
		info->ptr = this;
		info->sock_fd = *it;
		memcpy(info->file_name, file_name, file_name_len);
		thread_info_list.push_back(info);

		pthread_t * t = new pthread_t();
		pthread_create(t, NULL, &FMTPSender::StartTcpSendThread, info);
		thread_list.push_back(t);
	}

	for (list<pthread_t*>::iterator it = thread_list.begin(); it != thread_list.end(); it++) {
		pthread_join(**it, NULL);
	}

	for (list<pthread_t*>::iterator it = thread_list.begin(); it != thread_list.end(); it++) {
		delete (*it);
	}

	for (list<TcpThreadInfo*>::iterator it = thread_info_list.begin(); it != thread_info_list.end(); it++) {
		delete (*it);
	}

	cpu_info.Stop();
	int cpu_usage = cpu_info.GetAverageCpuUsage();

	// Record memory data multicast time
	double trans_time = GetElapsedSeconds(cpu_counter);
	double send_rate = file_size / 1024.0 / 1024.0 * 8.0 * 1514.0 / 1460.0 / trans_time;
	char str[256];
	sprintf(str, "***** TCP Send Info *****\nTotal transfer time: %.2f seconds\nThroughput: %.2f Mbps\nAvg. CPU Usage: %d\%\n",
					trans_time, send_rate, cpu_usage);
	status_proxy->SendMessageLocal(INFORMATIONAL, str);


	cur_session_id++;
}


// Thread functions for TCP transfer
void* FMTPSender::StartTcpSendThread(void* ptr) {
	TcpThreadInfo* info_ptr = (TcpThreadInfo*)ptr;
	info_ptr->ptr->RunTcpSendThread(info_ptr->file_name, info_ptr->sock_fd);
	return NULL;
}

void FMTPSender::RunTcpSendThread(const char* file_name, int sock_fd) {
	cout << "Start sending file " << file_name << " to socket " << sock_fd << endl;
	struct stat file_status;
	stat(file_name, &file_status);
	ulong file_size = file_status.st_size;
	ulong remained_size = file_size;

	// Transfer the file using memory mapped I/O
	int fd = open(file_name, O_RDWR);
	char* buffer = (char* )malloc(MAX_MAPPED_MEM_SIZE);
	off_t offset = 0;
	while (remained_size > 0) {
		int map_size = remained_size < MAX_MAPPED_MEM_SIZE ? remained_size
				: MAX_MAPPED_MEM_SIZE;
		read(fd, buffer, map_size);

		//retrans_tcp_server->SendToAll(buffer, map_size);
		retrans_tcp_server->SelectSend(sock_fd, buffer, map_size);
		offset += map_size;
		remained_size -= map_size;
	}
	close(fd);
	free(buffer);
}





///===============================  Obseleted Functions =================================
void FMTPSender::DoFileRetransmissionSerial(int fd) {
	// first: client socket; second: list of NACK_MSG info
	map<int, list<NACK_MSG> >* missing_packet_map = new map<int, list<NACK_MSG> >();
	ReceiveRetransRequestsSerial(missing_packet_map);

	int num_socks = missing_packet_map->size();
	if (num_socks == 0)
			return;

	int* sorted_socks = new int[num_socks];
	SortSocketsByShortestJobs(sorted_socks, missing_packet_map);

	list<FmtpRetransBuffer *> retrans_cache_list;
	FmtpRetransBuffer * ptr_cache = new FmtpRetransBuffer();
	retrans_cache_list.push_back(ptr_cache);

	FmtpHeader* header;
	char* packet_data;
	// first: sequence number of a packet; second: pointer to the packet in the cache
	map<uint32_t, char*>* packet_map = new map<uint32_t, char*>();
	map<uint32_t, char*>::iterator packet_map_it;

	for (int i = 0; i < num_socks; i++) {
		int sock = sorted_socks[i];

		list<NACK_MSG>* retrans_list = &(*missing_packet_map)[sock];
		cout << "Socket " << sock << " has " << retrans_list->size() << " retransmission requests." << endl;

		// double start_time = GetElapsedSeconds(cpu_counter);
		list<NACK_MSG>::iterator list_it;
		for (list_it = retrans_list->begin(); list_it != retrans_list->end(); list_it++) {
			// First check if the packet is already in the cache
			if ( (packet_map_it = packet_map->find(list_it->seq_num)) != packet_map->end()) {
				retrans_tcp_server->SelectSend(sock, packet_map_it->second, FMTP_HLEN + list_it->data_len);
				continue;
			}

			// If not, read the packet in from the disk file
			if (ptr_cache->cur_pos == ptr_cache->end_pos) {
				if (retrans_cache_list.size() > max_num_retrans_buffs) {
					list<FmtpRetransBuffer *>::iterator it;
					for (it = retrans_cache_list.begin(); it != retrans_cache_list.end(); it++) {
						delete (*it);
					}

					retrans_cache_list.clear();
					packet_map->clear();
				}

				ptr_cache = new FmtpRetransBuffer();
				retrans_cache_list.push_back(ptr_cache);
			}

			header = (FmtpHeader *)ptr_cache->cur_pos;
			header->session_id = cur_session_id;
			header->seq_number = list_it->seq_num;
			header->data_len = list_it->data_len;

			packet_data = ptr_cache->cur_pos + FMTP_HLEN;
			lseek(fd, list_it->seq_num, SEEK_SET);
			read(fd, packet_data, list_it->data_len);
			retrans_tcp_server->SelectSend(sock, ptr_cache->cur_pos, FMTP_HLEN + list_it->data_len);

			(*packet_map)[list_it->seq_num] = ptr_cache->cur_pos;
			ptr_cache->cur_pos += FMTP_PACKET_LEN;

			// Update statistics
			send_stats.total_retrans_packets++;
			send_stats.total_retrans_bytes += header->data_len;
			send_stats.session_retrans_packets++;
			send_stats.session_retrans_bytes += header->data_len;
		}

//		double read_finish_time = GetElapsedSeconds(cpu_counter);
//		cout << "Time to read all retransmission data: " << (read_finish_time - start_time) << " Seconds" << endl;
//
//
//		for (list_it = retrans_list->begin(); list_it != retrans_list->end(); list_it++) {
//			if ( (packet_map_it = packet_map->find(list_it->seq_num)) != packet_map->end()) {
//				retrans_tcp_server->SelectSend(sock, packet_map_it->second, FMTP_HLEN + list_it->data_len);
//			}
//		}
//		double send_finish_time = GetElapsedSeconds(cpu_counter);
//		cout << "Time to send all retransmission data: " << (send_finish_time - read_finish_time) << " Seconds" << endl;
	}

	// Clean up
	delete missing_packet_map;
	delete[] sorted_socks;
	list<FmtpRetransBuffer *>::iterator it;
	for (it = retrans_cache_list.begin(); it != retrans_cache_list.end(); it++) {
		delete (*it);
	}
	delete packet_map;
}


void FMTPSender::ReceiveRetransRequestsSerial(map<int, list<NACK_MSG> >* missing_packet_map) {
	int client_sock;
	FmtpRetransMessage retrans_msg;
	int msg_size = sizeof(retrans_msg);

	list<int> sock_list = retrans_tcp_server->GetSocketList();
	while (!sock_list.empty()) {
		int bytes = retrans_tcp_server->SelectReceive(&client_sock, &retrans_msg, msg_size);
		if (retrans_msg.num_requests == 0 || bytes <= 0) {
			sock_list.remove(client_sock);
			continue;
		}

		for (int i = 0; i < retrans_msg.num_requests; i++) {
			NACK_MSG packet_info;
			packet_info.seq_num = retrans_msg.seq_numbers[i];
			packet_info.data_len = retrans_msg.data_lens[i];
			(*missing_packet_map)[client_sock].push_back(packet_info);
		}
	}
}



// Use selection sort to reorder the sockets according to their retransmission request numbers
// Should use better sorting algorithm is this becomes a bottleneck
void FMTPSender::SortSocketsByShortestJobs(int* ptr_socks,
			const map<int, list<NACK_MSG> >* missing_packet_map) {
	map<int, int> num_sockets;
	map<int, list<NACK_MSG> >::const_iterator it;
	for (it = missing_packet_map->begin(); it != missing_packet_map->end(); it++) {
		num_sockets[it->first] = it->second.size();
	}

	map<int, int>::iterator num_it;
	int min_sock;
	int min_num;
	int pos = 0;
	while (!num_sockets.empty()) {
		min_num = 0x7fffffff;
		for (num_it = num_sockets.begin(); num_it != num_sockets.end(); num_it++) {
			if (min_num > num_it->second) {
				min_sock = num_it->first;
				min_num = num_it->second;
			}
		}

		ptr_socks[pos++] = min_sock;
		num_sockets.erase(min_sock);
	}

}


//
void FMTPSender::DoFileRetransmissionSerialRR(int fd) {
	// first: sequence number
	// second: list of sockets
	map<NACK_MSG, list<int> >* missing_packet_map = new map<NACK_MSG, list<int> >();
	ReceiveRetransRequestsSerialRR(missing_packet_map);


	char packet_buf[FMTP_PACKET_LEN];
	FmtpHeader* header = (FmtpHeader*)packet_buf;
	char* packet_data = packet_buf + FMTP_HLEN;

	map<NACK_MSG, list<int> >::iterator it;
	for (it = missing_packet_map->begin(); it != missing_packet_map->end(); it++) {
		const NACK_MSG& nack_msg = it->first;
		list<int>& sock_list = it->second;

		list<int>::iterator sock_it;
		for (sock_it = sock_list.begin(); sock_it != sock_list.end(); sock_it++) {
			header->session_id = cur_session_id;
			header->seq_number = nack_msg.seq_num;
			header->data_len = nack_msg.data_len;

			lseek(fd, header->seq_number, SEEK_SET);
			read(fd, packet_data, header->data_len);
			retrans_tcp_server->SelectSend(*sock_it, packet_buf, FMTP_HLEN + header->data_len);

			// Update statistics
			send_stats.total_retrans_packets++;
			send_stats.total_retrans_bytes += header->data_len;
			send_stats.session_retrans_packets++;
			send_stats.session_retrans_bytes += header->data_len;
		}
	}

	delete missing_packet_map;
}


void FMTPSender::ReceiveRetransRequestsSerialRR(map <NACK_MSG, list<int> >* missing_packet_map) {
	int client_sock;
	FmtpRetransMessage retrans_msg;
	int msg_size = sizeof(retrans_msg);

	list<int> sock_list = retrans_tcp_server->GetSocketList();
	while (!sock_list.empty()) {
		int bytes = retrans_tcp_server->SelectReceive(&client_sock, &retrans_msg, msg_size);
		if (retrans_msg.num_requests == 0 || bytes <= 0) {
			sock_list.remove(client_sock);
			continue;
		}

		for (int i = 0; i < retrans_msg.num_requests; i++) {
			NACK_MSG packet_info;
			packet_info.seq_num = retrans_msg.seq_numbers[i];
			packet_info.data_len = retrans_msg.data_lens[i];
			(*missing_packet_map)[packet_info].push_back(client_sock);
		}
	}
}


struct RetransThreadStartInfo {
	const char*	file_name;
	FMTPSender* sender_ptr;
	map<int, list<NACK_MSG> >* missing_packet_map;

	RetransThreadStartInfo(const char* fname): file_name(fname) {}
};

//
void FMTPSender::DoFileRetransmissionParallel(const char* file_name) {
	// first: client socket; second: list of NACK_MSG info
	map<int, list<NACK_MSG> >* missing_packet_map = new map<int, list<NACK_MSG> > ();
	ReceiveRetransRequestsSerial(missing_packet_map);

	int num_socks = missing_packet_map->size();
	if (num_socks == 0)
		return;

	int* sorted_socks = new int[num_socks];
	SortSocketsByShortestJobs(sorted_socks, missing_packet_map);

	retrans_sock_list.clear();
	for (int i = 0; i < num_socks; i++) {
		retrans_sock_list.push_back(sorted_socks[i]);
	}


	RetransThreadStartInfo start_info(file_name);
	start_info.sender_ptr = this;
	start_info.missing_packet_map = missing_packet_map;
	//start_info.file_name = file_name;

	pthread_mutex_init(&sock_list_mutex, NULL);
	pthread_t* retrans_threads = new pthread_t[num_retrans_threads];
	for (int i = 0; i < num_retrans_threads; i++) {
		pthread_create(&retrans_threads[i], NULL, &FMTPSender::StartRetransmissionThread, &start_info);
	}

	for (int i = 0; i < num_retrans_threads; i++) {
		pthread_join(retrans_threads[i], NULL);
	}

	delete missing_packet_map;
	delete[] retrans_threads;
	pthread_mutex_destroy(&sock_list_mutex);
}


void* FMTPSender::StartRetransmissionThread(void* ptr) {
	RetransThreadStartInfo* start_info = (RetransThreadStartInfo*)ptr;
	start_info->sender_ptr->RunRetransmissionThread(start_info->file_name, start_info->missing_packet_map);
	return NULL;
}


void FMTPSender::RunRetransmissionThread(const char* file_name, map<int, list<NACK_MSG> >* missing_packet_map) {
	int fd = open(file_name, O_RDONLY);
	if (fd < 0) {
		SysError("FMTPSender()::RunRetransmissionThread(): File open error!");
	}

	char packet_buf[FMTP_PACKET_LEN];
	FmtpHeader* header = (FmtpHeader*)packet_buf;
	char* packet_data = packet_buf + FMTP_HLEN;

	while (true) {
		int sock;
		list<NACK_MSG>* msg_list;
		pthread_mutex_lock(&sock_list_mutex);
		if (retrans_sock_list.size() == 0) {
			pthread_mutex_unlock(&sock_list_mutex);
			return;
		}
		else {
			sock = retrans_sock_list.front();
			retrans_sock_list.pop_front();
			pthread_mutex_unlock(&sock_list_mutex);
		}

		msg_list = &(*missing_packet_map)[sock];
		list<NACK_MSG>::iterator it;
		for (it = msg_list->begin(); it != msg_list->end(); it++) {
			header->session_id = cur_session_id;
			header->seq_number = it->seq_num;
			header->data_len = it->data_len;

			lseek(fd, header->seq_number, SEEK_SET);
			read(fd, packet_data, header->data_len);
			retrans_tcp_server->SelectSend(sock, packet_buf, FMTP_HLEN + header->data_len);

			// Update statistics
			send_stats.total_retrans_packets++;
			send_stats.total_retrans_bytes += header->data_len;
			send_stats.session_retrans_packets++;
			send_stats.session_retrans_bytes += header->data_len;
		}
	}
}
