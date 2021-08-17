#ifndef FRAME_FIFO_ADAPTER_DOT_H
#define FRAME_FIFO_ADAPTER_DOT_H

#include <inttypes.h>

#define FIN                         0
#define ONE_BILLION                 1000000000
#define HIGH_WATER_MARK             100             // 90% of HASH_TABLE_SIZE
#define LOW_WATER_MARK              50              // 50% of HASH_TABLE_SIZE
#define HASH_TABLE_SIZE             10 //15000      // CONDUIT frameRate (3500/s) * 2 * frameLatency input
#define TABLE_NUM_1                 0
#define TABLE_NUM_2                 1
#define NUMBER_OF_RUNS              2

#define MAX_SEQ_NUM                 UINT32_MAX      // (~(uint32_t)0)

#define PORT                        9127
#define SBN_FRAME_SIZE              5000
#define MIN_SOCK_TIMEOUT_MICROSEC   9000
#define ACCEPTABLE_GAPS_COUNT       10              // number of frames missed before starting reporting

#define NOAAPORT_NAMEDPIPE          "/tmp/noaaportIngesterPipe"

#define new_max(x,y) (((x) >= (y)) ? (x) : (y))

#define INITIAL_SEQ_NUM             0

typedef struct sockaddr_in SOCK4ADDR;

typedef struct Frame {
    pthread_mutex_t aFrameMutex;
    bool            occupied;
    uint16_t        runNum;  
    uint32_t        seqNum;
    unsigned char   sbnFrame[SBN_FRAME_SIZE];   
    int             socketId;
    int             frameIndex;
    int             tableNum;
} Frame_t;

typedef struct FrameState {
    uint32_t        seqNum;
    int             tableNum;
    int             index;
} FrameState_t;

#endif
