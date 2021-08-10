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
static  int numberOfFramesReceivedRun1      = 0;
static  int numberOfFramesReceivedRun2      = 0;
static  int highWaterMark                   = (int) HIGH_WATER_MARK * HASH_TABLE_SIZE / 100;

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
unsigned char* 
popFrame()
{
    // Assumption:  if oldest frame belongs to table A and for some incongruous reason the slot is invalid
    //              then, looking for the next oldest frame should be performed in the same table A, up to
    //              finding it - after eventual gaps - or not finding it at all if the table has become empty.
    // However, not finding the oldest frame should never occur - by construction
    int resp = pthread_mutex_trylock(&runMutex);
    assert( resp );

    
    unsigned char* frameData;

    // bring visibility on these 2 values!! mutex
    // use runMutex
    // pthread_mutex_lock(&runMutex);

    if( isHashTableEmpty(TABLE_NUM_1) && isHashTableEmpty(TABLE_NUM_2)) 
    {
        printf("\tNo frame available in either table... \n");
        // runMutex is STILL LOCKED here
        return NULL;
    }
    
    int indexOfOldestSeq    = oldestFrame.index;        // oldestFrame and oldest sequence are interchangeable as far as the concept.
    int whichTable          = oldestFrame.tableNum;

    // DEBUG:printf("\n   => PopFrame( oldestFrame ): currentTable: %d,  Sequence# : %lu (%d)\n", 
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
        }

        // ...

        // compute the next index of oldest frame
        ++indexOfOldestSeq;
        indexOfOldestSeq %= HASH_TABLE_SIZE;

        printf("\n\t(Trying next oldest index: %d)\n", indexOfOldestSeq);
        
    }
    aFrameMutex = frameHashTable[whichTable][indexOfOldestSeq].aFrameMutex;
    pthread_mutex_lock(&aFrameMutex);

    resp = pthread_mutex_trylock(&aFrameMutex);
    assert( resp );

    frameData = frameHashTable[whichTable][indexOfOldestSeq].frameData; 
    frameHashTable[whichTable][indexOfOldestSeq].occupied = false;

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

    // DEBUG: printf("\t- Hash Table Full: NO\n");
    hashTableIsFull_flag    = false;
       
    pthread_cond_signal(&cond);

    return frameData; // this returned frameData is still protected.
}


// pre-cond: entering this function with locked runMutex.

static bool 
insertFrameIntoHashTable(  
            int             currentTable, 
            uint32_t        sequenceNumber, 
            uint32_t        runNumber, 
            unsigned char*  buffer, 
            uint16_t        frameBytes)                                     
{
    bool inserted = false;

    int index = hashMe(sequenceNumber);

    // Frame_t aFrame = frameHashTable[currentTable][index];
    // DEBUG: 
    printf("   -> Frame In: currentTable: %d, Sequence# : %u (@ %d) \n", 
        currentTable, sequenceNumber, index);
        
    Frame_t *           aFrame      = &frameHashTable[currentTable][index];
    pthread_cond_t      aFrameCond  = aFrame->aFrameCond;
    pthread_mutex_t *   aFrameMutex = &aFrame->aFrameMutex;
    uint32_t            seqNum      = aFrame->seqNum;
    uint16_t            runNum      = aFrame->runNum;
    int resp = pthread_mutex_lock(&aFrame->aFrameMutex);
    assert( resp == 0);

    int hit = 0;
        // case 2: are we overwriting older - not yet consumed - frames? Then true collision! 
        // Wait and allow consumer to free this occupied slot
    while( aFrame->occupied && !( seqNum == sequenceNumber && runNum == runNumber) )
    {
        hit = 1;
        int status = pthread_cond_wait(&aFrameCond, aFrameMutex);

        assert(status == 0 );
    }


    if(hit)
    {
        ++collisionHits;
        printf("   -> Collision in buffer #%d for Sequence# : %u (%d collisions so far)\n", 
                    currentTable, sequenceNumber, collisionHits);
        printf("Blender cannot keep up... Consider increasing the hash Tables' size. (currently, %d)\n", 
                    HASH_TABLE_SIZE);
     }   

     // not occupied
    if(! ( seqNum == sequenceNumber && runNum == runNumber) )
    {
        // insertIt            
        memcpy(aFrame->frameData, 
                buffer, new_max(frameBytes, SBN_FRAME_SIZE - 1));
        aFrame->seqNum          = sequenceNumber;
        aFrame->occupied        = true;

        // Set the first oldest Frame to initialize the oldestFrame object
        if( theVeryFirstFrame_flag ) 
        {
            oldestFrame.index           = index;
            oldestFrame.seqNum          = sequenceNumber; 
            oldestFrame.tableNum        = currentTable;

            theVeryFirstFrame_flag = false;
        }

        (void) incrementFramesReceived(currentTable);

        pthread_cond_signal(&aFrame->aFrameCond);
        inserted = true;
    }
    

    pthread_mutex_unlock(&aFrame->aFrameMutex);
    
    return inserted;
}
    
    


// pre-condition: runMutex is UNLOCKED
int 
pushFrame(
        int             currentRunTable, 
        uint32_t        sequenceNumber, 
        uint16_t        runNumber, 
        unsigned char*  dataBlockStart, 
        uint16_t        dataBlockSize) 
{
    int cancelState;
    int status = pthread_mutex_lock(&runMutex);
    assert( status == 0 ); //  do it for all locks

    // DEBUG: 
    printf("\n\n-> -> -> -> -> -> -> InputClient Thread   -> -> -> -> -> -> -> ->\n");
    
// COLLISION (different frames) , SUCCESS, FAILURE (same frame: collision is ok)
    // insert into proper hashTable (either for Run# 1 or Run# 2)
    if ( !insertFrameIntoHashTable( currentRunTable, 
                                    sequenceNumber, 
                                    runNumber, 
                                    dataBlockStart, 
                                    dataBlockSize) )
    {
        // setcancelstate??? remove?
        pthread_setcancelstate(cancelState, &cancelState);
        int resp = pthread_mutex_unlock(&runMutex);
        assert( resp ==  0);

        return -1;
    } 

    // Set the new variables after this insertion: this only concerns a hashTable 
    // being completely filled up 
    // .....................................................................................................

 
 // TO REMOVE!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!   

    // if one of the hashTable is full we must consume frames from concerned hashTable
    if( isHashTableFull(TABLE_NUM_1) || isHashTableFull(TABLE_NUM_2) ) 
    {
        hashTableIsFull_flag   = true;  // holds for both hashTable!
        printf("\t- Hash Table Full: YES\n");
    }
    
    // 
    if( isHighWaterMarkReached(TABLE_NUM_1) || isHighWaterMarkReached(TABLE_NUM_2) ) 
    {
        highWaterMark_reached   = true;  // holds for both hashTable!
        printf("\t- high Water Mark reached: YES\n");
    }

    pthread_cond_signal(&cond);

    int resp = pthread_mutex_unlock(&runMutex);
    assert( resp ==  0);

    return true;     
}




