/**
 * Copyright (C) 2016 University of Virginia. All rights reserved.
 *
 * @file      ProdSegMNG.h
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
 * @brief     Define the interfaces of ProdSegMNG class.
 *
 * A per-product segment manager class, tracks all the data segments of
 * every product.
 */


#ifndef FMTP_RECEIVER_PRODSEGMNG_H_
#define FMTP_RECEIVER_PRODSEGMNG_H_


#include <stdint.h>
#include <map>
#include <mutex>
#include <unordered_map>


/* maps beginning seqnum of a segment to segment length */
typedef std::map<uint32_t, uint32_t> SeqLenMap;
struct SegMap {
    SeqLenMap seqlenMap;
    bool      completed;
    uint32_t  prodsize;
};
/* maps prodindex to a SegMap pointer */
typedef std::unordered_map<uint32_t, SegMap*> SegMapSet;


class ProdSegMNG
{
public:
    ProdSegMNG();
    ~ProdSegMNG();
    bool addProd(const uint32_t prodindex, const uint32_t prodsize);
    bool delIfComplete(const uint32_t prodindex);
    bool getLastSegment(const uint32_t prodindex);
    bool isComplete(const uint32_t prodindex);
    bool rmProd(const uint32_t prodindex);
    int  set(const uint32_t prodindex, const uint32_t seqnum,
             const uint16_t payloadlen);

private:
    SegMapSet    segmapSet;
    std::mutex   mutex;
};


#endif /* FMTP_RECEIVER_PRODSEGMNG_H_ */
