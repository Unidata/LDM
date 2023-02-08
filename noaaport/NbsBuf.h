/**
 * This file declares a module for ordering NBS frames.
 *
 *  @file:  NbsBuf.h
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

#ifndef NOAAPORT_NBSBUF_H_
#define NOAAPORT_NBSBUF_H_

#include <cstdint>

#ifdef __cplusplus

class NbsFrameKey
{
public:
    using SessionId = uint32_t; ///< Type of session identifier
    using FhSeqNum  = uint32_t; ///< Type of frame header sequence number

    /**
     * Constructs.
     * @param[in] sessionId  Strictly monotonically increasing session identifier
     * @param[in] seqNum     Frame header sequence number
     */
    NbsFrameKey(
            const SessionId sessionId,
            const FhSeqNum  seqNum)
        : sessionId(sessionId)
        , fhSeqNum(seqNum)
    {}

    /// Default constructs.
    NbsFrameKey()
        : NbsFrameKey(0, 0)
    {}

    /**
     * Indicates if this instance compares less than another.
     * @param[in] rhs      The other instance
     * @retval    `true`   This instance compares less than the other
     * @retval    `false`  This instance does not compare less than the other
     */
    bool operator<(const NbsFrameKey& rhs) {
        return this->isLessThan(sessionId, rhs.sessionId) ||
                (sessionId == rhs.sessionId && this->isLessThan(fhSeqNum, rhs.fhSeqNum));
    }

    /**
     * Increments this instance to that of the expected next frame.
     */
    inline NbsFrameKey& operator++() {
        /**
         * NB: This assumes that no frames are ignored (because they're timing frames or test frames
         * for example) so that an instance of this class is created for every incoming frame. If
         * this isn't the case, then the product-description header's sequence number and block
         * number likely must be used.
         */
        ++fhSeqNum;
        return *this;
    }

private:
    SessionId sessionId; ///< Strictly monotonically increasing session identifier
    FhSeqNum fhSeqNum;   ///< Frame header sequence number

    /**
     * Indicates if one frame header sequence number compares less than another.
     * @param[in] lhs      Left-hand-side value
     * @param[in] rhs      Right-hand-side value
     * @retval    `true`   `lhs` is less than `rhs`
     * @retval    `false`  `lhs` is not less than `rhs`
     */
    inline static bool isLessThan(
            const uint32_t lhs,
            const uint32_t rhs) {
        return lhs - rhs > UINT32_MAX/2; // NB: Unsigned arithmetic
    }
};

#endif // __cplusplus

#endif /* NOAAPORT_NBSBUF_H_ */
