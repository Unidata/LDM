/*
 * hashTableImpl.c
 *
 *  Created on: Aug 27, 2021
 *      Author: Mustapha Iles
 *
 *
 * hashTableImpl.c
 *
 *      Module to manage a single hash table
 *      with its own mutex to protect its state
 *      - numberOfFrames in table
 *      - index of oldest frame (oldest slot)
 *      - index of last oldest frame that was output -
 *        to prevent the insertion of an older frame by another input thread.
 *        to be initialized to -1 so that the first frame would be considered as a later frame.
 *
 *      and these functions:
 *      - isTableEmpty()
 *      - getOldestFrame()
 *      - setOldestFrame()
 *      - releaseOldestFrame
 *      - getNumberOfFrameInTable()
 *
 *
 */
#include "config.h"
#include <stdbool.h>
#include <time.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <log.h>
#include "globals.h"

#include "misc.h"
#include "hashTableImpl.h"

//  ========================================================================

extern int hashTableSize;
extern pthread_cond_t  cond;
//  ========================================================================

static int 		framesMissedCount 		= 0;
static uint32_t	totalFramesReceived 	= 0;

static int 		hashMe(uint32_t seqNum);
static clockid_t    	clockToUse = CLOCK_MONOTONIC;
//  ========================================================================


// key is the sequenceNumber
static int
hashMe(uint32_t seqNumKey)
{
	int hash_value = 0;
	hash_value = seqNumKey % hashTableSize;	//HASH_TABLE_SIZE;

	return hash_value;
}

// the 'frameCounter' value HAS to be updated at insert and/or consumption time
int
hti_getNumberOfFrames(HashTableStruct_t* currentHashTableStruct)
{
	lockIt( &currentHashTableStruct->aHashTableMutex );

	int frameCounter = currentHashTableStruct->frameCounter;

	unlockIt( &currentHashTableStruct->aHashTableMutex );
	return frameCounter;
}

static bool
hti_isDuplicateFrame(uint32_t seqNum, uint32_t sequenceNumber)
{
	if( seqNum == sequenceNumber )
		return true;

	return false;
}

static void
reset( HashTableStruct_t* table )
{
	table->totalFramesReceived	=  0;
	table->frameCounter 		=  0;
	table->lastOutputSeqNum 	= -1;

	// Initialize all slots in this table:
	FrameSlot_t* thisTable = table->aHashTable;
	for(int i = 0; i< hashTableSize; ++i)
	{
		thisTable[i].occupied 			= false;
		thisTable[i].aFrame.seqNum   	= INITIAL_SEQ_NUM;
	}
}

void
hti_reset( HashTableStruct_t* table )
{
	lockIt(&table->aHashTableMutex);
	reset(table);
	unlockIt(&table->aHashTableMutex);
}

void
hti_init(HashTableStruct_t* table)
{
    //(void)log_set_level(LOG_LEVEL_WARNING);
	// Initialize this table's mutex:
	int resp = pthread_mutex_init(&table->aHashTableMutex, NULL);
	if(resp)
	{
    	log_add("pthread_mutex_init( aHashTable ) failure: %s\n", strerror(resp));
        log_flush_fatal();
        exit(EXIT_FAILURE);
	}

	resp = pthread_cond_init(&table->aHashTableCond, NULL);
	if(resp)
	{
		log_add("pthread_cond_init( aHashTable ) failure: %s\n", strerror(resp));
		log_flush_fatal();
		exit(EXIT_FAILURE);
	}

	(void) reset( table );
}


// Compares frame sitting in table with incoming frame using their seqNum value
static bool
isThisBeforeThat(uint32_t this, uint32_t that)
{
	// if sequence number has changed then frame in slot is older
	// unsigned arithmetic applies:
	return ( that - this ) > UINT32_MAX/2 ;
}

static bool
isEmpty(HashTableStruct_t* currentHashTableStruct)
{
	assert( pthread_mutex_trylock( &currentHashTableStruct->aHashTableMutex) );

	return currentHashTableStruct->frameCounter == 0;
}

