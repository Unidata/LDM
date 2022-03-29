/*
 * noaaportFrame.h
 *
 *  Created on: Jan 12, 2022
 *      Author: miles
 */

#ifndef NOAAPORT_NOAAPORTFRAME_H_
#define NOAAPORT_NOAAPORTFRAME_H_

#include <stdint.h>

#define SBN_FRAME_SIZE 5000

typedef uint16_t BlkNum_t;
#define PRI_BLK_NUM PRIu16
#define BLK_NUM_MAX UINT16_MAX

typedef uint16_t RunNum_t;
#define PRI_RUN_NUM PRIu16
#define RUN_NUM_MAX UINT16_MAX

typedef uint32_t SeqNum_t;
#define PRI_SEQ_NUM PRIu32
#define SEQ_NUM_MAX UINT32_MAX

typedef uint16_t FrameSize_t;
#define PRI_FRAME_SIZE PRIu16

typedef struct aFrame {
<<<<<<< HEAD
    unsigned        prodSeqNum;
    unsigned        dataBlockNum;
=======
    SeqNum_t        prodSeqNum;
    BlkNum_t        dataBlockNum;
>>>>>>> branch 'find_next_frame' of git@github.com:Unidata/LDM.git
    char            data[SBN_FRAME_SIZE];
    FrameSize_t     nbytes;
} Frame_t;

#endif /* NOAAPORT_NOAAPORTFRAME_H_ */
