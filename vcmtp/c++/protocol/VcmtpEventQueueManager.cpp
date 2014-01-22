/*
 * VcmtpEventQueueManager.cpp
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#include "VcmtpEventQueueManager.h"

VcmtpEventQueueManager::VcmtpEventQueueManager() {
	app_notify_queue = new EventQueue();
	transfer_request_queue = new EventQueue();
}

VcmtpEventQueueManager::~VcmtpEventQueueManager() {
	delete app_notify_queue;
	delete transfer_request_queue;
}


// functions for adding and retrieving events
// produced by VCMTP and consumed by the application
int VcmtpEventQueueManager::GetNextEvent(VcmtpMsgTransferEvent* event) {
	return app_notify_queue->RecvEvent(event);
}


int	VcmtpEventQueueManager::AddNewEvent(VcmtpMsgTransferEvent& event) {
	return app_notify_queue->SendEvent(event.event_type, &event, sizeof(VcmtpMsgTransferEvent));
}

// functions for adding and retrieving message transfer events
// produced by the application and consumed by VCMTP
int VcmtpEventQueueManager::GetNextTransferEvent(VcmtpMsgTransferEvent* event) {
	return transfer_request_queue->RecvEvent(event);
}

int VcmtpEventQueueManager::AddNewTransferEvent(VcmtpMsgTransferEvent& event) {
	return transfer_request_queue->SendEvent(event.event_type, &event, sizeof(VcmtpMsgTransferEvent));
}
