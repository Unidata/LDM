/*
 * VcmtpSenderMetadata.h
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#ifndef VCMTPSENDERMETADATA_H_
#define VCMTPSENDERMETADATA_H_

#include <pthread.h>
#include <set>
#include "vcmtp.h"

using namespace std;

struct MessageTransferStats {
	uint 	session_sent_packets;
	uint	session_sent_bytes;
	uint 	session_retrans_packets;
	uint	session_retrans_bytes;
	double	session_retrans_percentage;
	double	session_total_time;
	double	session_trans_time;
	double 	session_retrans_time;

	MessageTransferStats() {
		session_sent_packets = 0;
		session_sent_bytes = 0;
		session_retrans_packets = 0;
		session_retrans_bytes = 0;
		session_retrans_percentage = 0.0;
		session_total_time = 0.0;
		session_trans_time = 0.0;
		session_retrans_time = 0.0;
	}
};


enum MsgTransferStatus {BOF_NOT_RECEIVED, IN_NORMAL_TRANSFER, FINISHED};

/*struct MessageMetadata {
	MsgTransferStatus 	status;
	VcmtpMessageInfo 	msg_info;
};
*/

#define	DOUBLE_MAX				99999999999.0
#define METADATA_SIZE_LIMIT		10000
struct MessageMetadata {
	u_int32_t 				msg_id;
	bool					ignore_file;
    bool 					is_disk_file;          		// true for disk file transfer, false for memory transfer
    long long 				msg_length;    				// the length of the file
    CpuCycleCounter			multicast_start_cpu_time;	// the CPU time counter when multicast is started
    int		  				retx_timeout_ratio;			// the retransmission timeout in terms of a ratio of the total multicast time
    double					retx_timeout_seconds;
    MessageTransferStats 	stats;
    void* 					info;                   	// the pointer to any auxiliary data that the user wants to pass
    set<int> 				unfinished_recvers;			// unfinished receiver set with socket ID as the key


    MessageMetadata(): msg_id(0), ignore_file(false),
    		is_disk_file(true), msg_length(0), retx_timeout_ratio(20),
    		retx_timeout_seconds(DOUBLE_MAX), info(NULL) {}

    virtual ~MessageMetadata() {

    }
};


struct FileMessageMetadata : public MessageMetadata {
	string 	file_name;
	int  	file_descriptor;			//when initialized, its value should be -1

	FileMessageMetadata() : MessageMetadata(),
			file_name(""), file_descriptor(-1) {}

	~FileMessageMetadata() {
		if (file_descriptor > 0)
			close(file_descriptor);
	}
};


struct MemoryMessageMetadata: public MessageMetadata {
	void* buffer;
};






class VcmtpSenderMetadata {
public:
	VcmtpSenderMetadata();
	~VcmtpSenderMetadata();

	void 	AddMessageMetadata(MessageMetadata* ptr_meta);
	void 	RemoveMessageMetadata(uint msg_id);
	void 	ClearAllMetadata();
	MessageMetadata* GetMetadata(uint msg_id);
	bool 	IsTransferFinished(uint msg_id);
	int 	GetFileDescriptor(uint msg_id);
	void 	RemoveFinishedReceiver(uint msg_id, int sock_fd);

private:
	map<uint, MessageMetadata*> metadata_map;	// the map from message id to the pointer to the message metadata
	pthread_rwlock_t 	map_lock;
	pthread_rwlock_t 	metadata_lock;
};

#endif /* VCMTPSENDERMETADATA_H_ */
