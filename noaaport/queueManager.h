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

extern pthread_t		flowDirectorThread;
//  ========================================================================

void setFIFOPolicySetPriority(pthread_t, char *, int);
void queue_start(const double frameLatency);

//  ========================================================================

#endif