bool
hti_isEmpty(HashTableStruct_t* currentHashTableStruct)
{
	// lock table
	lockIt( &currentHashTableStruct->aHashTableMutex );

	bool isEmptyState = isEmpty( currentHashTableStruct );

	unlockIt( &currentHashTableStruct->aHashTableMutex );
	return isEmptyState;
}
// insert a frame in this table and update attributes
// pre-condition: runMutex is LOCKed
// pre-condition: tableMutex is unLOCKed
int
hti_tryInsert(HashTableStruct_t* 	aTable,
			  uint32_t 				sequenceNumber,
			  uint16_t 				runNumber,
			  unsigned char* 		buffer, //<- buffer is entire SBN frame (not just data)
			  int 					frameBytes)
{
	if( frameBytes > SBN_FRAME_SIZE )
	{
    	log_add("Frame too large!\n");
    	log_warning("Frame too large!\n");
        log_flush(LOG_LEVEL_WARNING);
		return FRAME_TOO_LARGE;
	}

	// lock table
	(void) lockIt( &aTable->aHashTableMutex );

	int 			index			= hashMe( sequenceNumber );
	FrameSlot_t* 	aSlot 			= &aTable->aHashTable[ index ];
	Frame_t* 		aFrame 			= &aTable->aHashTable[ index ].aFrame;

	// Frame is valid to insert if slot is UNoccupied


	// Wait while all of these conditions are true
	int status = 0;
	while( aSlot->occupied  && aFrame->runNum == runNumber && sequenceNumber != aFrame->seqNum && 					// i.e. seq are different
		 (aTable->lastOutputSeqNum < 0 || ! isThisBeforeThat( sequenceNumber, aTable->lastOutputSeqNum )) )

	{
		pthread_cond_wait( &aTable->aHashTableCond, &aTable->aHashTableMutex);
	}

	log_info("Now out  of the WHILE loop... (sequenceNumber: % "PRIu32", lastOutputFrameSeqNum: % "PRId64" )", sequenceNumber, aTable->lastOutputSeqNum );

	log_info("Now out  of the WHILE loop...1");
	// slot is UNoccupied: Can insert
	if( !aSlot->occupied )
	{
		// slot IS empty: insert in it
		log_add("   -> Frame In: Seq# : %u \n", sequenceNumber);

		// fill in the attributes in this slot
		memcpy(aSlot->aFrame.data, buffer, frameBytes);
		aSlot->aFrame.seqNum 		= sequenceNumber;
		aSlot->aFrame.runNum 		= runNumber;

		// AND mark it as occupied
		aSlot->occupied 			= true;

		// consequently decrease the frameCounter for this table
		++(aTable->frameCounter);

		++totalFramesReceived;

		log_debug("      (Total frames received so far: %u)\n", totalFramesReceived);

		pthread_cond_broadcast(&aTable->aHashTableCond);

		// unlock table
		unlockIt( &aTable->aHashTableMutex );

		log_flush_info();
		return FRAME_INSERTED;
	}

	log_info("Now out  of the WHILE loop...2");
	if( aSlot->aFrame.runNum != runNumber )
	{
		log_error("Logic error: Incoming runNumber (%u) differs from hashTable's runNumber (%u)\n",
				runNumber, aSlot->aFrame.runNum);
		unlockIt( &aTable->aHashTableMutex );
		return DUPLICATE_FRAME;
	}

	log_info("Now out  of the WHILE loop...3");
	// Check if occupied with the same frame: ignore
	log_info("Duplicate: %s (s:%u:r%u) ", (aSlot->aFrame.seqNum == sequenceNumber)? "YES": "NO", sequenceNumber, runNumber);
	if(  sequenceNumber == aFrame->seqNum   )
	{
		log_info("Duplicate frame in the same run (r: %lu, s: %u) found! (skipping...)\n", runNumber, sequenceNumber);
		unlockIt( &aTable->aHashTableMutex );
		return DUPLICATE_FRAME;
	}

	// Frame came too late
	int64_t lastOutputFrameSeqNum 	= aTable->lastOutputSeqNum;
	if(  lastOutputFrameSeqNum >= 0 && isThisBeforeThat( sequenceNumber, lastOutputFrameSeqNum ))
	{
		log_notice("\nFrame (seqNum: %u) arrived too late. Increase blender's time-out? \
				 \n       (last seqNum: %u).\n\n", sequenceNumber, lastOutputFrameSeqNum);
		(void) unlockIt( &aTable->aHashTableMutex );
		// log_flush(LOG_LEVEL_WARNING);
		return FRAME_TOO_LATE;
	}
	log_info("Now out  of the WHILE loop...EXITing!");
}

