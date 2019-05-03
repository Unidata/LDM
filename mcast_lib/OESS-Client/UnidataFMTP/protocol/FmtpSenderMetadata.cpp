/*
 * FmtpSenderMetadata.cpp
 *
 *  Created on: Jul 1, 2012
 *      Author: jie
 */

#include "FmtpSenderMetadata.h"

FmtpSenderMetadata::FmtpSenderMetadata() {
	pthread_rwlock_init(&map_lock, NULL);
}

FmtpSenderMetadata::~FmtpSenderMetadata() {
	for (map<uint, MessageMetadata*>::iterator it = metadata_map.begin(); it != metadata_map.end(); it++) {
		delete (it->second);
	}

	pthread_rwlock_destroy(&map_lock);
}


void FmtpSenderMetadata::AddMessageMetadata(MessageMetadata* ptr_meta) {
	pthread_rwlock_wrlock(&map_lock);
	metadata_map[ptr_meta->msg_id] = ptr_meta;
	pthread_rwlock_unlock(&map_lock);
}


void FmtpSenderMetadata::RemoveMessageMetadata(uint msg_id) {
	pthread_rwlock_wrlock(&map_lock);
	map<uint, MessageMetadata*>::iterator it = metadata_map.find(msg_id);
	if (it != metadata_map.end()) {
		delete it->second;
		metadata_map.erase(it);
	}
	pthread_rwlock_unlock(&map_lock);
}

void FmtpSenderMetadata::ClearAllMetadata() {
	pthread_rwlock_wrlock(&map_lock);
	for (map<uint, MessageMetadata*>::iterator it = metadata_map.begin(); it != metadata_map.end(); it++) {
		delete (it->second);
	}
	metadata_map.clear();
	pthread_rwlock_unlock(&map_lock);
}

MessageMetadata* FmtpSenderMetadata::GetMetadata(uint msg_id) {
	MessageMetadata* temp = NULL;
	map<uint, MessageMetadata*>::iterator it;
	pthread_rwlock_rdlock(&map_lock);
	if ((it = metadata_map.find(msg_id)) != metadata_map.end())
		temp = it->second;
	pthread_rwlock_unlock(&map_lock);
	return temp;
}


bool FmtpSenderMetadata::IsTransferFinished(uint msg_id) {
	bool is_finished = true;
	map<uint, MessageMetadata*>::iterator it;
	pthread_rwlock_rdlock(&map_lock);
	if ( (it = metadata_map.find(msg_id)) != metadata_map.end()) {
		if (GetElapsedSeconds(it->second->multicast_start_cpu_time) < it->second->retx_timeout_seconds)
			is_finished = (it->second->unfinished_recvers.size() == 0);
	}
	pthread_rwlock_unlock(&map_lock);

	return is_finished;
}


int FmtpSenderMetadata::GetFileDescriptor(uint msg_id) {
	int res = -1;
	map<uint, MessageMetadata*>::iterator it;
	pthread_rwlock_rdlock(&map_lock);
	if ( (it = metadata_map.find(msg_id)) == metadata_map.end()) {
		pthread_rwlock_unlock(&map_lock);
		return res;
	}

	FileMessageMetadata* meta = (FileMessageMetadata*)it->second;
	if (meta->file_descriptor < 0) {
		pthread_rwlock_unlock(&map_lock);
		pthread_rwlock_wrlock(&map_lock);
		meta->file_descriptor = open(meta->file_name.c_str(), O_RDONLY);
		res = meta->file_descriptor;
		pthread_rwlock_unlock(&map_lock);
	}
	else {
		pthread_rwlock_unlock(&map_lock);
	}
	return res;
}


void FmtpSenderMetadata::RemoveFinishedReceiver(uint msg_id, int sock_fd) {
	map<uint, MessageMetadata*>::iterator it;
	pthread_rwlock_wrlock(&map_lock);
	if ( (it = metadata_map.find(msg_id)) != metadata_map.end())
		it->second->unfinished_recvers.erase(sock_fd);
	pthread_rwlock_unlock(&map_lock);
}

