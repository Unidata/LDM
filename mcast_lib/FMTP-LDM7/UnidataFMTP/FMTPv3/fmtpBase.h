/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      fmtpBase.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Oct 7, 2014
 *
 * @section   LICENSE
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief     Define the interfaces of FMTPv3 basics.
 *
 * Definition of control message definition, header struct and message length.
 */


#ifndef FMTP_FMTPV3_FMTPBASE_H_
#define FMTP_FMTPV3_FMTPBASE_H_


#include <stdint.h>
#include <string.h>
#include <strings.h>

//#define PRODNUM 207684
#define PRODNUM 20
#define MODBASE PRODNUM
#define EXPTRUN 1

/**
 * struct of Fmtp header
 */
typedef struct FmtpPacketHeader {
    uint32_t   prodindex;    /*!< identify both file and memdata by prodindex. */
    uint32_t   seqnum;
    uint16_t   payloadlen;
    uint16_t   flags;
} FmtpHeader;


/**
 * struct of Fmtp retx-request-message
 */
typedef struct FmtpRetxReqMessage {
    uint32_t startpos;
    uint16_t length;
} RetxReqMsg;


/* constants used by both sender and receiver */
const int MIN_MTU         = 1500;
const int FMTP_HEADER_LEN = sizeof(FmtpHeader);
const int RETX_REQ_LEN    = sizeof(RetxReqMsg);

/* non-constants, could change with MTU */
const int MTU                 = MIN_MTU;
const int MAX_FMTP_PACKET_LEN = MTU - 20 - 20; /* exclude IP and TCP header */
const int FMTP_DATA_LEN       = MAX_FMTP_PACKET_LEN - FMTP_HEADER_LEN;
/* sizeof(uint32_t) for BOPMsg.prodsize, sizeof(uint16_t) for BOPMsg.metasize */
const int AVAIL_BOP_LEN       = FMTP_DATA_LEN - sizeof(uint32_t) - sizeof(uint16_t);


/**
 * structure of Begin-Of-Product message
 */
typedef struct FmtpBOPMessage {
    uint32_t   prodsize;     /*!< support 4GB maximum */
    uint16_t   metasize;
    char       metadata[AVAIL_BOP_LEN];
    /* Be aware this default constructor could implicitly create a new BOP */
    FmtpBOPMessage() : prodsize(0), metasize(0), metadata() {}
} BOPMsg;


const uint16_t FMTP_BOP       = 0x0001;
const uint16_t FMTP_EOP       = 0x0002;
const uint16_t FMTP_MEM_DATA  = 0x0004;
const uint16_t FMTP_RETX_REQ  = 0x0008;
const uint16_t FMTP_RETX_REJ  = 0x0010;
const uint16_t FMTP_RETX_END  = 0x0020;
const uint16_t FMTP_RETX_DATA = 0x0040;
const uint16_t FMTP_BOP_REQ   = 0x0080;
const uint16_t FMTP_RETX_BOP  = 0x0100;
const uint16_t FMTP_EOP_REQ   = 0x0200;
const uint16_t FMTP_RETX_EOP  = 0x0400;


/** For communication between mcast thread and retx thread */
const int MISSING_BOP  = 1;
const int MISSING_DATA = 2;
const int MISSING_EOP  = 3;
const int SHUTDOWN     = 4;
typedef struct recvInternalRetxReqMessage {
    int reqtype;
    uint32_t prodindex;
    uint32_t seqnum;
    uint16_t payloadlen;
} INLReqMsg;


/** a structure defining parameters for each product, prodindex : sleeptime */
typedef struct timerParameter {
    uint32_t prodindex;
    double   seconds;
} timerParam;


class fmtpBase {
public:
    fmtpBase();
    ~fmtpBase();

private:
};


#endif /* FMTP_FMTPV3_FMTPBASE_H_ */
