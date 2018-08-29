/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      senderMetadata.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Nov 28, 2014
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
 * @brief     Define the interfaces and structures of FMTPv3 sender side
 *            retransmission metadata.
 *
 * FMTPv3 sender side retransmission data structures and interfaces, including
 * the timeout period value, unfinished set and product pointers.
 */


#ifndef FMTP_SENDER_SENDERMETADATA_H_
#define FMTP_SENDER_SENDERMETADATA_H_


#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

#include "fmtpBase.h"
#include "TcpSend.h"


typedef std::chrono::high_resolution_clock HRclock;

struct RetxMetadata {
    uint32_t       prodindex;
    /* recording the whole product size (for timeout factor use) */
    uint32_t       prodLength;
    uint16_t       metaSize;          /*!< metadata size               */
    void*          metadata;          /*!< metadata pointer            */
    double         retxTimeoutPeriod; /*!< timeout time in seconds     */
    void*          dataprod_p;        /*!< pointer to the data product */
    /* unfinished receiver set indexed by socket id */
    std::set<int>  unfinReceivers;
    /* indicates the RetxMetadata is in use */
    bool           inuse;
    /* indicates the RetxMetadata should be removed */
    bool           remove;

    RetxMetadata(): prodindex(0), prodLength(0), metaSize(0),
                    metadata(NULL), retxTimeoutPeriod(99999999999.0),
                    dataprod_p(NULL), inuse(false), remove(false) {}
    ~RetxMetadata() {
        delete[] (char*)metadata;
        metadata = NULL;
        /**
         * TODO: put a callback here to notify the application to
         * release the dataprod_p.
         */
        dataprod_p = NULL;
    }

    /* copy constructor of RetxMetadata */
    RetxMetadata(const RetxMetadata& meta)
    :
        prodindex(meta.prodindex),
        prodLength(meta.prodLength),
        metaSize(meta.metaSize),
        retxTimeoutPeriod(meta.retxTimeoutPeriod),
        unfinReceivers(meta.unfinReceivers),
        inuse(meta.inuse),
        remove(meta.remove)
    {
        /**
         * creates a copy of the metadata on heap,
         * points the metadata pointer to the copy.
         */
        char* metadata_ptr = new char[metaSize];
        std::memcpy(metadata_ptr, &meta.metadata, metaSize);
        metadata = metadata_ptr;
    }
};


class senderMetadata {
public:
    senderMetadata();
    ~senderMetadata();

    void addRetxMetadata(RetxMetadata* ptrMeta);
    bool clearUnfinishedSet(uint32_t prodindex, int retxsockfd,
                            TcpSend* tcpsend);
    RetxMetadata* getMetadata(uint32_t prodindex);
    void notifyUnACKedRcvrs(uint32_t prodindex, FmtpHeader* header,
                            TcpSend* tcpsend);
    bool releaseMetadata(uint32_t prodindex);
    bool rmRetxMetadata(uint32_t prodindex);

private:
    /* first: prodindex; second: pointer to metadata of the specified prodindex */
    std::map<uint32_t, RetxMetadata*> indexMetaMap;
    std::mutex                        indexMetaMapLock;
};


#endif /* FMTP_SENDER_SENDERMETADATA_H_ */
