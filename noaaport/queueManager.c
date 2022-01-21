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
void* cfb;
//  ========================================================================

/**
 * Threaded function to initiate the flowDirector running in its own thread
 *
 * pre-condition:	runMutex is UNlocked
 * post-condition: 	runMutex is UNlOCKed
 */
void*
flowDirectorRoutine()
{
	struct timespec abs_time;
	int numFrames;

	for (;;)
	{
		lockIt(&runMutex);

		// Call into the hashTableManager to provide a frame to consume.
		// It will NOT block:
		Frame_t oldestFrame;

		if (cfb_getOldestFrame(cfb, &oldestFrame)) {
            log_debug("\n=> => => ConsumeFrames Thread (flowDirectorRoutine) => => => =>");

			// if( writeFrame( oldestFrame->sbnFrame ) == -1 )  // <- comment-out this when ready and remove next line
			// also fix 'fr_writeFrame()' signature
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

void
queue_start(const double frameLatency)
{
	// Initialize runMutex
	(void) initMutex();

	// Create and initialize the CircFrameBuf class
	cfb = (void*) cfb_new(frameLatency);

	// create and launch flowDirector thread (to insert frames in map)
	flowDirector();
}

/*
 * tryInsertInQueue():	Try insert a frame
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
	assert( pthread_mutex_trylock(&runMutex) );

	// call in CircFrameBuf:
	bool status = cfb_add( cfb, runNumber, sequenceNumber, buffer, frameBytes);
	if( !status )
	{
		log_error("Inserting frame in queue failed.");
	}
	return status;
}
