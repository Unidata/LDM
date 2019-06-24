/**
 * Copyright 2019 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: OffsetMap.cpp
 * @author: Steven R. Emmerson
 *
 * This file implements a mapping from FMTP product-indexes to file-offsets.
 */

#include "log.h"
#include "OffsetMap.h"

#include <exception>
#include <sys/time.h>

void OffsetMap::put(
        const McastProdIndex prodIndex,
        const off_t          offset)
{
    Guard guard(mutex);
    map[prodIndex].offset = offset;
    ::gettimeofday(&map[prodIndex].added, nullptr);
}

off_t OffsetMap::get(
        const McastProdIndex prodIndex)
{
    Guard          guard(mutex);
    Element&       elt = map.at(prodIndex);
    auto           offset = elt.offset;

    struct timeval now;
    ::gettimeofday(&now, nullptr);
    unsigned long  sec = now.tv_sec - elt.added.tv_sec;
    long           usec = now.tv_usec - elt.added.tv_usec;
    if (usec < 0) {
        sec -= 1;
        usec += 1000000;
    }
    log_notice("{count: %lu, offset: %ld, duration: %lu.%05ld s}", map.size(),
            offset, sec, usec);

    map.erase(prodIndex);
    return offset;
}

OffMap* om_new()
{
    try {
        return new OffsetMap();
    }
    catch (const std::exception& e) {
        log_add("%s", e.what());
        return NULL;
    }
}

void om_free(
        OffMap* const offMap)
{
    ((OffsetMap*)offMap)->~OffsetMap();
}

int om_put(
        OffMap* const        offMap,
        const McastProdIndex prodIndex,
        const off_t          offset)
{
    try {
        ((OffsetMap*)offMap)->put(prodIndex, offset);
        return 0;
    }
    catch (const std::exception& e) {
        log_add("%s", e.what());
        return LDM7_SYSTEM;
    }
}

int om_get(
        OffMap* const        offMap,
        const McastProdIndex prodIndex,
        off_t* const         offset)
{
    try {
        *offset = ((OffsetMap*)offMap)->get(prodIndex);
        return 0;
    }
    catch (const std::exception& e) {
        log_add("%s", e.what());
        return LDM7_INVAL;
    }
}
