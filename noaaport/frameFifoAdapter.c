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

#include <assert.h>

#include "frameFifoAdapter.h"
#include "blender.h"
      
    //  "extern" variables declarations
    //  ============= begin ==================
// A hashtable of sequence numbers and frame data
Frame_t         frameHashTable[NUMBER_OF_RUNS][HASH_TABLE_SIZE];

FrameState_t    oldestFrame = {
    .index          = 0,
    .seqNum         = INITIAL_SEQ_NUM,
    .tableNum       = TABLE_NUM_1
};
//  ============= end ==================

static  bool theVeryFirstFrame_flag         = true;

static  int totalFramesReceived             = 0;

static  int highWaterMark                   = (int) HIGH_WATER_MARK * HASH_TABLE_SIZE / 100;
static  int lowWaterMark                    = (int) LOW_WATER_MARK  * HASH_TABLE_SIZE / 100;

// These variables are under mutex:
static  uint16_t    currentRun              = 0;

static  int collisionHits                   = 0;
static  int framesMissedCount               = 0;



void
assertLockUnlock(pthread_mutex_t aMutex, bool testZero, bool lock)
{
    int resp;
    if (lock )
        resp = pthread_mutex_lock(&aMutex);
    else
        resp = pthread_mutex_unlock(&aMutex);

    if( !testZero )
        assert( resp );
    else
        assert( resp == 0 );
}

void
assertLock(pthread_mutex_t aMutex, bool testZero, bool unlock)
{
    assertLockUnlock(aMutex, testZero, true);
}


void
assertUnLock(pthread_mutex_t aMutex, bool testZero, bool unlock)
{
    assertLockUnlock(aMutex, testZero, false);
}

void
assertTryLock(pthread_mutex_t aMutex, bool testZero)
{
    int resp = pthread_mutex_trylock(&aMutex);
    
    if( !testZero )
        assert( resp );
    else
        assert( resp == 0 );
}

// key is the sequenceNumber
static int 
hashMe(uint32_t seqNumKey)
{
    int hash_value = 0;

    hash_value = seqNumKey % HASH_TABLE_SIZE; 

    return hash_value;
}

static bool 
isHashTableFull(int whichRun) 
{

    int resp = pthread_mutex_trylock(&runMutex);
    assert( resp );
    
    return (whichRun == TABLE_NUM_1 ? numberOfFramesReceivedRun1 == HASH_TABLE_SIZE : 
                                      numberOfFramesReceivedRun2 == HASH_TABLE_SIZE);
}

static bool 
isHighWaterMarkReached(int whichRun) 
{
    int resp = pthread_mutex_trylock(&runMutex);
    assert( resp );
    
    return (whichRun == TABLE_NUM_1 ? numberOfFramesReceivedRun1 >= highWaterMark : 
                                      numberOfFramesReceivedRun2 >= highWaterMark);
}


static bool 
isLowWaterMarkReached(int whichRun) 
{
    int resp = pthread_mutex_trylock(&runMutex);
    assert( resp );
    
    return (whichRun == TABLE_NUM_1 ? numberOfFramesReceivedRun1 <= lowWaterMark : 
                                      numberOfFramesReceivedRun2 <= lowWaterMark);
}

static void 
incrementFramesReceived(int whichRun) 
{
    int resp = pthread_mutex_trylock(&runMutex);
    assert( resp );

    whichRun == TABLE_NUM_1 ? ++numberOfFramesReceivedRun1 : 
                              ++numberOfFramesReceivedRun2;
}

static void 
decrementFramesReceived(int whichRun) 
{
    int resp = pthread_mutex_trylock(&runMutex);
    assert( resp );
    
    whichRun == TABLE_NUM_1 ? --numberOfFramesReceivedRun1 : --numberOfFramesReceivedRun2;
}

// ==================================== API ============================================
// ============================== (visible to blender_clnt ) ===========================

bool 
isHashTableEmpty(int whichRun) 
{
    //assertTryLock(runMutex, false);    // assert( resp )    
    int resp = pthread_mutex_trylock(&runMutex);
    assert( resp );
 

    return (whichRun == TABLE_NUM_1 ? numberOfFramesReceivedRun1 == 0 : numberOfFramesReceivedRun2 == 0);
}

