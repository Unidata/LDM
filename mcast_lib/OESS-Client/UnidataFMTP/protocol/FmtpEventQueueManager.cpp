/*
 * FmtpEventQueueManager.cpp
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#include "FmtpEventQueueManager.h"

FmtpEventQueueManager::FmtpEventQueueManager() {
	app_notify_queue = new EventQueue();
	transfer_request_queue = new EventQueue();
}

FmtpEventQueueManager::~FmtpEventQueueManager() {
	delete app_notify_queue;
	delete transfer_request_queue;
}


// functions for adding and retrieving events
// produced by FMTP and consumed by the application
int FmtpEventQueueManager::GetNextEvent(FmtpMsgTransferEvent* event) {
	return app_notify_queue->RecvEvent(event);
}


int	FmtpEventQueueManager::AddNewEvent(FmtpMsgTransferEvent& event) {
	return app_notify_queue->SendEvent(event.event_type, &event, sizeof(FmtpMsgTransferEvent));
}

// functions for adding and retrieving message transfer events
// produced by the application and consumed by FMTP
int FmtpEventQueueManager::GetNextTransferEvent(FmtpMsgTransferEvent* event) {
	return transfer_request_queue->RecvEvent(event);
}

int FmtpEventQueueManager::AddNewTransferEvent(FmtpMsgTransferEvent& event) {
	return transfer_request_queue->SendEvent(event.event_type, &event, sizeof(FmtpMsgTransferEvent));
}
