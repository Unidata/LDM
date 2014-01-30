/*
 * vcmtp.h
 *
 *  Created on: Jun 28, 2011
 *      Author: jie
 */

#ifndef VCMTP_H_
#define VCMTP_H_


#define _LARGEFILE_SOURCE 1
#define _LARGEFILE64_SOURCE 1
#define _FILE_OFFSET_BITS   64

#include "../CommUtil/Timer.h"
#include "ConfigInfo.h"
#include <aio.h>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <list>
#include <netdb.h>
#include <stdarg.h>
#include <string>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/utsname.h>
//#include <linux/if_arp.h>


using namespace std;


//global functions
void VCMTPInit();
void SysError(string s);
void Log(char* format, ...);
void CreateNewLogFile(const char* file_name);

// Linux network struct typedefs
typedef struct sockaddr SA;
typedef struct ifreq	IFREQ;

typedef struct VcmtpHeader {
	u_int16_t	src_port;
	u_int16_t	dest_port;
	u_int32_t 	session_id;
	u_int32_t	seq_number;
	u_int32_t	data_len;
	u_int32_t	flags;
} VCMTP_HEADER, *PTR_VCMTP_HEADER;


// VCMTP Header Flags
const u_int32_t VCMTP_DATA = 0x00000000;			// data packet
const u_int32_t VCMTP_BOF = 0x00000001;				// begin of file
const u_int32_t VCMTP_EOF = 0x00000002;				// end of file
const u_int32_t VCMTP_SENDER_MSG_EXP = 0x00000004;	// sender messages used for experiment
const u_int32_t VCMTP_RETRANS_REQ = 0x00000008;		// retransmission request
const u_int32_t VCMTP_RETRANS_DATA = 0x00000010; 	// retransmission data
const u_int32_t VCMTP_RETRANS_END = 0x00000020;
const u_int32_t VCMTP_RETRANS_TIMEOUT = 0x00000040; // retransmission timeout message
const u_int32_t VCMTP_BOF_REQ = 0x00000080;     	// BOF request
const u_int32_t VCMTP_HISTORY_STATISTICS = 0x00000100;


/************ The BOF/EOF message data types ****************/
// maximum length of a file name
#define MAX_FILE_NAME_LENGTH 	1024

// transfer types
#define MEMORY_TO_MEMORY	1
#define DISK_TO_DISK		2

// message information
struct VcmtpMessageInfo {
	u_int16_t 	transfer_type;
	u_int32_t 	msg_id;
	long long 	msg_length;
	char		msg_name[MAX_FILE_NAME_LENGTH];
};


// buffer entry for a single packet
typedef struct PacketBuffer {
	int32_t 	packet_id;
	size_t		packet_len;
	size_t		data_len;
	char*		eth_header;
	char*		vcmtp_header;
	char*		data;
	char* 		packet_buffer;
} BUFFER_ENTRY, * PTR_BUFFER_ENTRY;

struct VcmtpNackMsg {
	int32_t 	proto;
	int32_t 	packet_id;
};


const int MAX_NACK_IDS = 10;
struct NackMsg {
	int32_t 	proto;
	int32_t 	num_missing_packets;
	int32_t 	packet_ids[MAX_NACK_IDS];
};

struct NackMsgInfo {
	int32_t		packet_id;
	clock_t		time_stamp;
	short		num_retries;
	bool		packet_received;
};

// Macros
#define MAX(a, b)  ((a) > (b) ? (a) : (b))

const static bool is_debug = true;

// Constant values used for the protocol
const static string group_id = "224.1.2.3";
const static unsigned char group_mac_addr[6] = {0x01, 0x00, 0x5e, 0x01, 0x02, 0x03};
const static ushort vcmtp_port = 123;
const static ushort BUFFER_UDP_SEND_PORT = 12345;
const static ushort BUFFER_UDP_RECV_PORT = 12346;
const static ushort BUFFER_TCP_SEND_PORT = 12347;
const static ushort BUFFER_TCP_RECV_PORT = 12348;
const static int PORT_NUM = 11001;
const static int BUFF_SIZE = 10000;

