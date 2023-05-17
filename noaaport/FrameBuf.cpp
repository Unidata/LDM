/**
 * This file implements a buffer for putting real-time frames in strictly monotonic order.
 *
 *  @file:  FrameBuf.cpp
 * @author: Steven R. Emmerson <emmerson@ucar.edu>
 *
 *    Copyright 2023 University Corporation for Atmospheric Research
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied. See the License for the specific language governing permissions and limitations under
 * the License.
 */

#include "FrameBuf.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <map>
#include <thread>

/**
 * Buffer for putting real-time frames in strictly increasing monotonic order.
 * @tparam Key       Type of key for sorting frames in increasing order. Must have a copy
 *                   constructor, the operators "=" and "++", and support for the expression
 *                   `Key < Key`. The "++" operator must increment to the next expected value.
 * @tparam Frame     Type of frame to be sequenced. Must have a copy constructor and copy assignment
 *                   operator.
 * @tparam Duration  Type of the timeout value
 */
template<class Key, class Frame, class Duration>
class FrameBuf<Key, Frame, Duration>::Impl final
{
    using Clock = std::chrono::steady_clock; ///< The clock used internally
    using Time  = Clock::time_point;         ///< The time-point used internally
    using Dur   = Clock::duration;           ///< The duration used internally

    /// A frame augmented with the time when it should be consumed (i.e., returned to the caller)
    struct Aframe {
        Frame frame;       ///< The frame
        Time  consumeTime; ///< The time when this frame should be consumed
        /**
         * Constructs
         * @param[in] frame        The frame
         * @param[in] consumeTime  The time when this frame should be consumed
         */
        Aframe( Frame&      frame,
                const Time& consumeTime)
            : frame(frame)
            , consumeTime(consumeTime)
        {}
    };

    using Mutex    = std::mutex;
    using Guard    = std::lock_guard<Mutex>;
    using Cond     = std::condition_variable;
    using Lock     = std::unique_lock<Mutex>;
    using Keys     = std::map<Time, Key>;
    using Frames   = std::map<Key, Aframe>;
    using Thread   = std::thread;

    mutable Mutex mutex;            ///< To ensure consistency
    mutable Cond  cond;             ///< For inter-thread co-ordination
    const Dur     timeout;          ///< Failsafe timeout to unconditionally consume the next frame
    Keys          keys;             ///< Map from consume-times to frame keys
    Frames        aFrames;          ///< Map from frame keys to augmented frames
    bool          expectedKeyIsSet; ///< Is `expectedKey` set?
    Key           expectedKey;      ///< The key expected after that of the last consumed frame

    /**
     * Indicates if two frame keys are equal.
     * @param[in] key1     First frame key
     * @param[in] key2     Second frame key
     * @retval    `true`   Keys are equal
     * @retval    `false`  Keys are not equal
     * @throws             Whatever `Key < Key` throws
     * @exceptionsafety    If `Key < Key` throws then strong guarantee; else no throw
     */
    inline bool areEqual(
            const Key& key1,
            const Key& key2) {
        return !(key1 < key2) && !(key2 < key1); // Tests for equality
    }

    /**
     * Indicates if the first frame in the buffer is the expected one.
     * @pre               The mutex is locked
     * @pre               The buffer isn't empty
     * @retval `true`     Yes
     * @retval `false`    No
     * @throws            Whatever `Key < Key` throws
     * @exceptionsafety   If `Key < Key` throws then strong guarantee; else no throw
     * @post              The mutex is locked
     */
    inline bool isExpected() {
        assert(!mutex.try_lock());
        assert(!aFrames.empty());

        bool expected = false;
        if (expectedKeyIsSet) {
            const auto& firstKey = aFrames.begin()->first;
            expected = areEqual(firstKey, expectedKey);
        }
        return expected;
    }

