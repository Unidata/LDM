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
      
    //  "extern" variables declarations
    //  ============= begin ==================


// A hashtable of sequence numbers and frame data
Frame_t frameHashTable[NUMBER_OF_RUNS][HASH_TABLE_SIZE];

FrameState_t    oldestFrame = {
    .index          = 0,
    .seqNum         = INITIAL_SEQ_NUM,
    .tableNum       = TABLE_NUM_1
};

pthread_mutex_t runMutex;

pthread_cond_t  cond = PTHREAD_COND_INITIALIZER;           

extern pthread_t    inputClientThread;
extern pthread_t    frameConsumerThread;

extern bool         hashTableIsFull_flag;
extern bool         highWaterMark_reached;

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
    return (whichRun == TABLE_NUM_1 ? numberOfFramesReceivedRun1 == HASH_TABLE_SIZE : 
                                      numberOfFramesReceivedRun2 == HASH_TABLE_SIZE);
}

static bool 
isHighWaterMarkReached(int whichRun) 
{
    return (whichRun == TABLE_NUM_1 ? numberOfFramesReceivedRun1 >= highWaterMark : 
                                      numberOfFramesReceivedRun2 >= highWaterMark);
}

static void 
incrementFramesReceived(int whichRun) 
{
    whichRun == TABLE_NUM_1 ? ++numberOfFramesReceivedRun1 : 
                              ++numberOfFramesReceivedRun2;
}

static void 
decrementFramesReceived(int whichRun) 
{
    whichRun == TABLE_NUM_1 ? --numberOfFramesReceivedRun1 : --numberOfFramesReceivedRun2;
}

// ==================================== API ============================================
// ============================== (visible to blender_clnt ) ===========================

bool 
isHashTableEmpty(int whichRun) 
{
    return (whichRun == TABLE_NUM_1 ? numberOfFramesReceivedRun1 == 0 : numberOfFramesReceivedRun2 == 0);
}

// pop the top frame from either hashTable as applicable...
// pre-condition: runMutex is UNLOCKED

unsigned char* 
popFrame()
{
    // Assumption:  if oldest frame belongs to table A and for some incongruous reason the slot is invalid
    //              then, looking for the next oldest frame should be performed in the same table A, up to
    //              finding it - after eventual gaps - or not finding it at all if the table has become empty.
    // However, not finding the oldest frame should never occur - by construction

    unsigned char* frameData;

    // bring visibility on these 2 values!! mutex
    // use runMutex
    pthread_mutex_lock(&runMutex);

    if( isHashTableEmpty(TABLE_NUM_1) && isHashTableEmpty(TABLE_NUM_2)) 
    {
        printf("\tNo frame available in either table... \n");
        //sleep(0.5);   // for testing
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
        printf("\n\t(Table #%d is empty! (gap in frame sequencing...!)\n", indexOfOldestSeq);

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
    
    pthread_mutex_lock(&frameHashTable[whichTable][indexOfOldestSeq].aFrameMutex);

    frameData = frameHashTable[whichTable][oldestFrame.index].frameData; // .frameData if we only return the frame data
    frameHashTable[whichTable][oldestFrame.index].occupied = false;

    pthread_mutex_unlock(&frameHashTable[whichTable][indexOfOldestSeq].aFrameMutex);

    printf("   => Frame Out: currentTable: %d,  Sequence# : %lu (@ %d) \n", 
        whichTable,  frameHashTable[whichTable][oldestFrame.index].seqNum, 
        oldestFrame.index);

    // increment index of oldest frame to the next - be it valid or not:
    ++indexOfOldestSeq;
    indexOfOldestSeq %= HASH_TABLE_SIZE;


    (void) decrementFramesReceived(whichTable);

    // set the next oldest frame in this table to the next slot if NOT full
    // if hashTable is NOT FULL: assign the next index to oldestFrame's index
    // hashTable IS NOT full cause just decrement its cardinality
    
    oldestFrame.index       = indexOfOldestSeq; // after index increase

    hashTableIsFull_flag    = false;
        // DEBUG: printf("\t- Hash Table Full: NO\n");
    
    
    pthread_cond_signal(&cond);

    pthread_mutex_unlock(&runMutex);
    return frameData;
}


// pre-cond: entering this function with locked runMutex.

static bool 
insertFrameIntoHashTable(  
            int             currentTable, 
            uint32_t        sequenceNumber, 
            unsigned char*  buffer, 
            uint16_t        frameBytes)                                     
{
    int index = hashMe(sequenceNumber);

    // DEBUG: 
    printf("   -> Frame In: currentTable: %d, Sequence# : %u (@ %d) \n", 
        currentTable, sequenceNumber, index);
        
    pthread_mutex_lock(&frameHashTable[currentTable][index].aFrameMutex);
    if( frameHashTable[currentTable][index].occupied )
    {
        ++collisionHits;
        printf("   -> Collision in buffer #%d for Sequence# : %u (%d collisions so far)\n", 
                currentTable, sequenceNumber, collisionHits);
        printf("Blender cannot keep up... Consider increasing the hash Tables' size. (currently, %d)\n", 
            HASH_TABLE_SIZE);
        
        pthread_mutex_unlock(&frameHashTable[currentTable][index].aFrameMutex);

        return false;   // collision
    }

    // insertIt
    
    memcpy(frameHashTable[currentTable][index].frameData, 
            buffer, new_max(frameBytes, SBN_FRAME_SIZE - 1));
    frameHashTable[currentTable][index].seqNum          = sequenceNumber;
    frameHashTable[currentTable][index].occupied        = true;

    // Set the first oldest Frame to initialize the oldestFrame object
    if( theVeryFirstFrame_flag ) 
    {
        oldestFrame.index           = index;
        oldestFrame.seqNum          = sequenceNumber; 
        oldestFrame.tableNum        = currentTable;

        theVeryFirstFrame_flag = false;
    }

    (void) incrementFramesReceived(currentTable);

    pthread_mutex_unlock(&frameHashTable[currentTable][index].aFrameMutex);
    
    return true;
}

// pre-condition: runMutex is UNLOCKED
int 
pushFrame(
        int             currentRunTable, 
        uint32_t        sequenceNumber, 
        unsigned char*  dataBlockStart, 
        uint16_t        dataBlockSize) 
{
    int cancelState;
    pthread_mutex_lock(&runMutex);

    // DEBUG: 
    printf("\n\n=================== InputClient Thread =======================\n");

    //sleep(1);
    // insert into proper hashTable (either for Run# 1 or Run# 2)
    if ( !insertFrameIntoHashTable( currentRunTable, 
                                    sequenceNumber, 
                                    dataBlockStart, 
                                    dataBlockSize) )
    {
        // setcancelstate??? remove?
        pthread_setcancelstate(cancelState, &cancelState);
        pthread_mutex_unlock(&runMutex);

        return -1;
    } 

    // Set the new variables after this insertion: this only concerns a hashTable being completely filled up 
    // .....................................................................................................

    

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
    pthread_mutex_unlock(&runMutex); 

    return true;     
}




