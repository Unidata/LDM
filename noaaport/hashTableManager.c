/*
 * hashTableManager.c
 *
 *  Created on: Sep 9, 2021
 *      Author: Mustapha Iles
 */

#include "config.h"
#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <log.h>
#include "globals.h"

#include "misc.h"
#include "hashTableManager.h"

//  ========================================================================
extern void 	lockIt(		pthread_mutex_t* );
extern void 	unlockIt( 	pthread_mutex_t* );
extern void 	hti_releaseOldest( HashTableStruct_t*, Frame_t* );
extern void 	hti_reset( HashTableStruct_t* );
extern int 		hashTableSize;
//  ========================================================================

// These variables are under mutex:

Frame_t*			oldestFrame;
static pthread_mutex_t mutex;

//  ========================================================================

// Define the 2 hash table structures
typedef struct hts {
    int            		runNum;
    HashTableStruct_t* 	impl;
} HashTableInfo_t;

HashTableInfo_t hashTableInfos[2];

static HashTableInfo_t* 	pNext;       // Hash table currently being filled
#define POTHER 				(pNext == hashTableInfos? hashTableInfos + 1:  hashTableInfos )
static HashTableInfo_t* 	pOut; // Hash table of last output frame

void
htm_init()
{
	bool firstIter = true;
	// initialize mutex new in HTMan
    for (int i = 0; i < 2; ++i) {
        hashTableInfos[i].runNum = -1;

        if( !(hashTableInfos[i].impl = malloc(sizeof(HashTableStruct_t))))
        {
        	log_info("Could not allocate a HashTableStruct_t");
        	if(!firstIter)
        		free(hashTableInfos[i-1].impl);

        	log_flush_info();
        	exit(EXIT_FAILURE);
        }
        firstIter = false;

        // initialize table mutex in HTImpl
        hti_init( hashTableInfos[i].impl );

    }

    pNext	= hashTableInfos;	// 0
    pOut 	= pNext;

    if( pthread_mutex_init(&mutex, NULL) )
    {
    	log_info("pthread_mutex_init() failure\n");
    	exit(EXIT_FAILURE);
    }
}


// ==================================================================
int
htm_tryInsert(uint16_t runNum,
              uint32_t seqNum,
              char*    data,
              int      nbytes)
{
	// mutex lock: mutex
	lockIt( &mutex );

	// Debug:
	log_info("\n==== Inserting seqNum %u within run#: %u =====\n",seqNum, runNum);

    if (pNext->runNum == -1)
        pNext->runNum = runNum; // Handles startup

    // if incoming frame's runNum has changed from current one
    // AND is also different from the previous one
    // THEN proceed to switching the tables if the previous table is empty
    if (runNum != pNext->runNum && pNext == pOut )
    {

		log_debug("if (runNum !=   pCurr->runNum && runNum != POTHER->runNum )...\
				\n  runNum: %u, pCurr: %u,                 POTHER: %u\n",
				runNum, pNext->runNum, POTHER->runNum);

    		log_debug("\t -> POTHER->impl is empty (count: %u, POTHER->runNum: %u)!\n",
    				POTHER->impl->frameCounter, POTHER->runNum);
    		log_debug("\t -> Swapping POTHER and pCurr...\n");
    		// Swap tables ('cause previous table is available)
			pNext = POTHER;

			hti_reset( pNext->impl );
			pNext->runNum = runNum;
    }

    log_debug("\t -> Using hashTable: %s\n",
    				(runNum == pNext->runNum)? "pNext":"POTHER");
    HashTableStruct_t* impl = (runNum == pNext->runNum)
            ? pNext->impl
            : POTHER->impl; // Insert older frame into previous table

    int status = hti_tryInsert(impl, seqNum, runNum, data, nbytes); // could fail: duplicate, too old, success

    // mutex unlock
	unlockIt( &mutex );
    return FRAME_INSERTED;
}

bool
htm_getOldestFrame(Frame_t* oldestFrame )
{
	// mutex lock HTMan mutex
	lockIt(&mutex);

    bool success = hti_getOldestFrame(pOut->impl, oldestFrame );

	// mutex unlock HTMan mutex
    unlockIt( &mutex);

    return success;
}

int
htm_numberOfFrames()
{
	// the 'frameCounter' value HAS to be updated at insert and/or consumption time

	int nbFrames_1 = hti_getNumberOfFrames( hashTableInfos[ TABLE_NUM_1 ].impl );
	int nbFrames_2 = hti_getNumberOfFrames( hashTableInfos[ TABLE_NUM_2 ].impl );

	return nbFrames_1 + nbFrames_2;
}

void
htm_releaseOldestFrame( Frame_t* oldestFrame )
{
	// oldestFrame is not used here but it could be:
	// Check that it is consistent
	// assert( oldestFrame->seqNum == pLastOutput->impl->lastOutputSeqNum);

	(void) hti_releaseOldest( pOut->impl, oldestFrame );

    if ( hti_isEmpty(pOut->impl ) && pNext != pOut )
    {
    	log_debug("htm_releaseOldestFrame(): pCurr: %u, pOut: %u \n",
    			pNext->runNum, pOut->runNum);

    	log_debug("reset(): %u \n",  pOut->runNum);
    	(void) hti_reset( pOut->impl );

    	log_debug("pOut = pNext;\n");
        pOut = pNext;

        log_flush_info();
    }

}

//============================== add this comment somewhere in the documentation ====================================================================================//
// Assumption:  if oldest frame belongs to table A and for some incongruous reason the slot is invalid
//              then, looking for the next oldest frame should be performed in the same table A, up to
//              finding it - after eventual gaps - or not finding it at all if the table has become empty.

