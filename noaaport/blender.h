#ifndef BLENDER_DOT_H
#define BLENDER_DOT_H



// ==================== extern ======================
// A hashtable of sequence numbers and frame data
extern Frame_t          frameHashTable[NUMBER_OF_RUNS][HASH_TABLE_SIZE];
extern pthread_mutex_t 	runMutex;
extern pthread_mutex_t  aFrameMutex;
extern pthread_cond_t   cond;
extern FrameState_t     oldestFrame;

extern bool         	hashTableIsFull_flag;
extern bool         	highWaterMark_reached;

//===================================================


#endif