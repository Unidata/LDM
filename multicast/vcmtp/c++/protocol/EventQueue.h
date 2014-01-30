/*
 * EventManager.h
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#ifndef EVENTQUEUE_H_
#define EVENTQUEUE_H_

#include <list>
#include <cstdlib>
#include <cstring>
#include <pthread.h>

using namespace std;

struct EventObject {
	int		event_type;
	int 	num_bytes;
	void* 	event_data;
};

class EventQueue {
public:
	EventQueue(int size_limit = 1048576);
	virtual ~EventQueue();

	int 	SendEvent(int event_type, void* event_object, int num_bytes);
	int 	RecvEvent(void* buff, int* event_type = NULL, int* event_length = NULL);
	bool 	HasEvent();

private:
	int 				buf_size_limit;
	int 				cur_buf_size;
	int					num_events;
	list<EventObject> 	event_list;
	pthread_mutex_t 	queue_mutex;
};

#endif /* EVENTQUEUE_H_ */
