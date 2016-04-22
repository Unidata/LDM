/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: decode.h
 * @author: Steven R. Emmerson
 *
 * This file declares the API for NOAAPort decoding utilities.
 */

#ifndef NOAAPORT_DECODE_H_
#define NOAAPORT_DECODE_H_

#include <stdint.h>

#ifdef __cplusplus
    extern "C" {
#endif

static inline void encode_uint16(
        uint8_t* const buf,
        const uint16_t value)
{
    buf[0] = (value >> 8) & 0xff;
    buf[1] = value & 0xff;
}

static inline void encode_uint32(
        uint8_t* const buf,
        uint32_t       value)
{
    buf[0] = (value >> 24) & 0xff;
    buf[1] = (value >> 16) & 0xff;
    buf[2] = (value >> 8) & 0xff;
    buf[3] = value & 0xff;
}

static inline uint16_t decode_uint16(
        const uint8_t* const buf)
{
    return (buf[0] << 8) + buf[1];
}

static inline uint32_t decode_uint32(
        const uint8_t* const buf)
{
    return (((((buf[0] << 8) + buf[1]) << 8) + buf[2]) << 8) + buf[3];
}


#ifdef __cplusplus
    }
#endif

#endif /* NOAAPORT_DECODE_H_ */
