/*
 * queueManager.h
 *
 *  Created on: Aug 24, 2021
 *      Author: Mustapha Iles
 */

#ifndef QUEUE_MANAGER_DOT_H
#define QUEUE_MANAGER_DOT_H

#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>

#include "hashTableImpl.h"

#define ONE_BILLION                 1000000000



pthread_t		flowDirectorThread;


typedef struct QueueConf {
    double 		frameLatency;
    int 		hashTableSize;
} QueueConf_t;

//  ========================================================================

extern void 			setFIFOPolicySetPriority(pthread_t, char *, int);
extern void 			initFrameHashTable(void);

//  ========================================================================




//static void				setMaxWait(double);
static void 			flowDirector(void);
static void 			tryAddToQueue(uint32_t, uint16_t, unsigned char*, uint16_t, int);
static void 			initMutexAndCond(void);
//  ========================================================================

#endif