// pop the top frame from either hashTable as applicable...
// pre-condition: runMutex is LOCKED
// post-conditions: 
//      - runMutex is still LOCKED
//      - aFrameMutex is LOCKED (will get unlocked AFTER sending out the frame)
Frame_t *
popFrameSlot()
{
    // Assumption:  if oldest frame belongs to table A and for some incongruous reason the slot is invalid
    //              then, looking for the next oldest frame should be performed in the same table A, up to
    //              finding it - after eventual gaps - or not finding it at all if the table has become empty.
    // However, not finding the oldest frame should never occur - by construction
    int resp = pthread_mutex_trylock(&runMutex);
    assert( resp );

    if( isHashTableEmpty(TABLE_NUM_1) && isHashTableEmpty(TABLE_NUM_2)) 
    {
        printf("\tNo frame available in either table... \n");
        // runMutex is STILL LOCKED here
        return NULL;
    }
    
    int indexOfOldestSeq    = oldestFrame.index;        // oldestFrame and oldest sequence are interchangeable as far as the concept.
    int whichTable          = oldestFrame.tableNum;

    // DEBUG:printf("\n   => popFrameSlot( oldestFrame ): currentTable: %d,  Sequence# : %lu (%d)\n", 
    //    whichTable,  frameHashTable[whichTable][oldestFrame.index].seqNum, 
    //    oldestFrame.index);

    // Verify that this table entry is NOT empty
    while( ! frameHashTable[whichTable][indexOfOldestSeq].occupied )
    {
        printf("\n\t=> Table #%d slot: %d is empty! (gap in frame sequencing...!)\n", 
            whichTable, indexOfOldestSeq);

        // TO-DO: count the number of gaps , number of frames, etc.  so far
        ++framesMissedCount;
        
        if (framesMissedCount > ACCEPTABLE_GAPS_COUNT)
        {
            printf("\n\tMissed number of frames: %d\n", framesMissedCount);
            return NULL;
        }

        // ...

        // compute the next index of oldest frame
        ++indexOfOldestSeq;
        indexOfOldestSeq %= HASH_TABLE_SIZE;

        printf("\n\t(Trying next oldest index: %d)\n", indexOfOldestSeq);
        
    }
    
    /* Does not seem to work: it blocks the input thread!
    
    // lock the frame slot: 
    resp = pthread_mutex_lock(&frameHashTable[whichTable][indexOfOldestSeq].aFrameMutex);
    assert( resp == 0 );
    
    */
 
    frameHashTable[whichTable][indexOfOldestSeq].occupied   = false;
    frameHashTable[whichTable][indexOfOldestSeq].tableNum   = whichTable;
    frameHashTable[whichTable][indexOfOldestSeq].frameIndex = indexOfOldestSeq;

    printf("   => Frame Out: currentTable: %d,  Sequence# : %lu (@ %d) \n", 
                whichTable,  
                frameHashTable[whichTable][indexOfOldestSeq].seqNum, 
                indexOfOldestSeq);

    // increment index of oldest frame to the next - be it valid or not:
    ++indexOfOldestSeq;
    indexOfOldestSeq %= HASH_TABLE_SIZE;

    (void) decrementFramesReceived(whichTable);

    // set the next oldest frame in this table to the next slot
    // i.e. assign the next index to oldestFrame's index attribute
    // (hashTable IS NOT full cause just decremented its entries index)
    oldestFrame.index       = indexOfOldestSeq; // <- Note: after index increase
    // oldestFrame.tableNum has not changed
 
    // if( isLowWaterMarkReached(whichTable) ) lowWaterMark_touched   = true;  

    pthread_cond_broadcast(&cond);

    // this returned frame slot is still protected.
    /*Frame_t *aFrameSlot = malloc(sizeof(frameSlot)); 
    memcpy(aFrameSlot, frameHashTable[whichTable][indexOfOldestSeq], 
        sizeof(frameHashTable[whichTable][indexOfOldestSeq]));
    */
    return &frameHashTable[whichTable][indexOfOldestSeq]; 
}

static bool
isSlotOccupied( uint32_t seqNum, 
                uint32_t sequenceNumber, 
                uint16_t runNum, 
                uint16_t runNumber)
{
    return ( seqNum == sequenceNumber && runNum == runNumber) ;
}

static bool
slotHasNewerFrame(uint32_t aFrameSeqNum, 
                  uint32_t sequenceNumber, 
                  uint16_t aFrameRunNum, 
                  uint16_t runNumber) 
{
    if(aFrameRunNum == runNumber)
        return aFrameSeqNum < sequenceNumber;
    
    return false;   // if run number has changed then frame in slot is older
}

