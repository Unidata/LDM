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
 * @brief     Map from socket descriptor to locked product indexes.
 *
 * Allows data-products that a receiving node has locked to be released if the
 * connection to the node is broken.
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SENDER_SOCKTOINDEXMAP_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SENDER_SOCKTOINDEXMAP_H_

#include <memory>
#include <mutex>
#include <utility>
#include <set>

class SockToIndexMap final
{
    typedef std::pair<int,uint32_t> Element;

    static bool compare(
            const Element& lhs,
            const Element& rhs)
    {
        return (lhs.first < rhs.first)
                ? true
                : (lhs.first > rhs.first)
                  ? false
                  : (lhs.second < rhs.second);
    }

    typedef std::set<Element, decltype(&compare)> Set;
    typedef std::mutex                            Mutex;
    typedef std::lock_guard<Mutex>                Guard;

    Set           set;
    mutable Mutex mutex;

public:
    typedef Set::iterator                         iterator;

    SockToIndexMap()
        : set(compare)
        , mutex()
    {}

    void insert(
            const std::list<int>& socks,
            const uint32_t        index)
    {
        Guard guard(mutex);

        for (auto sd : socks)
            set.emplace(sd, index);
    }

    void erase(
            const int      sock,
            const uint32_t index)
    {
        Guard guard(mutex);

        set.erase(Element(sock, index));
    }

    void erase(
            const std::list<int>& socks,
            const uint32_t        index)
    {
        Guard guard(mutex);

        for (auto sd : socks)
            set.erase(Element(sd, index));
    }

    void erase(const int sd)
    {
        Guard guard(mutex);
        set.erase(set.lower_bound({sd,0}), set.upper_bound({sd,UINT32_MAX}));
    }

    typedef std::shared_ptr<std::vector<uint32_t>> FindResult;

    FindResult find(const int sd) const
    {
        Guard guard(mutex);
        auto* indexes = new FindResult::element_type(set.size());

        for (auto elt = set.lower_bound({sd,0}),
                end = set.upper_bound({sd,UINT32_MAX}); elt != end; ++elt)
            indexes->push_back(elt->second);

        return FindResult(indexes);
    }
};

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SENDER_SOCKTOINDEXMAP_H_ */