    /**
     * Waits until the first frame in the buffer should be consumed.
     * @pre              The mutex is locked
     * @throws           Whatever `Key < Key` throws
     * @exceptionsafety  If `Key < Key` throws then strong guarantee; else no throw
     * @post             The mutex is locked
     */
    inline void waitForFrame(Lock& lock) {
        assert(!mutex.try_lock());

        // Wait until the buffer isn't empty
        if (aFrames.empty())
            cond.wait(lock, [&]{!aFrames.empty();});

        /*
         * and
         *   - The first frame in the buffer is the expected one; or
         *   - A timeout occurs for the earliest frame in the buffer
         */
        auto pred = [&]{isExpected() || keys.begin()->first <= Clock::now();};
        cond.wait_until(lock, keys.begin()->first, pred);
    }

public:
    /**
     * Constructs.
     * @param[in] timeout   Failsafe timeout for consuming the next frame even if it's not the
     *                      expected one. Increasing the value will decrease the risk of gaps but
     *                      increase latency when they occur.
     */
    Impl(const Duration& timeout)
        : mutex()
        , cond()
        , timeout(std::chrono::duration_cast<Dur>(timeout))
        , aFrames()
        , expectedKeyIsSet(false)
        , expectedKey()
    {}

    /**
     * Tries to insert a frame. The frame will not be inserted if its key doesn't compare greater
     * than that of the last consumed frame or if the frame is already in the buffer.
     * @param[in] key         The key of the frame to insert
     * @param[in] frame       The frame to insert
     * @retval    `true`      The frame was inserted
     * @retval    `false`     The frame was not inserted
     * @throws                Whatever `Key < Key` throws
     * @throws runtime_error  The clock used doesn't have sufficient resolution to make all
     *                        insertion times unique
     * @threadsafety          Safe
     * @exceptionsafety       Strong guarantee
     */
    bool tryInsert(
            const Key& key,
            Frame&     frame) {
        bool  inserted = false;
        Guard guard{mutex}; // Nothrow

        if (!expectedKeyIsSet || !(key < expectedKey)) { // Unknown
            const auto consumeTime = Clock::now() + timeout;
            // Arguments are copied:
            inserted = aFrames.emplace(key, Aframe{frame, consumeTime}).second; // Strong
            if (inserted) {
                try {
                    if (!keys.emplace(consumeTime, key).second) // Strong
                        throw std::runtime_error("Clock doesn't have sufficient resolution");
                    cond.notify_all(); // Nothrow
                }
                catch (const std::exception& ex) {
                    aFrames.erase(key);
                    throw;
                }
            }
        }

        return inserted;
    }

    /**
     * Returns the next frame.
     * @param[out] key    Key of the next frame
     * @param[out] frame  The next frame
     * @throws            Whatever `Key < Key` throws
     * @throws            Whatever `Key = Key` throws
     * @throws            Whatever `++Key` throws
     * @threadsafety      Safe
     * @exceptionsafety   If the `Key` operations throw, then strong guarantee; else no throw
     */
    void getFrame(
            Key&   key,
            Frame& frame) {
        Lock lock{mutex};

        waitForFrame(lock);
        auto  iter = aFrames.begin(); // Nothrow
        key = iter->first; // Unknown
        frame = iter->second.frame; // Unknown

        expectedKey = key; // Unknown
        ++expectedKey; // Unknown
        expectedKeyIsSet = true;

        keys.erase(iter->second.consumeTime); // Nothrow
        aFrames.erase(iter); // Nothrow
    }
};

template<class Key, class Frame, class Duration>
FrameBuf<Key, Frame, Duration>::FrameBuf(const Duration& timeout)
    : pImpl{new Impl{timeout}}
{}

template<class Key, class Frame, class Duration>
bool FrameBuf<Key, Frame, Duration>::tryInsert(
        const Key& key,
        Frame&     frame) const {
    return pImpl->tryInsert(key, frame);
}

template<class Key, class Frame, class Duration>
void FrameBuf<Key, Frame, Duration>::getFrame(
        Key&   key,
        Frame& frame) const {
    pImpl->getFrame(key, frame);
}
