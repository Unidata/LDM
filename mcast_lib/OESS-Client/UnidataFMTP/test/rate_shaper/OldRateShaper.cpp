/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      RateShaper.cpp
 * @author    Jie Li
 *            Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      June 13, 2015
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
 * @brief     Token-Bucket-like Rate shaper to limit rate in application layer.
 */


#include "RateShaper.h"

#include <math.h>


RateShaper::RateShaper()
{
    bucket_size   = 0;
    overflow      = 0;
    avail_tokens  = 0;
    token_gentime = 0;
    tim.tv_sec    = 0;
    tim.tv_nsec   = 0;
}


RateShaper::~RateShaper()
{
    // TODO Auto-generated destructor stub
}


void RateShaper::SetRate(double rate_bps)
{
    token_gentime = 8 / rate_bps;
    /* round up to an integer */
    bucket_size = static_cast<unsigned int>(ceil(rate_bps / 8));
    avail_tokens = bucket_size;
    /* allow 0ms burst tolerance */
    overflow = static_cast<unsigned int>(rate_bps * 0.0);
    bucket_size += overflow;

    /* record the latest time point */
    last_check_time = HRC::now();
}


int RateShaper::RetrieveTokens(unsigned int num_tokens)
{
    /* update the token number first */
    double timespan = getElapsedTime(last_check_time);
    last_check_time = HRC::now();
    addTokens(timespan);

    /**
     * If the number of tokens requested exceeds the bucket size,
     * there is no way to give back sufficient tokens. Thus, just
     * return all the available tokens.
     */
    if (num_tokens > bucket_size) {
        unsigned int tmp = avail_tokens;
        avail_tokens = 0;
        return tmp;
    }

    while (num_tokens > avail_tokens) {
        /* compute how much time it needs to generate enough tokens */
        double required_time = (num_tokens - avail_tokens) / token_gentime;
        double sec_part = floor(required_time);
        tim.tv_sec = static_cast<long int>(sec_part);
        tim.tv_nsec = static_cast<long int>((required_time - sec_part) * 1e9);
        nanosleep(&tim , NULL);

        double timespan = getElapsedTime(last_check_time);
        last_check_time = HRC::now();
        addTokens(timespan);
    }

    avail_tokens -= num_tokens;
    return num_tokens;
}


void RateShaper::addTokens(double elapsed_time)
{
    unsigned int generated_tokens = static_cast<unsigned int>(floor(
        elapsed_time * token_gentime));
    avail_tokens += generated_tokens;
    /* bucket will overflow if too much tokens are added */
    if (avail_tokens > bucket_size) {
        avail_tokens = bucket_size;
    }
}


double RateShaper::getElapsedTime(HRC::time_point last_check_time)
{
    HRC::time_point current_time = HRC::now();
    std::chrono::duration<double> timespan =
        std::chrono::duration_cast<std::chrono::duration<double>>
            (current_time - last_check_time);
    return timespan.count();
}
