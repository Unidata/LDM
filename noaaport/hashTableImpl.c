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
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "misc.h"
#include "hashTableImpl.h"

//  ========================================================================

extern int hashTableSize;
//  ========================================================================

static int 		framesMissedCount 		= 0;
static uint32_t	totalFramesReceived 	= 0;

static int 		hti_hashMe(uint32_t seqNum);
//  ========================================================================


// key is the sequenceNumber
static int
hti_hashMe(uint32_t seqNumKey)
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

	// Initialize this table's mutex:
	int resp = pthread_mutex_init(&table->aHashTableMutex, NULL);
	if(resp)
	{
		printf("pthread_mutex_init( aHashTable ) failure: %s\n", strerror(resp));
		exit(EXIT_FAILURE);
	}
	(void) reset( table );
}


// Compares frame sitting in table with incoming frame using their seqNum value
static bool
hti_isThisBeforeThat(uint32_t this, uint32_t that)
{
	// if sequence number has changed then frame in slot is older
	// unsigned arithmetic applies:
	return ( that - this ) > UINT32_MAX/2 ;
}

static bool
hti_areEqual(uint32_t this, uint32_t that)
{
	return !hti_isThisBeforeThat( this , that ) && !hti_isThisBeforeThat( that , this );
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
		printf("Frame too large!\n");
		return FRAME_TOO_LARGE;
	}

	// lock table
	(void) lockIt( &aTable->aHashTableMutex );

	int 			index			= hti_hashMe( sequenceNumber );
	FrameSlot_t* 	aSlot 			= &aTable->aHashTable[ index ];
	Frame_t* 		aFrame 			= &aTable->aHashTable[ index ].aFrame;

	int64_t lastOutputFrameSeqNum 	= aTable->lastOutputSeqNum;

	// Set the index of the very first frame received in this table
	/*if(  lastOutputFrameSeqNum == -1) // invalid oldest seqNumber: set oldest to this seqNum
	{
		aTable->lastOutputSeqNum = sequenceNumber;
	}
*/
	//if not empty, return it.
	if( aSlot->occupied )
	{
		// input frame is too late to arrive
		// (its sequenceNumber is smaller than the current oldest frame's seqNum): ignore
		if ( hti_isThisBeforeThat(aSlot->aFrame.seqNum, sequenceNumber ) )
		{
			printf("Warning: frame (%ul) arrived too late (last seqNum: %ul): \
				table or timeout too small...\n", sequenceNumber, aSlot->aFrame.seqNum);
			unlockIt( &aTable->aHashTableMutex );
			return TABLE_TOO_SMALL;
		}

		// Check if occupied with the same frame: ignore
		if( hti_isDuplicateFrame(aSlot->aFrame.seqNum, sequenceNumber ) )
		{
			printf("Warning: Duplicate frame... %u\n", sequenceNumber);
			unlockIt( &aTable->aHashTableMutex );
			return DUPLICATE_FRAME;
		}
	}


/*
	// input frame is too late to arrive (its sequenceNumber is smaller than the current oldest frame's seqNum): ignore
	// this is to be checked regardless of its slot being 'occupied' or not.
	if ( lastOutputFrameSeqNum == -1 && lastOutputFrameSeqNum != sequenceNumber
			&& lastOutputFrameSeqNum - sequenceNumber <  UINT32_MAX/2 )
	{
		printf("Warning: frame (%ul) arrived too late (last seqNum: %u): table is too small or small timeout\n",
				sequenceNumber, lastOutputFrameSeqNum);
		return TABLE_TOO_SMALL;
	}

*/

	printf("   -> Frame In: Seq# : %u (@ %d) \n", sequenceNumber, index);

	// fill in the attributes in this slot
	memcpy(aSlot->aFrame.data, buffer, frameBytes);
	aSlot->aFrame.seqNum 		= sequenceNumber;
	aSlot->aFrame.runNum 		= runNumber;
	// AND mark it as occupied
	aSlot->occupied 			= true;


	++(aTable->frameCounter);
	++totalFramesReceived;

	printf("      (Total frames received so far: %u)\n", totalFramesReceived);

	// unlock table
	unlockIt( &aTable->aHashTableMutex );

	return FRAME_INSERTED;

}




void
hti_releaseOldest( HashTableStruct_t* impl, Frame_t* oldestFrame )
{
	lockIt( &impl->aHashTableMutex );

	int indexOfLastOutputFrame = hti_hashMe( oldestFrame->seqNum );

	impl->aHashTable[ indexOfLastOutputFrame ].occupied = false;
	--(impl->frameCounter);

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
		printf("table is empty (last seqNum: %u) \n", table->lastOutputSeqNum);

		unlockIt( &table->aHashTableMutex);
		return false;
	}

	int lastOutputSeqNum = table->lastOutputSeqNum;
	int indexOfOldestFrame = -1;
	if( lastOutputSeqNum >=0 )
	{
		indexOfOldestFrame = hti_hashMe( lastOutputSeqNum );
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
