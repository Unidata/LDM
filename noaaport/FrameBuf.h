/**
 * This file declares a buffer for putting real-time frames in strictly monotonic order.
 *
 *  @file:  FrameBuf.h
 * @author: Steven R. Emmerson <emmerson@ucar.edu>
 *
 *    Copyright 2023 University Corporation for Atmospheric Research
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NOAAPORT_FRAMEBUF_H_
#define NOAAPORT_FRAMEBUF_H_

#include <memory>

/**
 * Buffer for putting real-time frames in strictly increasing monotonic order.
 * @tparam Key       Type of key for sorting frames in increasing order. Must have a copy
 *                   constructor, the operators "=", and "++", and support for the expression
 *                   `Key < Key`. The "++" operator must increment to the next expected value.
 * @tparam Frame     Type of frame to be sequenced. Must have a copy constructor.
 * @tparam Duration  Type of the timeout value
 */
template<class Key, class Frame, class Duration>
class FrameBuf
{
private:
    class                 Impl;  ///< Implementation
    std::shared_ptr<Impl> pImpl; ///< Smart pointer to an implementation

    /**
     * Constructs from an implementation.
     * @param[in] impl  Pointer to an implementation
     */
    FrameBuf(Impl* const impl);

public:
    /**
     * Constructs.
     * @param[in] timeout   Failsafe timeout for consuming the next frame even if it's not the
     *                      expected one. Increasing the value will decrease the risk of gaps but
     *                      increase latency when they occur.
     */
    FrameBuf(const Duration& timeout);

    /// Copy constructs.
    FrameBuf(const FrameBuf& frameBuf) =delete;
    /// Copy assigns.
    FrameBuf& operator=(const FrameBuf& frameBuf) =delete;

    /**
     * Tries to insert a frame. The frame will not be inserted if its key doesn't compare greater
     * than that of the last consumed frame or if the frame is already in the buffer.
     * @param[in] key      The key of the frame to insert
     * @param[in] frame    The frame to insert
     * @retval    `true`   The frame was inserted
     * @retval    `false`  The frame was not inserted
     * @throws             Whatever `Key.operator<(const Key&)` throws
     * @throws             Whatever `Consumer.consume(const Key&, Frame&)` throws
     * @threadsafety       Safe
     * @exceptionsafety    Strong guarantee
     */
    bool tryInsert(
            const Key& key,
            Frame&     frame) const;

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
            Frame& frame) const;
};

#endif /* NOAAPORT_FRAMEBUF_H_ */
