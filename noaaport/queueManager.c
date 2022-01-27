#include "config.h"

#include "misc.h"
#include "queueManager.h"
#include "CircFrameBuf.h"
#include "frameWriter.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <log.h>


//  ========================================================================
static pthread_mutex_t runMutex;
static void* cfbInst;
//  ========================================================================

/**
 * Threaded function to initiate the flowDirector running in its own thread
 *
 *
 * Function to continuously check for oldest frame in queue and write to stdout
 *
 * Never returns.
 *
 * pre-condition:	runMutex is UNlocked
 * post-condition: 	runMutex is UNlOCKed
 */
void*
flowDirectorRoutine()
{
	for (;;)
	{
		lockIt(&runMutex);

		Frame_t oldestFrame;
		if (cfb_getOldestFrame(cfbInst, &oldestFrame)) {
			if( fw_writeFrame( &oldestFrame ) == -1 )
			{
				log_add("Error writing to standard output");
				log_flush_fatal();
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

static void
initMutex()
{
    int resp = pthread_mutex_init(&runMutex, NULL);
    if(resp)
    {
        log_add("pthread_mutex_init( runMutex ) failure: %s - resp: %d\n", strerror(resp), resp);
		log_flush_error();
        exit(EXIT_FAILURE);
    }
}

/*
 * Function to
 * 	1- initialize mutex
 * 	2- create the C++ class instance: CircFrameBuf
 * 	3- launch the flowDirector thread
 *
 * @param[in]  frameLatency		Time to wait for more incoming frames when queue is empty
 * 								Used in CircFrameBuf class
 *
 * Never returns.
 */
void
queue_start(const double frameLatency)
{
	// Initialize runMutex
	(void) initMutex();

	// Create and initialize the CircFrameBuf class
	cfbInst = cfb_new(frameLatency);

	// create and launch flowDirector thread (to insert frames in map)
	flowDirector();
}

/*
 * tryInsertInQueue():	Try insert a frame in a queue
 *
 * @param[in]  sequenceNumber	Sequence number of this frame
 * @param[in]  runNumber  		Run number of this frame
 * @param[in]  buffer 			SBN data of this frame
 * @param[out] frameBytes  		Number of data bytes in this frame

 * @retval     0  				Success
 * @retval     !0      		  	Error
 *
 * pre-condition: 	runMutex is LOCKed
 * post-condition: 	runMutex is LOCKed
 */
int
tryInsertInQueue(  unsigned 		sequenceNumber,
		       	   unsigned 		runNumber,
				   unsigned char 	*buffer,
				   unsigned 		frameBytes)
{
	// runMutex is already LOCKed!
	log_assert( pthread_mutex_trylock(&runMutex) );

	int status = 0;

	// call in CircFrameBuf: (C++ class)
	bool cfbStatus = cfb_add( cfbInst, runNumber, sequenceNumber, buffer, frameBytes);
	if( !cfbStatus )
	{
		log_error("Inserting frame in queue failed.");
		status = -1;
	}
	return status;
}
