/*
 * misc.c
 *
 *  Created on: Oct 25, 2021
 *      Author: Mustapha Iles
 */

#include "misc.h"
#include <pthread.h>

void
lockIt(pthread_mutex_t* thisMutex)
{
	int resp = pthread_mutex_lock(thisMutex);
	assert( resp == 0);
}

void
unlockIt(pthread_mutex_t* thisMutex)
{
	int resp = pthread_mutex_unlock(thisMutex);
	assert( resp == 0);
}
