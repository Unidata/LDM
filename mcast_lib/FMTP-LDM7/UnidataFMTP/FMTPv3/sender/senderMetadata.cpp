/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      senderMetadata.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @author    Steve Emmerson <emmerson@ucar.edu>
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
 * @brief     Implement the interfaces and structures of FMTPv3 sender side
 *            retransmission metadata.
 *
 * FMTPv3 sender side retransmission metadata method functions. It supports
 * add/rm, query and modify operations.
 */


#include "senderMetadata.h"

#include <algorithm>


#ifndef NULL
    #define NULL 0
#endif


/**
 * Construct the senderMetadata class
 *
 * @param[in] none
 */
senderMetadata::senderMetadata()
{
}


/**
 * Destruct the senderMetadata class. Clear the whole prodindex-metadata map.
 *
 * @param[in] none
 */
senderMetadata::~senderMetadata()
{
    std::unique_lock<std::mutex> lock(indexMetaMapLock);
    for (std::map<uint32_t, RetxMetadata*>::iterator it = indexMetaMap.begin();
         it != indexMetaMap.end(); ++it) {
        delete(it->second);
    }
    indexMetaMap.clear();
}


/**
 * Add the new RetxMetadata entry into the prodindex-RetxMetadata map. A mutex
 * lock is added to ensure no conflict happening when adding a new entry.
 *
 * @param[in] ptrMeta           A pointer to the new RetxMetadata struct
 */
void senderMetadata::addRetxMetadata(RetxMetadata* ptrMeta)
{
    std::unique_lock<std::mutex> lock(indexMetaMapLock);
    indexMetaMap[ptrMeta->prodindex] = ptrMeta;
}


/**
 * Remove the particular receiver identified by the retxsockfd from the
 * finished receiver set. And check if the set is empty after the operation.
 * If it is, then remove the whole entry from the map. Otherwise, just clear
 * that receiver.
 *
 * @param[in] prodindex         product index of the requested product
 * @param[in] retxsockfd        sock file descriptor of the retransmission tcp
 *                              connection.
 * @return    True if RetxMetadata is removed, otherwise false.
 */
bool senderMetadata::clearUnfinishedSet(uint32_t prodindex, int retxsockfd,
                                        TcpSend* tcpsend)
{
    bool                        prodRemoved;
    std::lock_guard<std::mutex> lock(indexMetaMapLock);
    auto                        metaIter = indexMetaMap.find(prodindex);

    if (metaIter != indexMetaMap.end()) {
        auto  metaData = metaIter->second;
        auto& socks = metaData->unfinReceivers;

         socks.erase(retxsockfd);

        /* find possible legacy offline receivers and erase from set */
        for (auto unfinSockIter = socks.begin(); unfinSockIter != socks.end();
                ++unfinSockIter) {
            // Socket-list should not be empty */
            if (!tcpsend->isMember(*unfinSockIter))
                socks.erase(unfinSockIter); // Conforming C++11:
        }

        if (socks.empty()) {
            if (metaData->inuse) {
                if (metaData->remove) {
                    /**
                     * If the remove flag is already marked as true, it implies
                     * the deletion has been successfully done by another call.
                     * Thus, the prodRemoved should be set to false and nothing
                     * else should be done.
                     */
                    prodRemoved = false;
                }
                else {
                    /**
                     * If the remove flag is not marked as true, it implies
                     * the deletion is successfully done by this call. Thus,
                     * it deserves to set the prodRemoved to true.
                     */
                    metaData->remove = true;
                    prodRemoved = true;
                }
            }
            else {
                metaData->~RetxMetadata();
                indexMetaMap.erase(metaIter);
                prodRemoved = true;
            }
        }
        else {
            prodRemoved = false;
        }
    } // Product has metadata entry
    else {
        prodRemoved = false;
    }

    return prodRemoved;
}


/**
 * Fetch the requested RetxMetadata entry identified by a given prodindex. If
 * found nothing, return NULL pointer. Otherwise return the pointer to that
 * RetxMetadata struct. By default, the RetxMetadata pointer should be
 * initialized to NULL.
 *
 * @param[in] prodindex         specific product index
 * @return    A pointer to the original RetxMetadata in the map.
 */
RetxMetadata* senderMetadata::getMetadata(uint32_t prodindex)
{
    RetxMetadata* temp = NULL;
    std::map<uint32_t, RetxMetadata*>::iterator it;
    {
        std::unique_lock<std::mutex> lock(indexMetaMapLock);
        if ((it = indexMetaMap.find(prodindex)) != indexMetaMap.end()) {
            temp = it->second;
            /* sets up exclusive flag to acquire this RetxMetadata */
            it->second->inuse = true;
        }
    }
    return temp;
}


/**
 * Sends all unACKed receivers an EOP. This is to make sure the unACKed
 * receivers did not miss the whole last file.
 * Also another possibility is that the receiver timer is set improperly,
 * in which case the receiver missed the last few blocks of the last file
 * but is still waiting for the receiver timer to wake up and detect loss.
 * This improperly set receiver timer wakes up after the sender timer expires,
 * which causes no RETX_END to come back to the sender in time.
 * But either case, it looks the same from the sender's perspective. The
 * sender will retransmit an EOP via TCP to guarantee receivers know the
 * existence of a file, but cannot guarantee the successful delivery.
 * A receiver might drop offline before timer wakes up, and the unfinished
 * set is not updated. This could cause the sending EOP operation to fail
 * due to a non-existing connection. This method checks if a connection is
 * still valid before sending EOP.
 *
 * @param[in] prodindex         product index of the product
 * @param[in] header            the EOP message
 *
 * @throw std::runtime_error if TcpSend::send() fails.
 */
