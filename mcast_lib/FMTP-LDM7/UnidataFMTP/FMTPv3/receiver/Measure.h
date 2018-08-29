/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      Measure.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      May 21, 2015
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
 * @brief     Define the interfaces of Measure class.
 *
 * A statistical information class for measurement.
 */


#ifndef FMTP_RECEIVER_MEASURE_H_
#define FMTP_RECEIVER_MEASURE_H_


#include <stdint.h>
#include <mutex>
#include <unordered_map>


typedef std::chrono::high_resolution_clock HRclock;

struct MeasureInfo {
    bool EOPmissed;
    HRclock::time_point start_t;
    HRclock::time_point end_t;
    HRclock::time_point mcastend_t;
    HRclock::time_point retxend_t;
    uint32_t recvbytes;

    MeasureInfo(): EOPmissed(false), start_t(HRclock::now()), end_t(start_t),
                   mcastend_t(start_t), retxend_t(start_t), recvbytes(0) {}
    virtual ~MeasureInfo() {}
};

/* a prodindex-to-Measure-struct mapping */
typedef std::unordered_map<uint32_t, MeasureInfo*> MeasureMap;

class Measure
{
public:
    Measure();
    ~Measure();
    uint32_t getsize(const uint32_t prodindex);
    std::string gettime(const uint32_t prodindex);
    bool getEOPmiss(const uint32_t prodindex);
    bool insert(const uint32_t prodindex, const uint32_t prodsize);
    bool setEOPmiss(const uint32_t prodindex);
    bool setMcastClock(const uint32_t prodindex);
    bool setRetxClock(const uint32_t prodindex);
    bool remove(const uint32_t prodindex);

private:
    MeasureMap         measuremap;
    std::mutex         mutex;
};


#endif /* FMTP_RECEIVER_MEASURE_H_ */
