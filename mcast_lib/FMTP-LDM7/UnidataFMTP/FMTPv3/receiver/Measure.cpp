/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      Measure.cpp
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
 * @brief     Implement the interfaces of Measure class.
 *
 * Stores the measurement-related information in a struct map.
 */


#include "Measure.h"

#include <string>


/**
 * Constructor of the Measure class.
 */
Measure::Measure() : mutex()
{
}


/**
 * Destructor of the Measure class.
 */
Measure::~Measure()
{
    std::unique_lock<std::mutex> lock(mutex);
    MeasureMap::iterator it;
    for (it = measuremap.begin(); it != measuremap.end(); ++it)
        delete it->second;
    measuremap.clear();
}


/**
 * Get the product size.
 *
 * @param[in] prodindex        Product index of the product
 * @return                     product size in bytes or 0 if not existing.
 */
uint32_t Measure::getsize(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (measuremap.find(prodindex) != measuremap.end()) {
        return measuremap[prodindex]->recvbytes;
    }
    else {
        return 0;
    }
}


/**
 * Get elapsed time in seconds.
 *
 * @param[in] prodindex        Product index of the product
 * @return                     Elapsed time in seconds
 */
std::string Measure::gettime(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (measuremap.find(prodindex) != measuremap.end()) {
        std::chrono::duration<double> timespan =
            std::chrono::duration_cast<std::chrono::duration<double>>
                (measuremap[prodindex]->end_t - measuremap[prodindex]->start_t);
        return std::to_string(timespan.count());
    }
    else {
        return std::to_string(0);
    }
}


/**
 * Get the EOP retransmitted status
 *
 * @param[in] prodindex        Product index of the product
 * @return                     EOP status (true for retransmitted)
 */
bool Measure::getEOPmiss(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (measuremap.find(prodindex) != measuremap.end()) {
        return measuremap[prodindex]->EOPmissed;
    }
    else {
        return false;
    }
}


/**
 * Insert a new arrived product into the measurement map. And record
 * the start time.
 *
 * @param[in] prodindex        Product index of the product to track.
 * @param[in] prodsize         Product size in bytes.
 * @return                     true for successful insertion.
 *                             false for unsuccessful insertion.
 */
bool Measure::insert(const uint32_t prodindex, const uint32_t prodsize)
{
    std::unique_lock<std::mutex> lock(mutex);
    /* check if the product is already under tracking */
    if (measuremap.find(prodindex) == measuremap.end()) {
        /* put current product under tracking */
        measuremap[prodindex] = new MeasureInfo();
        measuremap[prodindex]->recvbytes = prodsize;
        measuremap[prodindex]->start_t = HRclock::now();
        return true;
    }
    else {
        /* product already under tracking, insert() failed */
        return false;
    }
}


/**
 * Set the EOP retransmitted status
 *
 * @param[in] prodindex        Product index of the product
 * @return                     true for setting successfully
 *                             false for unsuccessfully.
 */
bool Measure::setEOPmiss(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (measuremap.find(prodindex) != measuremap.end()) {
        measuremap[prodindex]->EOPmissed = true;
        return true;
    }
    else {
        return false;
    }
}


/**
 * Set the multicast end clock.
 *
 * @param[in] prodindex        Product index of the product
 * @return                     true for setting successfully
 *                             false for unsuccessfully.
 */
bool Measure::setMcastClock(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (measuremap.find(prodindex) != measuremap.end()) {
        measuremap[prodindex]->mcastend_t = HRclock::now();
        measuremap[prodindex]->end_t = measuremap[prodindex]->mcastend_t >
            measuremap[prodindex]->retxend_t ? measuremap[prodindex]->mcastend_t :
            measuremap[prodindex]->retxend_t;
        return true;
    }
    else {
        return false;
    }
}


/**
 * Set the retransmission end clock.
 *
 * @param[in] prodindex        Product index of the product
 * @return                     true for setting successfully
 *                             false for unsuccessfully.
 */
bool Measure::setRetxClock(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (measuremap.find(prodindex) != measuremap.end()) {
        measuremap[prodindex]->retxend_t = HRclock::now();
        measuremap[prodindex]->end_t = measuremap[prodindex]->mcastend_t >
            measuremap[prodindex]->retxend_t ? measuremap[prodindex]->mcastend_t :
            measuremap[prodindex]->retxend_t;
        return true;
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
bool Measure::remove(const uint32_t prodindex)
{
    std::unique_lock<std::mutex> lock(mutex);
    if (measuremap.find(prodindex) != measuremap.end()) {
        delete measuremap[prodindex];
        measuremap.erase(prodindex);
        return true;
    }
    else {
        return false;
    }
}
