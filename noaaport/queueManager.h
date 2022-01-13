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

#define ONE_BILLION                 1000000000



pthread_t		flowDirectorThread;


typedef struct QueueConf {
    double 		frameLatency;
} QueueConf_t;

//  ========================================================================

void setFIFOPolicySetPriority(pthread_t, char *, int);
void initFrameHashTable(void);
void queue_start(const double frameLatency);

//  ========================================================================

#endif


