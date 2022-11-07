#include "config.h"

#include "misc.h"
#include "queueManager.h"
#include "CircFrameBuf.h"
#include "frameWriter.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <log.h>

pthread_t		flowDirectorThread;

//  ========================================================================
static void* cfbInst;
//  ========================================================================

/**
 * Threaded function to initiate the flowDirector running in its own thread
 *
 *
 * Function to continuously check for oldest frame in queue and write to stdout
 *
 * Never returns.
 */
void*
flowDirectorRoutine()
{
	for (;;)
	{
		Frame_t oldestFrame;
		if (!cfb_getOldestFrame(cfbInst, &oldestFrame)) {
		    log_flush_fatal();
		    exit(EXIT_FAILURE);
		}
		else if( fw_writeFrame( &oldestFrame ) == -1 )
        {
            log_flush_fatal();
            exit(EXIT_FAILURE);
        }
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

/*
 * Function to
 * 	1- create the C++ class instance: CircFrameBuf
 * 	2- launch the flowDirector thread
 *
 * @param[in]  frameLatency		Time to wait for more incoming frames when queue is empty
 * 								Used in CircFrameBuf class
 *
 * Never returns.
 */
void
queue_start(const double frameLatency)
{
	// Create and initialize the CircFrameBuf class
	cfbInst = cfb_new(frameLatency);

	// create and launch flowDirector thread (to insert frames in map)
	flowDirector();
}

/**
 * tryInsertInQueue():	Try insert a frame in a queue
 *
 * @param[in]  fh               Frame-level header
 * @param[in]  pdh              Product-description header
 * @param[in]  buffer 			SBN data of this frame
 * @param[out] frameBytes  		Number of data bytes in this frame

 * @retval     0  				Success
 * @retval     1                Frame is too late. `log_add()` called.
 * @retval     2                Frame is duplicate
 * @retval     -1               System error. `log_add()` called.
 *
 * pre-condition: 	runMutex is unLOCKed
 * post-condition: 	runMutex is unLOCKed
 */
int
tryInsertInQueue(  const NbsFH*         fh,
		       	   const NbsPDH*        pdh,
				   const uint8_t* const buffer,
				   size_t 			    frameBytes)
{
	// call in CircFrameBuf: (C++ class)
	return cfb_add( cfbInst, fh, pdh, buffer, frameBytes);
}
