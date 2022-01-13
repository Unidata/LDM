/*
 * hashTableImpl.h
 *
 *  Created on: Aug 27, 2021
 *      Author: Mustapha Iles
 */

#ifndef HASHTABLEIMPL_H_
#define HASHTABLEIMPL_H_

#include "noaaportFrame.h"

#include <stdint.h>
#include <pthread.h>
#include <limits.h>

#define HASH_TABLE_SIZE             15000    // CONDUIT frameRate (3500/s) * 2 * frameLatency input
#define SBN_FRAME_SIZE              5000

#define FRAME_INSERTED				0
#define DUPLICATE_FRAME				-1
#define TABLE_TOO_SMALL				-2
#define FRAME_TOO_LARGE				-3
#define FRAME_TOO_LATE				-4

#define INITIAL_SEQ_NUM             0
#define INITIAL_RUN_NUM             0

typedef struct aFrameSlot {

	bool      occupied;
    Frame_t   aFrame;

} FrameSlot_t;

typedef struct aHashTableStruct {

	pthread_mutex_t aHashTableMutex;
	pthread_cond_t	aHashTableCond;
	uint32_t		frameCounter;
	uint32_t 		totalFramesReceived;
	int64_t			lastOutputSeqNum;

	FrameSlot_t 	aHashTable[HASH_TABLE_SIZE];

} HashTableStruct_t;

extern int 			hti_tryInsert(		HashTableStruct_t*, uint32_t, uint16_t, unsigned char*, int);
extern bool 		hti_isEmpty(		HashTableStruct_t* );
extern bool			hti_getOldestFrame(	HashTableStruct_t*, Frame_t*);
extern int 			hti_getNumberOfFrames(	HashTableStruct_t* );
extern void			hti_releaseOldest(	HashTableStruct_t*, Frame_t* );
extern void 		hti_init(			HashTableStruct_t* );

#endif /* HASHTABLEIMPL_H_ */
