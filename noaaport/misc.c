#include "config.h"
/*
 * misc.c
 * Utility functions to lock/unlock a mutex and set policy
 *
 *  Created on: Oct 25, 2021
 *      Author: Mustapha Iles
 */

#include "misc.h"
#include "log.h"

#include <pthread.h>
#include <string.h>

void
lockIt(pthread_mutex_t* thisMutex)
{
	int resp = pthread_mutex_lock(thisMutex);
	log_assert( resp == 0);
}

void
unlockIt(pthread_mutex_t* thisMutex)
{
	int resp = pthread_mutex_unlock(thisMutex);
	log_assert( resp == 0);
}

void
setFIFOPolicySetPriority(pthread_t pThread, char *threadName, int deltaPriority)
{

    int prevPolicy, newPolicy, prevPrio, newPrio;
    struct sched_param param;
    memset(&param, 0, sizeof(struct sched_param));

    // set it
    newPolicy = SCHED_FIFO;
    //=============== increment the consumer's thread's priority ====
    int thisPolicyMaxPrio = sched_get_priority_max(newPolicy);

    if( param.sched_priority < thisPolicyMaxPrio - deltaPriority)
    {
        param.sched_priority += deltaPriority;
    }
    else
    {
        log_add("Could not get a new priority to frameConsumer thread! \n");
        log_add("Current priority: %d, Max priority: %d\n",
            param.sched_priority, thisPolicyMaxPrio);
        log_flush_warning();
    }


    int resp;
    resp = pthread_setschedparam(pThread, newPolicy, &param);
    if( resp )
    {
        log_add("setFIFOPolicySetPriority() : pthread_setschedparam() failure: %s\n",
        		strerror(resp));
        log_flush_warning();
    }
    else
    {
    	newPrio = param.sched_priority;
    	log_add("Thread: %s \tpriority: %d, policy: %s\n",
    	        threadName, newPrio, newPolicy == 1? "SCHED_FIFO": newPolicy == 2? "SCHED_RR" : "SCHED_OTHER");
    	log_flush_info();
    }
}
