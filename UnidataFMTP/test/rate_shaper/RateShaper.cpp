/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      RateShaper.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      June 19, 2015
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
 * @brief     Rate shaper to limit rate in application layer.
 */


#include "RateShaper.h"

#include <math.h>
#include <thread>


RateShaper::RateShaper()
{
    period    = 0;
    sleeptime = 0;
    rate      = 0;
    txsize    = 0;
}


RateShaper::~RateShaper()
{
    // TODO Auto-generated destructor stub
}


void RateShaper::SetRate(double rate_bps)
{
    rate = ceil(rate_bps);
}


void RateShaper::CalPeriod(unsigned int size)
{
    txsize = size;
    /* compute time period in seconds */
    period = (size * 8) / rate;
    start_time  = HRC::now();
}


void RateShaper::Sleep()
{
    end_time = HRC::now();
    std::chrono::duration<double> txtime = end_time - start_time;
    std::chrono::duration<double> p(period);
    /* sleep for the computed time */
    std::this_thread::sleep_for(p - txtime);
}
