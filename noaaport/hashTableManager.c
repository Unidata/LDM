/*
 * hashTableManager.c
 *
 *  Created on: Sep 9, 2021
 *      Author: Mustapha Iles
 */

#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

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
static HashTableInfo_t* 	pPrev;       // Hash table previously being filled
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
        	printf("Could not allocate a HashTableStruct_t");
        	if(!firstIter)
        		free(hashTableInfos[i-1].impl);

        	exit(EXIT_FAILURE);
        }
        firstIter = false;

        // initialize table mutex in HTImpl
        hti_init( hashTableInfos[i].impl );

    }

    pNext 		= hashTableInfos;	// 0
    printf("Initial pCurr->runNum: %u\n", pNext->runNum);
    pPrev 		= hashTableInfos+1;
    printf("Initial pPrev->runNum: %u\n", pPrev->runNum);
    pOut = pPrev;

    if( pthread_mutex_init(&mutex, NULL) )
    {
    	printf("pthread_mutex_init() failure\n");
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
	printf("\n============ Inserting seqNum %u in runNum: %u =========\n",
			seqNum, runNum);

    if (pNext->runNum == -1)
        pNext->runNum = runNum; // Handles startup

    // if incoming frame's runNum has changed from current one
    // AND is also different from the previous one
    // THEN proceed to switching the tables if the previous table is empty
    if (runNum != pNext->runNum && runNum != pPrev->runNum )
    {

		printf("if (runNum !=   pCurr->runNum && runNum != pPrev->runNum )...\
				\n  runNum: %u, pCurr: %u,                 pPrev: %u\n",
				runNum, pNext->runNum, pPrev->runNum);

    	if( hti_isEmpty( pPrev->impl ) )
    	{
    		printf("pPrev->impl is empty (count: %u, pPrev->runNum: %u)!\n",
    				pPrev->impl->frameCounter, pPrev->runNum);
    		printf("Swapping pPrev and pCurr...\n");
    		// Swap tables ('cause previous table is available)
			HashTableInfo_t* pTmp = pNext;
			pNext = pPrev;
			pPrev = pTmp;
			pNext->runNum = runNum;
    	}
    	else
    	{
    		// too bad!
    		printf("\n\nAdjust sleep and wait time until it works!...\n\n");
    		printf("pPrev->impl is NOT empty: count: %u (pPrev->runNum: %u)!\n",
    				pPrev->impl->frameCounter, pPrev->runNum);

    	}
    }

    printf("Which table to use to insert? %s\n",
    				(runNum == pNext->runNum)? "pCurr":"pPrev");
    HashTableStruct_t* impl = (runNum == pNext->runNum)
            ? pNext->impl
            : pPrev->impl; // Insert older frame into previous table

    int status = hti_tryInsert(impl, seqNum, runNum, data, nbytes); // could fail: duplicate, too old, success

    if( status == DUPLICATE )
    {
    	// msg:
		unlockIt( &mutex );
    }
    else
    if( status == TOO_OLD )
    {
    	// mutex unlock
		unlockIt( &mutex );
    }
    else
    if( status == FRAME_TOO_LARGE )
    {
    	// msg:
    	// mutex unlock
		unlockIt( &mutex );
    }

    // mutex unlock
	unlockIt( &mutex );
    return FRAME_INSERTED;
}

bool
htm_getOldestFrame(Frame_t* oldestFrame )
{
	// mutex lock HTMan mutex
	lockIt(&mutex);

    if (hti_isEmpty(pOut->impl) && pNext != pOut)
    {
    	printf("getOldestFrame(): pCurr: %u, pLastOutput: %u \n",
    			pNext->runNum, pOut->runNum);

    	printf("reset(): %u \n",  pOut->runNum);
    	(void) hti_reset( pOut->impl );

    	printf("pLastOutput <- pCurr\n");
        pOut = pNext;
    }

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

}

//============================== add this comment somewhere in the documentation ====================================================================================//
// Assumption:  if oldest frame belongs to table A and for some incongruous reason the slot is invalid
//              then, looking for the next oldest frame should be performed in the same table A, up to
//              finding it - after eventual gaps - or not finding it at all if the table has become empty.

