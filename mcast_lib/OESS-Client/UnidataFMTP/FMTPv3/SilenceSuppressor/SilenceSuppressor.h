/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      SilenceSuppressor.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      August 1, 2015
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
 * @brief     Silence suppressor header. Suppresses silence when replaying
 *            metadata.
 */

#ifndef FMTP_FMTPV3_SILENCESUPPRESSOR_H_
#define FMTP_FMTPV3_SILENCESUPPRESSOR_H_


#include <mutex>
#include <set>


class SilenceSuppressor {
public:
    SilenceSuppressor(int prodnum);
    ~SilenceSuppressor();
    void     clearrange(uint32_t end);
    uint32_t query();
    bool     remove(uint32_t prodindex);

private:
    std::set<uint32_t>* prodset;
    std::mutex mtx;
};


#endif /* FMTP_FMTPV3_SILENCESUPPRESSOR_H_ */
