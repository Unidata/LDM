#include "config.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <sys/time.h>
#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <signal.h>
#include <stdbool.h>
#include <assert.h>
#include <log.h>

#include "misc.h"
#include "queueManager.h"
//#include "noaaportFrame.h"
#include "CircFrameBuf.h"

//  ========================================================================
pthread_mutex_t runMutex;
pthread_cond_t  cond;
//  ========================================================================

extern int 			fw_writeFrame( Frame_t );
extern void 		lockIt(		pthread_mutex_t* );
extern void 		unlockIt( 	pthread_mutex_t* );

extern unsigned 	cfb_numberOfFrames(void*);
extern bool 		cfb_getOldestFrame( void*, unsigned*, unsigned*, const char**, unsigned*);

//  ========================================================================
struct timespec 	timeOut = {						// <- may not be used
    .tv_sec = 1,    // default value
    .tv_nsec = 0
};
int hashTableSize;
//  ========================================================================
static clockid_t    	clockToUse = CLOCK_MONOTONIC;

static void* cfb;
//  ========================================================================
QueueConf_t* setQueueConf(double frameLatency, int hashTableSize)
{
	QueueConf_t* aQueueConfig 	= (QueueConf_t*) malloc(sizeof(QueueConf_t) );
	aQueueConfig->frameLatency 	= frameLatency;
	aQueueConfig->hashTableSize = hashTableSize;

	// Create and initialize the CircFrameBuf class here
	cfb = (void*) cfb_new( hashTableSize, frameLatency);
	return aQueueConfig;
}

static void
setMaxWait(double frameLatency)
{
    double integral;
    double fractional = modf(frameLatency, &integral);

    timeOut.tv_sec     = integral;
    timeOut.tv_nsec    = fractional * ONE_BILLION;
}

static void
setHashTableSize(int tSize)
{
	hashTableSize = tSize;
}

void
queue_start(QueueConf_t* aQueueConf)
{
	// QueueConf elements:


	(void) initMutexAndCond();

	(void) setMaxWait(aQueueConf->frameLatency);
	(void) setHashTableSize(aQueueConf->hashTableSize);

	free(aQueueConf);

	flowDirector();
}

void
queueDestroy()
{

}

static void
initMutexAndCond()
{
    int resp = pthread_mutex_init(&runMutex, NULL);
    if(resp)
    {
        log_add("pthread_mutex_init( runMutex ) failure: %s - resp: %d\n", strerror(resp), resp);
		log_flush_error();
        exit(EXIT_FAILURE);
    }

    // Code not needed as it does not make a difference: clockToUse
    pthread_condattr_t attr;
    int ret = pthread_condattr_setclock(&attr, clockToUse);

    // init cond
    resp = pthread_cond_init(&cond, &attr);
    if(resp)
    {
        log_add("pthread_cond_init( cond ) failure: %s\n", strerror(resp));
		log_flush_error();
        exit(EXIT_FAILURE);
    }
}

/**
 * Threaded function to initiate the flowDirector running in its own thread
 *
 */
//pre-condition:	runMutex NOT locked
void*
flowDirectorRoutine()
{
	struct timespec abs_time;
	int numFrames;

	for (;;)
	{
		int status = 0;
		lockIt(&runMutex);

		clock_gettime(clockToUse, &abs_time);
		// pthread cond_timedwait() expects an absolute time to wait until
		//abs_time.tv_sec     += max_wait.tv_sec;
		//abs_time.tv_nsec    += max_wait.tv_nsec;

		//abs_time.tv_sec     += 1;
		//abs_time.tv_nsec    = 0;
		abs_time.tv_sec     += timeOut.tv_sec; // .10
		abs_time.tv_nsec    += timeOut.tv_nsec;
		if(abs_time.tv_nsec > ONE_BILLION )
		{
			abs_time.tv_nsec -= ONE_BILLION;
			++abs_time.tv_sec;
		}

		for( numFrames = cfb_numberOfFrames( cfb );
				numFrames == 0 || (status == 0 && numFrames < hashTableSize/2);
			    numFrames = cfb_numberOfFrames( cfb ) )
		{
			status = pthread_cond_timedwait(&cond, &runMutex, &abs_time);
			log_add("\n\nWAIT: (flowDirectorRoutine).. .\n\n");
			log_flush_debug();
			if( status && status != ETIMEDOUT)
			{
				log_fatal("\n\n status: %d.\n\n", status);
				abort();
			}
			//assert(status == 0 || status == ETIMEDOUT);
		}

		if( numFrames > 0 && status )
			log_debug("\n=> => => ConsumeFrames Thread (flowDirectorRoutine) => => => =>");

		// Call into the hashTableManager to provide a frame to consume.
		// It will NOT block:
		Frame_t* oldestFrame;

		bool resp = cfb_getOldestFrame(cfb,
										oldestFrame->runNum,
										oldestFrame->seqNum,
										oldestFrame->data,
										&oldestFrame->nbytes);

		if( resp )
		{
			// if( writeFrame( oldestFrame->sbnFrame ) == -1 )  // <- comment-out this when ready and remove next line
			// also fix 'fr_writeFrame()' signature
			if( fw_writeFrame( *oldestFrame ) == -1 )
			{
				log_add("\nError writing to pipeline\n");
				log_flush_error();
				exit(EXIT_FAILURE);
			}

		}
		unlockIt(&runMutex);

    } // for

    log_free();
    return NULL;

}

// flowDirector thread creation: thread with a highest priority
static void
flowDirector()
{
    if(pthread_create(  &flowDirectorThread, NULL, flowDirectorRoutine, NULL ) < 0)
    {
        log_add("Could not create a thread!\n");
		log_flush_error();
        exit(EXIT_FAILURE);
    }
    setFIFOPolicySetPriority(flowDirectorThread, "flowDirectorThread", 2);
}
//======================================================


/*
 * tryInsertInQueue():	Try insert a frame in ... queue (one of 2 hash tables)
 *
 * pre-condition: 	runMutex is unLOCKed
 * post-condition: 	runMutex is unLOCKed
 */
int
tryInsertInQueue(  unsigned 		sequenceNumber,
		       	   unsigned 		runNumber,
				   unsigned char 	*buffer,
				   unsigned 		frameBytes)
{

	// runMutex is unLOCKed: lock it!
	lockIt(&runMutex);

	// call in CircFrameBuf:
	bool status = cfb_add( cfb, runNumber, sequenceNumber, buffer, frameBytes);
	if( !status )
	{
		log_error("Inserting frame in queue failed.");
	}

	return status;
}




