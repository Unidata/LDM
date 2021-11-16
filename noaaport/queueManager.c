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

#include "misc.h"
#include "queueManager.h"
//  ========================================================================
pthread_mutex_t runMutex;
pthread_cond_t  cond;
//  ========================================================================

extern int 			fw_writeFrame( Frame_t );
extern void 		htm_releaseOldestFrame( Frame_t* );
extern int			htm_numberOfFrames( void );

extern int 			htm_tryInsert( uint16_t, uint32_t, char*, int );
extern bool 		htm_getOldestFrame( Frame_t* );
extern void  		htm_init( void );

extern void 		lockIt(		pthread_mutex_t* );
extern void 		unlockIt( 	pthread_mutex_t* );

//  ========================================================================
struct timespec 	max_wait = {						// <- may not be used
    .tv_sec = 1,    // default value
    .tv_nsec = 0
};
int hashTableSize;
//  ========================================================================
static clockid_t    	clockToUse = CLOCK_MONOTONIC;	// <- may not be used

//  ========================================================================
QueueConf_t* setQueueConf(double frameLatency, int hashTableSize)
{
	QueueConf_t* aQueueConfig 	= (QueueConf_t*) malloc(sizeof(QueueConf_t) );
	aQueueConfig->frameLatency 	= frameLatency;
	aQueueConfig->hashTableSize = hashTableSize;

	return aQueueConfig;
}
static void
setMaxWait(double frameLatency)
{
    double integral;
    double fractional = modf(frameLatency, &integral);

    max_wait.tv_sec     = integral;
    max_wait.tv_nsec    = fractional * ONE_BILLION;
}

static void
setHashTableSize(int tSize)
{
	hashTableSize = tSize;
}


void
queue_init(QueueConf_t* aQueueConf)
{
	// QueueConf elements:
	(void)  htm_init();

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
        printf("pthread_mutex_init( runMutex ) failure: %s - resp: %d\n", strerror(resp), resp);
        exit(EXIT_FAILURE);
    }

    // Code not needed as it does not make a difference: clockToUse
    pthread_condattr_t attr;
    int ret = pthread_condattr_setclock(&attr, clockToUse);
    resp = pthread_cond_init(&cond, &attr);
    if(resp)
    {
        printf("pthread_cond_init( cond ) failure: %s\n", strerror(resp));
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

		clock_gettime(CLOCK_REALTIME, &abs_time);
		// pthread cond_timedwait() expects an absolute time to wait until
		//abs_time.tv_sec     += max_wait.tv_sec;
		//abs_time.tv_nsec    += max_wait.tv_nsec;

		abs_time.tv_sec     += 1;
		abs_time.tv_nsec    = 0;
	/*
		if( abs_time.tv_nsec > ONE_BILLION)
		{
			abs_time.tv_nsec -= ONE_BILLION;
			++abs_time.tv_sec;
		}
	*/

		for( numFrames = htm_numberOfFrames();
				numFrames == 0 || (status == 0 && numFrames < hashTableSize/2);
			    numFrames = htm_numberOfFrames() )
		{

			status = pthread_cond_timedwait(&cond, &runMutex, &abs_time);
			printf("status: %d\n", status);
			assert(status == 0 || status == ETIMEDOUT);
		}

		printf("\n\n=> => => => => => => ConsumeFrames Thread (flowDirectorRoutine)=> => => => => => => =>\n\n");

		// Call into the hashTableManager to provide a frame to consume.
		// It will NOT block:
		Frame_t oldestFrame;
		bool resp = htm_getOldestFrame( &oldestFrame );

		if( resp )
		{
			// if( writeFrame( oldestFrame->sbnFrame ) == -1 )  // <- comment-out this when ready and remove next line
			// also fix 'fr_writeFrame()' signature
			if( fw_writeFrame( oldestFrame ) == -1 )
			{
				printf("\nError writing to pipeline\n");
				exit(EXIT_FAILURE);
			}

			(void) htm_releaseOldestFrame( &oldestFrame );
		}
		unlockIt(&runMutex);
    } // for

    // log_free();
    return NULL;

}

// Thread creation: flow director, thread with a highest priority
static void
flowDirector()
{
    if(pthread_create(  &flowDirectorThread, NULL, flowDirectorRoutine, NULL ) < 0)
    {
        printf("Could not create a thread!\n");
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
tryInsertInQueue(  uint32_t 		sequenceNumber,
		       	   uint16_t 		runNumber,
				   unsigned char 	*buffer,
				   uint16_t 		frameBytes)
{

	// runMutex is unLOCKed: lock it!
	lockIt(&runMutex);

	// call in hashTableManager:
	int status = 	htm_tryInsert(runNumber, sequenceNumber, buffer, frameBytes);

	if( status == FRAME_INSERTED )
	{
		pthread_cond_signal(&cond);
	}
	unlockIt(&runMutex);

	return status;
}




