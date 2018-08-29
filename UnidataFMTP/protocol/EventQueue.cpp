/*
 * EventQueue.cpp
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#include "EventQueue.h"

EventQueue::EventQueue(int size_limit) {
	buf_size_limit = size_limit;
	cur_buf_size = 0;
	num_events = 0;

	pthread_mutex_init(&queue_mutex, NULL);
}

EventQueue::~EventQueue() {
	pthread_mutex_lock(&queue_mutex);
	list<EventObject>::iterator it;
	for (it = event_list.begin(); it != event_list.end(); it++) {
		free(it->event_data);
	}
	pthread_mutex_unlock(&queue_mutex);

	pthread_mutex_destroy(&queue_mutex);
}

// Enqueue a new event
int EventQueue::SendEvent(int event_type, void* event_object, int num_bytes) {
	if ( (cur_buf_size + num_bytes) > buf_size_limit)
		return -1;

	EventObject new_event;
	new_event.event_type = event_type;
	new_event.num_bytes = num_bytes;
	new_event.event_data = malloc(num_bytes);
	memcpy(new_event.event_data, event_object, num_bytes);

	pthread_mutex_lock(&queue_mutex);
	event_list.push_back(new_event);
	pthread_mutex_unlock(&queue_mutex);

	cur_buf_size += num_bytes;
	num_events++;
	return 1;
}

// Retrieve a new event from the queue
// on return, event_type will be assigned the actual event type,
// and event_length will be assigned the length of the event object copied into buff
int EventQueue::RecvEvent(void* buff, int* event_type, int* event_length){
	if (!HasEvent())
		return -1;

	EventObject object;
	pthread_mutex_lock(&queue_mutex);
	object = event_list.front();
	event_list.pop_front();
	cur_buf_size -= object.num_bytes;
	num_events--;
	pthread_mutex_unlock(&queue_mutex);

	memcpy(buff, object.event_data, object.num_bytes);
	if (event_type != NULL)
		*event_type = object.event_type;
	if (event_length != NULL)
		*event_length = object.num_bytes;

	// delete the event object from the buffer
	free(object.event_data);

	return 1;
}


bool EventQueue::HasEvent() {
	return (num_events != 0);
}

