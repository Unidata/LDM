/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: OffsetMap.cpp
 * @author: Steven R. Emmerson
 *
 * This file implements a mapping from VCMTP product-indexes to file-offsets.
 */

#include "log.h"
#include "OffsetMap.h"

#include <exception>

void OffsetMap::put(
        const McastProdIndex prodIndex,
        const off_t          offset)
{
    std::unique_lock<std::mutex> lock(mutex);
    map[prodIndex] = offset;
}

off_t OffsetMap::get(
        const McastProdIndex prodIndex)
{
    std::unique_lock<std::mutex> lock(mutex);
    return map.at(prodIndex);
}

OffMap* om_new()
{
    try {
        return new OffsetMap();
    }
    catch (const std::exception& e) {
        LOG_START1("%s", e.what());
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
        LOG_START1("%s", e.what());
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
        LOG_START1("%s", e.what());
        return LDM7_INVAL;
    }
}
