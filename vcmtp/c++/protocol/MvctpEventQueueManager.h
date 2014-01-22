/*
 * MvctpEventQueueManager.h
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#ifndef MVCTPEVENTQUEUEMANAGER_H_
#define MVCTPEVENTQUEUEMANAGER_H_

#include "mvctp.h"
#include "EventQueue.h"

// maximum size of an event object
#define MAX_EVENT_LENGTH  4096

// event types
#define MVCTP_MSG_SEND_SUCCESS  	1
#define MVCTP_MSG_SEND_FAILED		2
#define MVCTP_MSG_RECV_SUCCESS		3
#define MVCTP_MSG_RECV_FAILED		4
#define MVCTP_BOF_RECVED			5

struct MvctpMsgTransferEvent {
	int			event_type;
	u_int16_t 	transfer_type;
	u_int32_t 	msg_id;
	char		msg_name[MAX_FILE_NAME_LENGTH];
	long long 	msg_length;
};

/*struct MvctpMsgBofRecvedEvent {
	u_int16_t 	transfer_type;
	u_int32_t 	msg_id;
	char		msg_name[MAX_FILE_NAME_LENGTH];
	long long 	msg_length;
};*/



class MvctpEventQueueManager {
public:
	MvctpEventQueueManager();
	virtual ~MvctpEventQueueManager();

	// functions for adding and retrieving events
	// produced by MVCTP and consumed by the application
	int GetNextEvent(MvctpMsgTransferEvent* event);
	int	AddNewEvent(MvctpMsgTransferEvent& event);

	// functions for adding and retrieving message transfer events
	// produced by the application and consumed by MVCTP
	int GetNextTransferEvent(MvctpMsgTransferEvent* event);
	int AddNewTransferEvent(MvctpMsgTransferEvent& event);

private:
	EventQueue* app_notify_queue;
	EventQueue* transfer_request_queue;
};

#endif /* MVCTPEVENTQUEUEMANAGER_H_ */
