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
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or（at your option）
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details at http://www.gnu.org/copyleft/gpl.html
 *
 * @brief     Rate shaper to limit rate in application layer.
 */


#include "RateShaper.h"

#include <stdexcept>
#include <thread>


/**
 * Constructor of RateShaper.
 */
RateShaper::RateShaper()
{
    period    = 0;
    sleeptime = 0;
    rate      = 0;
    txsize    = 0;
}


/**
 * Destructor of RateShaper.
 */
RateShaper::~RateShaper()
{
    // Auto-generated destructor stub
}


/**
 * Sets the sending rate to a given value.
 *
 * @param[in] rate_bps Sending rate in bits per second.
 *
 * @throw std::runtime_error  If reads negative input rate.
 * @throw std::runtime_error  If input rate less than 1Kbps.
 */
void RateShaper::SetRate(uint64_t rate_bps)
{
    if (rate_bps < 1000) {
        throw std::runtime_error(
                "RateShaper::SetRate() rate possibly in wrong metric.");
    }
    else {
        rate = rate_bps;
    }
}


/**
 * Calculates the time period based the pre-set rate, and starts to time.
 *
 * @param[in] size     Size of the packet to be sent.
 *
 * @throw std::runtime_error  If input size is not positive.
 */
void RateShaper::CalcPeriod(uint64_t size)
{
    if (size <= 0) {
        throw std::runtime_error(
                "RateShaper::CalcPeriod() input size is not positive.");
    }
    else {
        txsize = size;
    }
    /* compute time period (t_s) in seconds */
    period = (static_cast<double>(size) * 8) / static_cast<double>(rate);
    start_time  = HRC::now();
}


/**
 * Stops the clock, calculates the transmission time, and sleeps for the
 * rest of the time period.
 * Note that t_nic = s / r_nic and t_s = s / r_s, where r_nic is the NIC rate
 * and r_s is the specified rate. If r_nic < r_s, there is t_nic < t_s. Then
 * to limit the rate, the function just needs to sleep for (t_s - t_nic).
 * Else, for r_nic < r_s, the application is asking for more than NIC can
 * provide. In this case, nothing should be done, meaning letting NIC send at
 * full speed is enough.
 */
void RateShaper::Sleep()
{
    end_time = HRC::now();
    std::chrono::duration<double> txtime =
        std::chrono::duration_cast<std::chrono::duration<double>>
        (end_time - start_time);
    std::chrono::duration<double> p(period);
    /* p is t_s, txtime is t_nic */
    if (p > txtime) {
        std::this_thread::sleep_for(p - txtime);
    }
}
