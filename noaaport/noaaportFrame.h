/*
 * noaaportFrame.h
 *
 *  Created on: Jan 12, 2022
 *      Author: miles
 */

#ifndef NOAAPORT_NOAAPORTFRAME_H_
#define NOAAPORT_NOAAPORTFRAME_H_

#define	SBN_FRAME_SIZE	5000

typedef struct aFrame {
    uint32_t        seqNum;
    uint16_t        runNum;
    char   			data[SBN_FRAME_SIZE];	// change
    unsigned		nbytes;
} Frame_t;

#endif /* NOAAPORT_NOAAPORTFRAME_H_ */
