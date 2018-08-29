/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      ProdBlockMNG.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Jan 22, 2015
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
 * @brief     Implement the interfaces of ProdBlockMNG class.
 *
 * A per-product bitmap class, tracks all the data blocks of every product.
 */


#include "ProdBlockMNG.h"


/**
 * Constructor of the ProdBlockMNG class.
 *
 * @param[in] none
 */
ProdBlockMNG::ProdBlockMNG() : map(NULL), mutex()
{
}


/**
 * Destructor of the ProdBlockMNG class.
 *
 * @param[in] none
 */
ProdBlockMNG::~ProdBlockMNG()
{
    std::unique_lock<std::mutex> lock(mutex);
    /* only bitmapSet needs to be de-allocated */
    BitMapSet::iterator it;
    for (it = bitmapSet.begin(); it != bitmapSet.end(); ++it)
        delete it->second;
    bitmapSet.clear();
    bitmapSizeSet.clear();
    bmRecvBlockSet.clear();
}


/**
 * Puts a new product under tracking. Adds it into the bitmapSet and
 * bitmapSizeSet. If the product is already in bitmapSet, return false
 * indicating failure to add product. Otherwise return true indicating
 * successful addition.
 *
 * @param[in] prodindex        Product index of the product to track.
 * @param[in] bitmapsize       Block number (bitmap size) of the product.
 * @return                     true for successful addition.
 *                             false for unsuccessful addition.
 */
bool ProdBlockMNG::addProd(const uint32_t prodindex, const uint32_t bitmapsize)
{
    std::unique_lock<std::mutex> lock(mutex);
    /* check if the product is already under tracking */
    if (!bitmapSet.count(prodindex)) {
        /* put current product under tracking */
        bitmapSet[prodindex] = new std::vector<bool>(bitmapsize, false);
        bitmapSizeSet[prodindex] = bitmapsize;
        bmRecvBlockSet[prodindex] = 0;
        return true;
    }
    else {
        /* product already under tracking, addProd() failed */
        return false;
    }
}


/**
 * Gets the currently received block number of the given product and compare
 * it with the total block number (bitmap size) of the product. If all blocks
 * are received, delete all related resources and return true. Otherwise, do
 * nothing and return false.
 *
 * @param[in] prodindex        Product index of the product to query.
 * @return                     true for complete and successfully deleted.
 *                             false for incomplete or product not found.
 */
bool ProdBlockMNG::delIfComplete(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (bitmapSet.count(prodindex) && bitmapSizeSet.count(prodindex)) {
        /*
         * if complete, free the bitmap on heap and clear associated maps
         * And count() will return 0 if product does not exist
         */
        if (count(prodindex) == bitmapSizeSet[prodindex]) {
            delete bitmapSet[prodindex];
            bitmapSet.erase(prodindex);
            bitmapSizeSet.erase(prodindex);
            bmRecvBlockSet.erase(prodindex);
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
 * Gets the block number (bitmap size) of the given product.
 *
 * @param[in] prodindex        Product index of the product to get size from.
 * @return                     Number of blocks the product has. If the product
 *                             no longer exists, return 0.
 */
uint32_t ProdBlockMNG::getMapSize(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (bitmapSizeSet.count(prodindex)) {
        return bitmapSizeSet[prodindex];
    }
    else {
        return 0;
    }
}


/**
 * Gets the status of the last block of the given product.
 *
 * @param[in] prodindex        Product index of the product to get status from.
 * @return                     Arrival status of the last block.
 */
bool ProdBlockMNG::getLastBlock(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (bitmapSet.count(prodindex) && bitmapSizeSet.count(prodindex)) {
        uint32_t blockidx = bitmapSizeSet[prodindex] - 1;
        map = bitmapSet[prodindex];
        return map->at(blockidx);
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
bool ProdBlockMNG::isComplete(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (bitmapSet.count(prodindex) && bitmapSizeSet.count(prodindex)) {
        if (count(prodindex) == bitmapSizeSet[prodindex]) {
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
 * Puts a new product under tracking. Adds it into the bitmapSet and
 * bitmapSizeSet. If the product is already in bitmapSet, return false
 * indicating failure to add product. Otherwise return true indicating
 * successful addition.
 *
 * @param[in] prodindex        Product index of the product to track.
 * @return                     true for successful deletion and false
 *                             for incomplete deletion.
 */
bool ProdBlockMNG::rmProd(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (bitmapSet.count(prodindex) && bitmapSizeSet.count(prodindex) &&
        bmRecvBlockSet.count(prodindex)) {
        delete bitmapSet[prodindex];
        bitmapSet.erase(prodindex);
        bitmapSizeSet.erase(prodindex);
        bmRecvBlockSet.erase(prodindex);
        return true;
    }
    else {
        return false;
    }
}


/**
 * Sets the status of the given block of the given product in bitmap to true.
 * And increases the received block counter for the product.
 *
 * @param[in] prodindex        Product index of the product to set.
 * @param[in] blockindex       Block index of the received block.
 */
void ProdBlockMNG::set(const uint32_t prodindex, const uint32_t blockindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (bitmapSet.count(prodindex) && bmRecvBlockSet.count(prodindex)) {
        map = bitmapSet[prodindex];
        /* if duplicated block is received, just ignore it */
        if (!map->at(blockindex)) {
            map->at(blockindex) = true;
            bmRecvBlockSet[prodindex]++;
        }
    }
}


/**
 * Gets the number of received blocks for the given product.
 *
 * @param[in] prodindex        Product index of the product to count.
 * @return                     Number of blocks already received, if product
 *                             not found, return 0.
 */
uint32_t ProdBlockMNG::count(const uint32_t prodindex)
{
    if (bmRecvBlockSet.count(prodindex)) {
        return bmRecvBlockSet[prodindex];
    }
    else {
        return 0;
    }
}
