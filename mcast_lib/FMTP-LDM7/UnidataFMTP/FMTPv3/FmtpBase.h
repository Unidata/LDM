/**
 * Copyright (C) 2021 University of Virginia. All rights reserved.
 *
 * @file      fmtpBase.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @author    Steven R. Emmerson <emmerson@ucar.edu>
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

#include <ctime>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

//#define PRODNUM 207684
#define PRODNUM 20
#define MODBASE PRODNUM
#define EXPTRUN 1

/**
 * struct of Fmtp header
 */
typedef struct FmtpPacketHeader {
    uint32_t   prodindex;       ///< identify both file and memdata by prodindex
    uint32_t   seqnum;          ///< Byte-offset of payload in file
    uint16_t   payloadlen;      ///< Length of payload in bytes
    uint16_t   flags;
} FmtpHeader;
const int FMTP_HEADER_LEN = sizeof(FmtpHeader); // DANGER! sizeof() use!

/**
 * struct of Fmtp retx-request-message
 */
typedef struct FmtpRetxReqMessage {
    uint32_t startpos;
    uint16_t length;
} RetxReqMsg;

const int MIN_MTU         = 1500; // Maximum ethernet frame payload

/* non-constants, could change with MTU */
const int MTU                 = MIN_MTU;
const int MAX_FMTP_PACKET     = (MTU - 20 - 20); /* exclude IP and TCP header */

typedef union {
    struct {
        FmtpHeader header;     ///< FMTP header in network byte-order
        char       payload[1]; ///< FMTP payload (excludes any MAC)
    };
    char bytes[MAX_FMTP_PACKET];
} FmtpPacket;

/// Most significant seconds, least significant seconds, nanoseconds
typedef uint32_t StartTime[3];

/**
 * structure of Begin-Of-Product message
 */
#if 0
typedef struct FmtpBOPMessage {
    StartTime startTime;    ///< Start of transmission of product
    uint32_t  prodsize;     /*!< support 4GB maximum */
    uint16_t  metasize;
    char      metadata[MAX_FMTP_PACKET]; ///< Oversize
    /* Be aware this default constructor could implicitly create a new BOP */
    FmtpBOPMessage() : prodsize(0), metasize(0), metadata() {}
} BOPMsg;
#else
typedef struct FmtpBOPMessage {
    union {
        struct {
            StartTime startTime;    ///< Start of transmission of product
            uint32_t  prodsize;     /*!< support 4GB maximum */
            uint16_t  metasize;
            char      metadata[1];
        };
        char bytes[MAX_FMTP_PACKET]; ///< Oversize
    };

    static const unsigned HEADER_SIZE = sizeof(StartTime) + sizeof(uint32_t) +
            sizeof(uint16_t);

    /* Be aware this default constructor could implicitly create a new BOP */
    FmtpBOPMessage() : prodsize(0), metasize(0) {}
} BOPMsg;
#endif

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
typedef enum {
    MISSING_BOP = 1,
    MISSING_DATA,
    MISSING_EOP,
	RETX_EOP,
    SHUTDOWN
} ReqType;
typedef struct recvInternalRetxReqMessage {
    ReqType  reqtype;
    uint32_t prodindex;
    uint32_t seqnum;
    uint16_t payloadlen;
} INLReqMsg;


/** a structure defining parameters for each product, prodindex : sleeptime */
typedef struct timerParameter {
    uint32_t prodindex;
    double   seconds;
} timerParam;


/**
 * Class for holding runtime (i.e., not compile-time) constants.
 */
class FmtpBase {
public:
    const unsigned MAC_SIZE;         ///< MAC size in bytes
    const unsigned MAX_PAYLOAD;      ///< Maximum payload in bytes (excl. MAC)
    const unsigned MAX_BOP_METADATA; ///< Maximum BOP metadata in bytes

    FmtpBase();
    ~FmtpBase();

private:
};


#endif /* FMTP_FMTPV3_FMTPBASE_H_ */
