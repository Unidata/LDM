/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      ProdBlockMNG.h
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
 * @brief     Define the interfaces of ProdBlockMNG class.
 *
 * A per-product bitmap class, tracks all the data blocks of every product.
 */


#ifndef FMTP_RECEIVER_PRODBLOCKMNG_H_
#define FMTP_RECEIVER_PRODBLOCKMNG_H_


#include <stdint.h>
#include <mutex>
#include <vector>
#include <unordered_map>


/* a prodindex-to-vector<bool> mapping */
typedef std::unordered_map<uint32_t, std::vector<bool>*> BitMapSet;
/* a prodindex-to-bitmap-size mapping */
typedef std::unordered_map<uint32_t, uint32_t> BitMapSizeSet;
/* a prodindex-to-received-blocks-count mapping */
typedef std::unordered_map<uint32_t, uint32_t> BMRecvBlockSet;


class ProdBlockMNG
{
public:
    ProdBlockMNG();
    ~ProdBlockMNG();
    bool addProd(const uint32_t prodindex, const uint32_t bitmapsize);
    bool delIfComplete(const uint32_t prodindex);
    uint32_t getMapSize(const uint32_t prodindex);
    bool getLastBlock(const uint32_t prodindex);
    bool isComplete(const uint32_t prodindex);
    bool rmProd(const uint32_t prodindex);
    void set(const uint32_t prodindex, const uint32_t blockindex);

private:
    /* count the received data block number */
    uint32_t count(const uint32_t prodindex);

    BitMapSet          bitmapSet;
    BitMapSizeSet      bitmapSizeSet;
    BMRecvBlockSet     bmRecvBlockSet;
    std::vector<bool>* map;
    std::mutex         mutex;
};


#endif /* FMTP_RECEIVER_PRODBLOCKMNG_H_ */