static bool
slotHasOlderFrame(uint32_t aFrameSeqNum, 
                  uint32_t sequenceNumber, 
                  uint16_t aFrameRunNum, 
                  uint16_t runNumber) 
{
    return !slotHasNewerFrame(aFrameSeqNum, sequenceNumber, aFrameRunNum, runNumber);
}

static void
insertFrame(int         currentTable, 
            uint32_t    sequenceNumber, 
            unsigned char* buffer,     //<- buffer is the entire SBN frame (not just data)
            int         frameBytes, 
            int         index, 
            int         frameSocketId)
{
    //debug && 
    printf("   -> Frame In: socketId: %d, table: %d, Seq# : %u (@ %d), \n", 
        frameSocketId, currentTable, sequenceNumber, index);

    memcpy(frameHashTable[currentTable][index].sbnFrame, 
            buffer, new_max(frameBytes, SBN_FRAME_SIZE - 1));
    
    frameHashTable[currentTable][index].seqNum          = sequenceNumber;
    frameHashTable[currentTable][index].occupied        = true;
    frameHashTable[currentTable][index].socketId        = frameSocketId;

    // Set the first oldest Frame to initialize the oldestFrame object
    if( theVeryFirstFrame_flag ) 
    {
        oldestFrame.index           = index;
        oldestFrame.seqNum          = sequenceNumber; 
        oldestFrame.tableNum        = currentTable;

        theVeryFirstFrame_flag = false;
    }

    (void) incrementFramesReceived(currentTable);
}

// pre-cond: entering this function with UNLOCKED runMutex.


// pre-condition: runMutex      is LOCKED 
// pre-condition: aFrameMutex   is unLOCKED

// post-condition: runMutex      is LOCKED 
// post-condition: aFrameMutex   is unLOCKED
static void
tryInsertFrame( int         currentTable, 
            uint32_t        sequenceNumber, 
            uint32_t        runNumber, 
            unsigned char*  buffer, 
            uint16_t        frameBytes, 
            int             frameSocketId) 
{

    int index           = hashMe(sequenceNumber);
    Frame_t * aFrame    = &frameHashTable[currentTable][index];

    int resp            = pthread_mutex_lock(&frameHashTable[currentTable][index].aFrameMutex);
    assert( resp == 0 );
    
    // not occupied
    if( ! isSlotOccupied(aFrame->seqNum, sequenceNumber, aFrame->runNum, runNumber) )
    {
        
        insertFrame(currentTable, sequenceNumber, buffer, frameBytes, index, frameSocketId);
    }
    else if( slotHasOlderFrame(aFrame->seqNum, sequenceNumber, aFrame->runNum, runNumber) )
    {
        printf("Found older frame in slot. Buffer is likely too small or timeout is too large\n");
    }
    else if( slotHasNewerFrame(aFrame->seqNum, sequenceNumber, aFrame->runNum, runNumber) )
    {
        printf("Found newer frame in slot. Buffer is likely too small or timeout is too large\n");
    }

    resp = pthread_mutex_unlock(&frameHashTable[currentTable][index].aFrameMutex);
    assert( resp == 0);
}


// pre-condition: runMutex is UNLOCKED
int 
pushFrame(
        int             currentRunTable, 
        uint32_t        sequenceNumber, 
        uint16_t        runNumber, 
        unsigned char*  frameBuffer, 
        uint16_t        frameSize,
        int             frameSocketId) 
{
    int cancelState;
    int status = pthread_mutex_lock(&runMutex);
    assert( status == 0 ); //  do it for all locks

    // DEBUG: 
    debug && printf("\n\n-> -> -> -> -> -> -> InputClient Thread   -> -> -> -> -> -> -> ->\n");
    
    // insert into proper hashTable (either for Run# 1 or Run# 2)
    (void) tryInsertFrame(   currentRunTable, 
                            sequenceNumber, 
                            runNumber, 
                            frameBuffer, 
                            frameSize,
                            frameSocketId);
   
    if( isHighWaterMarkReached(TABLE_NUM_1) || isHighWaterMarkReached(TABLE_NUM_2) ) 
    {
        highWaterMark_reached   = true;  // holds for both hashTable!
        //printf("\t- high Water Mark reached: YES\n");
    }

    pthread_cond_broadcast(&cond);
    int resp = pthread_mutex_unlock(&runMutex);
    assert( resp ==  0);

    pthread_setcancelstate(cancelState, &cancelState);

    return true;     
}




