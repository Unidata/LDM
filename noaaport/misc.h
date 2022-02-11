/*
 * misc.h
 *
 *  Created on: Oct 25, 2021
 *      Author: Mustapha Iles
 */

#ifndef MISC_H_
#define MISC_H_

#include <pthread.h>
#include <assert.h>

void lockIt(pthread_mutex_t*);
void unlockIt(pthread_mutex_t*);

#endif /* MISC_H_ */