const static ushort VCMTP_PROTO_TYPE = 0x0001;
// Force maximum VCMTP packet length to be 1460 bytes so that it won't cause fragmentation
// when using TCP for packet retransmission
const static int VCMTP_ETH_FRAME_LEN = 1460 + ETH_HLEN;
const static int VCMTP_PACKET_LEN = 1460; //ETH_FRAME_LEN - ETH_HLEN;
const static int VCMTP_HLEN = sizeof(VCMTP_HEADER);
const static int VCMTP_DATA_LEN = VCMTP_PACKET_LEN - sizeof(VCMTP_HEADER); //ETH_FRAME_LEN - ETH_HLEN - sizeof(VCMTP_HEADER);

// parameters for VCMTP over UDP
static const int UDP_VCMTP_PACKET_LEN = 1460;
static const int UDP_VCMTP_HLEN = sizeof(VCMTP_HEADER);
static const int UDP_VCMTP_DATA_LEN = 1200 - sizeof(VCMTP_HEADER);
static const int UDP_PACKET_LEN = ETH_DATA_LEN;

static const int INIT_RTT	= 50;		// in milliseconds


// parameters for data transfer
static const double SEND_RATE_RATIO = (VCMTP_PACKET_LEN + 8 + ETH_HLEN) * 1.0 / VCMTP_DATA_LEN;
static const int MAX_NUM_RECEIVERS = 200;
static const int MAX_MAPPED_MEM_SIZE = 4096 * VCMTP_DATA_LEN;

// message types for VCMTP data transfer
static const int STRING_TRANSFER_START = 1;
static const int STRING_TRANSFER_FINISH = 2;
static const int MEMORY_TRANSFER_START = 3;
static const int MEMORY_TRANSFER_FINISH = 4;
static const int FILE_TRANSFER_START = 5;
static const int FILE_TRANSFER_FINISH = 6;
static const int DO_RETRANSMISSION = 7;

// message types related to TCP transfer (for performance comparison)
static const int TCP_MEMORY_TRANSFER_START = 8;
static const int TCP_MEMORY_TRANSFER_FINISH = 9;
static const int TCP_FILE_TRANSFER_START = 10;
static const int TCP_FILE_TRANSFER_FINISH = 11;
static const int SPEED_TEST = 12;
static const int COLLECT_STATISTICS = 13;
static const int EXECUTE_COMMAND = 14;
static const int RESET_HISTORY_STATISTICS = 15;
static const int SET_LOSS_RATE = 16;


struct VcmtpSenderMessage {
	int32_t		msg_type;
	uint32_t	session_id;
	uint32_t 	data_len;
	char       	text[256];
	double		time_stamp;
};

struct VcmtpRetransRequest {
	u_int32_t	msg_id;
	u_int32_t 	seq_num;
	u_int32_t	data_len;
};


const int MAX_NUM_NACK_REQ = 50;
struct VcmtpRetransMessage {
	int32_t		num_requests;
	u_int32_t	seq_numbers[MAX_NUM_NACK_REQ];
	u_int32_t	data_lens[MAX_NUM_NACK_REQ];
};

typedef struct VcmtpNackMessage {
	u_int32_t 	seq_num;
	u_int32_t	data_len;
} NACK_MSG, * PTR_NACK_MSG;



// three different retransmission schemes
static const int RETRANS_SERIAL = 1;  		// single retransmission thread, shortest job first
static const int RETRANS_SERIAL_RR = 2;  	// single retransmission thread, send missing blocks one by one to all receivers
static const int RETRANS_PARALLEL = 3;		// parallel retransmission threads


bool operator==(const VcmtpNackMessage& l, const VcmtpNackMessage& r);
bool operator<(const VcmtpNackMessage& l, const VcmtpNackMessage& r);


class VCMTP {
public:
	static FILE*  log_file;
	static bool is_log_enabled;
};

#endif /* VCMTP_H_ */