void
hti_releaseOldest( HashTableStruct_t* impl, Frame_t* oldestFrame )
{
	lockIt( &impl->aHashTableMutex );

	int indexOfLastOutputFrame = hashMe( oldestFrame->seqNum );

	impl->aHashTable[ indexOfLastOutputFrame ].occupied = false;
	--(impl->frameCounter);

	pthread_cond_broadcast(&impl->aHashTableCond);
	unlockIt( &impl->aHashTableMutex );
}

/*
 * Populate oldestFrame and set the next lastOutput Frame's seqNum
 * param out:	oldestFrame
 *
 * return:	success = true, failure = false
 */
bool
hti_getOldestFrame(HashTableStruct_t* table, Frame_t* oldestFrame)
{
	bool success = false;

	// table mutex
	lockIt( &table->aHashTableMutex);
	if( isEmpty( table ) )
	{
		// debug:
		log_debug("\t (table is empty. Last seqNum: %u) \n", table->lastOutputSeqNum);
		// log_flush_warning();

		unlockIt( &table->aHashTableMutex);
		return false;
	}

	int lastOutputSeqNum = table->lastOutputSeqNum;
	int indexOfOldestFrame = -1;
	if( lastOutputSeqNum >=0 )
	{
		indexOfOldestFrame = hashMe( lastOutputSeqNum );
	}

	++indexOfOldestFrame;	// <- start from the next slot
	indexOfOldestFrame %= hashTableSize;	// cycle through the hash table

	FrameSlot_t* slots = table->aHashTable;

	// - advance lastOutput's seqNum -> call that function with the while loop

	// Search for the next occupied slot (frameCounter has been decremented already, so we start with >0)
	// a last minute frame can still be inserted during the while loop - hence table is unlocked.
	// (table is not empty)
	while( !slots[indexOfOldestFrame].occupied )
	{
		// count gaps here
		//...

		// TO-DO: count the number of gaps , number of frames, etc.  so far
		++framesMissedCount;
		// ...

		++indexOfOldestFrame;
		indexOfOldestFrame %= hashTableSize;
	}

	// update the oldest seqNum on this table
	table->lastOutputSeqNum	= slots[ indexOfOldestFrame ].aFrame.seqNum;
	// set oldestFrame frame (as output param) to slot[hashMe(lastOutput's seqNum) ]
	// oldestFrame = slots[ indexOfOldestFrame ].aFrame;
	oldestFrame->seqNum = slots[ indexOfOldestFrame ].aFrame.seqNum;
	oldestFrame->runNum = slots[ indexOfOldestFrame ].aFrame.runNum;
	memcpy( oldestFrame->data, slots[ indexOfOldestFrame ].aFrame.data, sizeof(oldestFrame->data));
	oldestFrame->nbytes = slots[ indexOfOldestFrame ].aFrame.nbytes;

//	int indexOfLastOutput = hti_hashMe( table->lastOutputSeqNum );
	// set oldestFrame frame (as output param) to slot[hashMe(lastOutput's seqNum) ]
	//oldestFrame = &slots[ indexOfLastOutput ].aFrame;

	success = true;

	unlockIt(&table->aHashTableMutex);

	return success;
}
