/*
 * noaaportFrame.h
 *
 *  Created on: Jan 12, 2022
 *      Author: miles
 */

#ifndef NOAAPORT_NOAAPORTFRAME_H_
#define NOAAPORT_NOAAPORTFRAME_H_

#include <stdint.h>

#define	SBN_FRAME_SIZE	5000

typedef uint_fast16_t RunNum_t;
typedef uint_fast32_t SeqNum_t;
typedef uint_fast16_t FrameSize_t;

typedef struct aFrame {
    SeqNum_t        seqNum;
    RunNum_t        runNum;
    char   			data[SBN_FRAME_SIZE];
    FrameSize_t		nbytes;
} Frame_t;

#endif /* NOAAPORT_NOAAPORTFRAME_H_ */
