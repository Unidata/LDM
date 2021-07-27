/**
 * Copyright (C) 2021 University of Virginia. All rights reserved.
 *
 * @file      fmtpBase.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @author    Steven R. Emmerson <emmerson@ucar.edu>
 * @version   1.0
 * @date      Oct 7, 2014
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
 * @brief     Define the entity of FMTPv3 basics.
 *
 * Definition of control message definition, header struct and message length.
 */


#include "FmtpBase.h"
#include "mac.h"

#ifdef LDM_LOGGING
#   include "log.h"
#endif

#include <cstring>

unsigned FmtpBase::getCanonPduSize()
{
    // Exclude IP and TCP headers
    unsigned          canonPduSize = MAX_ETHERNET_PAYLOAD - 20 - 20;
    static const char ENV_NAME[] = "FMTP_CANON_PDU_SIZE";
    const char*       envStr = ::getenv(ENV_NAME);

    if (envStr) {
        char* end;
        errno = 0;
        auto size = ::strtol(envStr, &end, 0);

        if (errno || *end || size <= 0) {
            throw std::runtime_error(std::string("Invalid environment "
                    "variable ") + ENV_NAME + ": " + envStr);
        }
        if (size > MAX_FMTP_PACKET) {
            throw std::runtime_error(std::string("Environment variable ") +
                    ENV_NAME + " can't be greater than " +
                    std::to_string(MAX_FMTP_PACKET) + ": " + envStr);
        }

        canonPduSize = size;
    }

#   ifdef LDM_LOGGING
        log_notice("Canonical PDU size is %u bytes", canonPduSize);
#   endif

    return canonPduSize;
}

FmtpBase::FmtpBase()
    : CANON_PDU_SIZE(getCanonPduSize())
    , MAC_SIZE{Mac::getSize()}
    , MAX_PAYLOAD{CANON_PDU_SIZE - FMTP_HEADER_LEN - MAC_SIZE}
    , MAX_BOP_METADATA{MAX_PAYLOAD - BOPMsg::HEADER_SIZE}
		//- static_cast<int>(sizeof(StartTime))
        //- static_cast<int>(sizeof(uint32_t))   // BOPMsg.prodsize
		//- static_cast<int>(sizeof(uint16_t))}  // BOPMsg.metasize
{}

FmtpBase::~FmtpBase()
{}
