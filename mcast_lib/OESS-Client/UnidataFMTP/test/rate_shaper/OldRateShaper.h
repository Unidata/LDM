/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      RateShaper.h
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
 * @brief     Token-Bucket-like Rate shaper header file.
 */

#ifndef FMTP_FMTPV3_RATESHAPER_H_
#define FMTP_FMTPV3_RATESHAPER_H_


#include <time.h>
#include <chrono>

typedef std::chrono::high_resolution_clock HRC;


class RateShaper {
public:
    RateShaper();
    ~RateShaper();
    /* sets the expected rate in bits/sec */
    void SetRate(double rate_bps);
    /* retrieves specified number of tokens, otherwise blocks. */
    int RetrieveTokens(unsigned int num_tokens);

private:
    /* add tokens to the bucket */
    void addTokens(double elapsed_time);
    /* get elapsed time in seconds */
    double getElapsedTime(HRC::time_point last_check_time);

    /* amount of tokens a bucket can hold */
    unsigned int bucket_size;
    /* tokens allowed to overflow */
    unsigned int overflow;
    /* number of tokens currently in bucket */
    unsigned int avail_tokens;
    /* the time it takes to generate a token (in seconds) */
    double token_gentime;

    struct timespec tim;
    HRC::time_point last_check_time;
};

#endif /* FMTP_FMTPV3_RATESHAPER_H_ */
