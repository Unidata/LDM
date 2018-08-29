/**
 * Copyright (C) 2016 University of Virginia. All rights reserved.
 *
 * @file      ProdSegMNG.cpp
 * @author    Ryan Aubrey <rma7qb@virginia.edu>
 *            Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      May 27, 2016
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
 * @brief     Implement the interfaces of ProdSegMNG class.
 *
 * A per-product segment manager class, tracks all the data segments of
 * every product.
 */


#include "ProdSegMNG.h"


/**
 * Constructor of the ProdSegMNG class.
 *
 * @param[in] none
 */
ProdSegMNG::ProdSegMNG() : mutex()
{
}


/**
 * Destructor of the ProdSegMNG class.
 *
 * @param[in] none
 */
ProdSegMNG::~ProdSegMNG()
{
    std::unique_lock<std::mutex> lock(mutex);
    SegMapSet::iterator it;
    for (it = segmapSet.begin(); it != segmapSet.end(); ++it)
        delete it->second;
    segmapSet.clear();
}


/**
 * Puts a new product under tracking. If the product is already in map,
 * return false indicating failure to add product.
 * Otherwise return true indicating successful addition.
 *
 * @param[in] prodindex        Product index of the product to track.
 * @param[in] prodsize         size of the product.
 * @return                     true for successful addition.
 *                             false for unsuccessful addition.
 */
bool ProdSegMNG::addProd(const uint32_t prodindex, const uint32_t prodsize)
{
    std::unique_lock<std::mutex> lock(mutex);
    /* check if the product is already under tracking */
    if (!segmapSet.count(prodindex)) {
        /* put current product under tracking */
        SegMap* segmap = new SegMap();
        segmap->completed = false;
        segmap->prodsize  = prodsize;
        segmap->seqlenMap[0] = prodsize;
        segmapSet[prodindex] = segmap;
        return true;
    }
    else {
        /* product already under tracking, addProd() failed */
        return false;
    }
}


/**
 * If all segments are received, delete all related resources and return true.
 * Otherwise, do nothing and return false.
 *
 * @param[in] prodindex        Product index of the product to query.
 * @return                     true for complete and successfully deleted.
 *                             false for incomplete or product not found.
 */
bool ProdSegMNG::delIfComplete(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (segmapSet.count(prodindex)) {
        SegMap* segmap = segmapSet[prodindex];
        if (segmap->completed) {
            delete segmap;
            segmapSet.erase(prodindex);
            return true;
        }
        else {
            /* If not complete, do nothing but report failure */
            return false;
        }
    }
    else {
        return false;
    }
}


/**
 * Gets the status of the last segment of the given product.
 *
 * @param[in] prodindex        Product index of the product to get status from.
 * @return                     Arrival status of the last block.
 */
bool ProdSegMNG::getLastSegment(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (segmapSet.count(prodindex)) {
        SegMap* segmap = segmapSet[prodindex];
        if (segmap->completed) {
            return true;
        }
        else {
            SeqLenMap::reverse_iterator it;
            it = segmap->seqlenMap.rbegin();
            /*
             * In terms of the last entry in the map,
             * if seqnum + paylen < prodsize, the last block is received.
             * Otherwise, it is unreceived.
             */
            if (it->first + it->second < segmap->prodsize) {
                return true;
            }
            else {
                return false;
            }
        }
    }
    else {
        return false;
    }
}


/**
 * Checks if the given product has been completely received.
 *
 * @param[in] prodindex        Product index of the product to query.
 * @return                     true for complete and false for incomplete or
 *                             product not found.
 */
bool ProdSegMNG::isComplete(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (segmapSet.count(prodindex)) {
        SegMap* segmap = segmapSet[prodindex];
        if (segmap->completed) {
            return true;
        }
        else {
            return false;
        }
    }
    else {
        return false;
    }
}


/**
 * Removes a product from map and frees its resources.
 *
 * @param[in] prodindex        Product index of the product to remove.
 * @return                     true for successful deletion and false
 *                             for product not found.
 */
bool ProdSegMNG::rmProd(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (segmapSet.count(prodindex)) {
        delete segmapSet[prodindex];
        segmapSet.erase(prodindex);
        return true;
    }
    else {
        return false;
    }
}


/**
 * Sets the received status of the given segment of a product.
 *
 * @param[in] prodindex        Product index of the product to set.
 * @param[in] blockindex       Block index of the received block.
 *
 * @return                     -1 if product not found or segment misaligned
 *                             0 if duplicate, no operation done
 *                             1 if set segment successful
 */
int ProdSegMNG::set(const uint32_t prodindex, const uint32_t seqnum,
                    const uint16_t payloadlen)
{
    int state = 0;
    std::unique_lock<std::mutex> lock(mutex);
    if (segmapSet.count(prodindex)) {
        SegMap* segmap = segmapSet[prodindex];
        if (!segmap->seqlenMap.empty()) {
            SeqLenMap::iterator it = segmap->seqlenMap.find(seqnum);
            if (it != segmap->seqlenMap.end()) {
                /* there exists an entry starting with seqnum */
                if (it->second > payloadlen) {
                    /* entry covers segment, cut head */
                    uint32_t newlen = it->second - payloadlen;
                    segmap->seqlenMap.erase(seqnum);
                    segmap->seqlenMap[seqnum + payloadlen] = newlen;
                    state = 1;
                }
                else if (it->second == payloadlen) {
                    /* segment exactly matches the entry, erase entry */
                    segmap->seqlenMap.erase(seqnum);
                    state = 1;
                }
                else {
                    /* entry represents a segment smaller than min size, err */
                    state = -1;
                }
            }
            else {
                /* there does not exist an entry starting with seqnum */
                it = segmap->seqlenMap.lower_bound(seqnum);
                /* as far as map is not empty, it == end is fine */
                if (it != segmap->seqlenMap.begin()) {
                    /* point to the entry before seqnum, should cover segment */
                    it--;
                    if (it->first + it->second > seqnum + payloadlen) {
                        /* entry covers segment, break into two */
                        uint32_t firstlen  = seqnum - it->first;
                        uint32_t secondlen = it->second - payloadlen - firstlen;
                        segmap->seqlenMap[it->first] = firstlen;
                        segmap->seqlenMap[seqnum + payloadlen] = secondlen;
                        state = 1;
                    }
                    else if (it->first + it->second == seqnum + payloadlen) {
                        /* entry covers segment, cut the tail */
                        segmap->seqlenMap[it->first] = it->second - payloadlen;
                        state = 1;
                    }
                    else {
                        if (it->first + it->second <= seqnum) {
                            /* segment is duplicate */
                            state = 0;
                        }
                        else {
                            /* entry not fully cover segment, err */
                            state = -1;
                        }
                    }
                }
                else {
                    /* no entry can cover segment, possibly a duplicate seg */
                    state = 0;
                }
            }
        }
        if (segmap->seqlenMap.empty()) {
            segmap->completed = true;
        }
    }
    else {
        state = -1;
    }
    return state;
}
