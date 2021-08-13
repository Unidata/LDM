#ifndef BLENDER_DOT_H
#define BLENDER_DOT_H


extern bool debug;
// ==================== extern ======================
// A hashtable of sequence numbers and frame data
extern Frame_t          frameHashTable[NUMBER_OF_RUNS][HASH_TABLE_SIZE];
extern pthread_mutex_t 	runMutex;
extern pthread_mutex_t  aFrameMutex;
extern pthread_cond_t   cond;

extern bool         	highWaterMark_reached;
extern bool 			lowWaterMark_touched;

extern int 				numberOfFramesReceivedRun1;
extern int 				numberOfFramesReceivedRun2;
//===================================================

#endif