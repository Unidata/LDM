/**
 * This file declares an abstract base class for serializing objects.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 * 
 *        File: Serializer.cpp
 *  Created on: Dec 21, 2018
 *      Author: Steven R. Emmerson
 */

#include <arpa/inet.h>
#include <cstdint>
#include <limits.h>

#ifndef FMTPV3_SENDER_SERIALIZER
#define FMTPV3_SENDER_SERIALIZER

class Serializer
{
protected:
    virtual void add(const uint16_t value) =0;
    virtual void add(const uint32_t value) =0;

public:
    virtual ~Serializer()
    {}

    inline void encode(uint16_t value)
    {
        add(htons(value));
    }

#if 0
    inline void encode(int16_t value)
    {
        encode(static_cast<uint16_t>(value));
    }
#endif

    void encode(uint32_t value)
    {
        add(htonl(value));
    }

#if 0
    inline void encode(int32_t value)
    {
        encode(static_cast<uint32_t>(value));
    }
#endif

    inline void encode(const uint64_t value)
    {
        add(htonl(value >> 32));
        add(htonl(static_cast<uint32_t>(value & 0xFFFFFFFF)));
    }

#if 0
    inline void encode(const int64_t value)
    {
        encode(static_cast<uint64_t>(value));
    }
#endif

    /**
     * Encodes an array of bytes. The array must exist until `flush()` is
     * called.
     *
     * @param[in] bytes   Array of bytes to be encoded
     * @param[in] nbytes  Number of bytes in the array
     */
    virtual void encode(const void* bytes, unsigned nbytes) =0;

    virtual void flush() =0;
};

#endif
