/*
 * VcmtpEventQueueManager.h
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#ifndef VCMTPEVENTQUEUEMANAGER_H_
#define VCMTPEVENTQUEUEMANAGER_H_

#include "vcmtp.h"
#include "EventQueue.h"

// maximum size of an event object
#define MAX_EVENT_LENGTH  4096

// event types
#define VCMTP_MSG_SEND_SUCCESS  	1
#define VCMTP_MSG_SEND_FAILED		2
#define VCMTP_MSG_RECV_SUCCESS		3
#define VCMTP_MSG_RECV_FAILED		4
#define VCMTP_BOF_RECVED			5

struct VcmtpMsgTransferEvent {
	int			event_type;
	u_int16_t 	transfer_type;
	u_int32_t 	msg_id;
	char		msg_name[MAX_FILE_NAME_LENGTH];
	long long 	msg_length;
};

/*struct VcmtpMsgBofRecvedEvent {
	u_int16_t 	transfer_type;
	u_int32_t 	msg_id;
	char		msg_name[MAX_FILE_NAME_LENGTH];
	long long 	msg_length;
};*/



class VcmtpEventQueueManager {
public:
	VcmtpEventQueueManager();
	virtual ~VcmtpEventQueueManager();

	// functions for adding and retrieving events
	// produced by VCMTP and consumed by the application
	int GetNextEvent(VcmtpMsgTransferEvent* event);
	int	AddNewEvent(VcmtpMsgTransferEvent& event);

	// functions for adding and retrieving message transfer events
	// produced by the application and consumed by VCMTP
	int GetNextTransferEvent(VcmtpMsgTransferEvent* event);
	int AddNewTransferEvent(VcmtpMsgTransferEvent& event);

private:
	EventQueue* app_notify_queue;
	EventQueue* transfer_request_queue;
};

#endif /* VCMTPEVENTQUEUEMANAGER_H_ */
