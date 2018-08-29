/*
 * FmtpEventQueueManager.h
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#ifndef FMTPEVENTQUEUEMANAGER_H_
#define FMTPEVENTQUEUEMANAGER_H_

#include "fmtp.h"
#include "EventQueue.h"

// maximum size of an event object
#define MAX_EVENT_LENGTH  4096

// event types
#define FMTP_MSG_SEND_SUCCESS  	1
#define FMTP_MSG_SEND_FAILED		2
#define FMTP_MSG_RECV_SUCCESS		3
#define FMTP_MSG_RECV_FAILED		4
#define FMTP_BOF_RECVED			5

struct FmtpMsgTransferEvent {
	int			event_type;
	u_int16_t 	transfer_type;
	u_int32_t 	msg_id;
	char		msg_name[MAX_FILE_NAME_LENGTH];
	long long 	msg_length;
};

/*struct FmtpMsgBofRecvedEvent {
	u_int16_t 	transfer_type;
	u_int32_t 	msg_id;
	char		msg_name[MAX_FILE_NAME_LENGTH];
	long long 	msg_length;
};*/



class FmtpEventQueueManager {
public:
	FmtpEventQueueManager();
	virtual ~FmtpEventQueueManager();

	// functions for adding and retrieving events
	// produced by FMTP and consumed by the application
	int GetNextEvent(FmtpMsgTransferEvent* event);
	int	AddNewEvent(FmtpMsgTransferEvent& event);

	// functions for adding and retrieving message transfer events
	// produced by the application and consumed by FMTP
	int GetNextTransferEvent(FmtpMsgTransferEvent* event);
	int AddNewTransferEvent(FmtpMsgTransferEvent& event);

private:
	EventQueue* app_notify_queue;
	EventQueue* transfer_request_queue;
};

#endif /* FMTPEVENTQUEUEMANAGER_H_ */