void senderMetadata::notifyUnACKedRcvrs(uint32_t prodindex, FmtpHeader* header,
                                        TcpSend* tcpsend)
{
    std::unique_lock<std::mutex> lock(indexMetaMapLock);
    auto                         iter = indexMetaMap.find(prodindex);

    if (iter != indexMetaMap.end()) {
    	/*
    	 * A list of relevant sockets is first generated in order to quickly
    	 * release the lock to avoid blocking other threads while writing to the
    	 * sockets.
    	 */
    	std::list<int> socks{};

		for (auto sd : iter->second->unfinReceivers)
			if (tcpsend->isMember(sd))
				socks.push_back(sd); // Unfinished receiver is still connected

    	lock.unlock(); // So `indexMetaMap` can be modified on other threads

		for (auto sd : socks) {
			auto retval = TcpSend::send(sd, header, NULL, 0);

			if (retval < 0)
				throw std::runtime_error(
						"senderMetadata::notifyUnACKedRcvrs() "
						"TcpSend::sendData() error");
		} // Relevant socket loop
    } // Product has metadata
}


/**
 * Releases the acquired RetxMetadata. If it is marked as in use, reset
 * the in use flag. If it is marked as remove, remove it correspondingly.
 *
 * @param[in] prodindex         product index of the requested product
 * @return    True if release operation is successful, otherwise false.
 */
bool senderMetadata::releaseMetadata(uint32_t prodindex)
{
    bool                        relstate;
    std::lock_guard<std::mutex> lock(indexMetaMapLock);
    auto                        iter = indexMetaMap.find(prodindex);

    if (iter != indexMetaMap.end()) {
    	auto metaData = iter->second;

        if (metaData->inuse) {
            metaData->inuse = false;
        }
        if (metaData->remove) {
            metaData->~RetxMetadata();
            indexMetaMap.erase(iter);
        }
        relstate = true;
    }
    else {
        relstate = false;
    }

    return relstate;
}


/**
 * Remove the RetxMetadata identified by a given product index. It returns
 * a boolean status value to indicate whether the remove is successful or not.
 * If successful, it's a true, otherwise it's a false.
 *
 * @param[in] prodindex         product index of the requested product
 * @return    True if removal is successful, otherwise false.
 */
bool senderMetadata::rmRetxMetadata(uint32_t prodindex)
{
    bool                         rmSuccess;
    std::unique_lock<std::mutex> lock(indexMetaMapLock);
    auto                         metaIter = indexMetaMap.find(prodindex);

    if (metaIter == indexMetaMap.end()) {
        rmSuccess = false;
    }
    else {
        if (metaIter->second->inuse) {
            if (metaIter->second->remove) {
                /**
                 * If the remove flag is already marked as true, it implies
                 * the deletion has been successfully done by another call.
                 * Thus, the rmSuccess should be set to false and nothing
                 * else should be done.
                 */
                rmSuccess = false;
            }
            else {
                /**
                 * If the remove flag is not marked as true, it implies
                 * the deletion is successfully done by this call. Thus,
                 * it deserves to set the rmSuccess to true.
                 */
                metaIter->second->remove = true;
                rmSuccess = true;
            }
        } // Product's metadata is in-use
        else {
            metaIter->second->~RetxMetadata();
            indexMetaMap.erase(metaIter);
            rmSuccess = true;
        }
    } // Product has metadata entry

    return rmSuccess;
}

/**
 * Populates a list with the indexes of products for which a receiver hasn't
 * acknowledged complete reception.
 *
 * @param[in]  sd       Descriptor of receiver's retransmission socket
 * @param[out] indexes  List to be populated
 */
void senderMetadata::getUnAckedProds(
		const int            sd,
		std::list<uint32_t>& indexes) const
{
    std::lock_guard<std::mutex> lock(indexMetaMapLock);

    for (auto entry : indexMetaMap) {
    	auto  metaData = entry.second;
    	auto& socks = metaData->unfinReceivers;

    	if (socks.find(sd) != socks.end())
    		indexes.push_back(metaData->prodindex);
    }
}

/**
 * Deletes a receiver from all product metadata and deletes product metadata
 * that have no receivers. Returns the products whose metadata were deleted.
 *
 * @param[in]  sd       Socket descriptor of receiver
 * @param[out] indexes  Index of products whose metadata were deleted
 */
void senderMetadata::deleteReceiver(int sd, std::list<uint32_t>& indexes)
{
    std::lock_guard<std::mutex> lock(indexMetaMapLock);

    for (auto iter = indexMetaMap.begin(); iter != indexMetaMap.end(); ++iter) {
        auto  metaData = iter->second;
        auto& socks = metaData->unfinReceivers;

        socks.erase(sd);

        if (socks.empty()) {
            indexes.push_back(metaData->prodindex);
            indexMetaMap.erase(iter);
        }
    }
}
