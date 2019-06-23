/**
 * Copyright (C) 2019 University of Virginia. All rights reserved.
 * 
 * @file      SockToIndexMap.h
 * @author    Steven Emmerson <emmerson@ucar.edu>
 * @version   1.0
 * @date      Jun 20, 2019
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
 * @brief     Map from socket descriptor to unreleased product indexes.
 *
 * Allows data-products that a receiving node has locked to be released if the
 * connection to the node is broken.
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SENDER_SOCKTOINDEXMAP_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SENDER_SOCKTOINDEXMAP_H_

#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <set>

class SockToIndexMap final
{
public:
    typedef std::set<uint32_t>       IndexSet;

private:
    typedef std::map<int, IndexSet>  Map;
    typedef std::mutex               Mutex;
    typedef std::lock_guard<Mutex>   Guard;

    Map           map;
    mutable Mutex mutex;

    inline void guardedErase(
            const int      sd,
            const uint32_t index)
    {
        auto indexes = map.find(sd);

        if (indexes != map.end())
            indexes->second.erase(index);
    }

public:
    typedef Map::iterator iterator;

    SockToIndexMap()
        : map()
        , mutex()
    {}

    void insert(
            const std::list<int>& socks,
            const uint32_t        index)
    {
        Guard guard(mutex);

        for (auto sd : socks)
            map[sd].insert(index);
    }

    void erase(
            const int      sd,
            const uint32_t index)
    {
        Guard guard(mutex);
        guardedErase(sd, index);
    }

    void erase(
            const std::list<int>& socks,
            const uint32_t        index)
    {
        Guard guard(mutex);

        for (auto sd : socks)
            guardedErase(sd, index);
    }

    void erase(const int sd)
    {
        Guard guard(mutex);
        map.erase(sd);
    }

    const IndexSet& find(const int sd)
    {
        Guard guard(mutex);
        return map[sd];
    }
};

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SENDER_SOCKTOINDEXMAP_H_ */
