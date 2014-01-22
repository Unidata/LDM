/*
 * MvctpEventQueueManager.cpp
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#include "MvctpEventQueueManager.h"

MvctpEventQueueManager::MvctpEventQueueManager() {
	app_notify_queue = new EventQueue();
	transfer_request_queue = new EventQueue();
}

MvctpEventQueueManager::~MvctpEventQueueManager() {
	delete app_notify_queue;
	delete transfer_request_queue;
}


// functions for adding and retrieving events
// produced by MVCTP and consumed by the application
int MvctpEventQueueManager::GetNextEvent(MvctpMsgTransferEvent* event) {
	return app_notify_queue->RecvEvent(event);
}


int	MvctpEventQueueManager::AddNewEvent(MvctpMsgTransferEvent& event) {
	return app_notify_queue->SendEvent(event.event_type, &event, sizeof(MvctpMsgTransferEvent));
}

// functions for adding and retrieving message transfer events
// produced by the application and consumed by MVCTP
int MvctpEventQueueManager::GetNextTransferEvent(MvctpMsgTransferEvent* event) {
	return transfer_request_queue->RecvEvent(event);
}

int MvctpEventQueueManager::AddNewTransferEvent(MvctpMsgTransferEvent& event) {
	return transfer_request_queue->SendEvent(event.event_type, &event, sizeof(MvctpMsgTransferEvent));
}
