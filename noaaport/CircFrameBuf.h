/*
 * frameCircBufImpl.h
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#ifndef NOAAPORT_CIRCFRAMEBUF_H_
#define NOAAPORT_CIRCFRAMEBUF_H_

#include "NbsHeaders.h"
#include "noaaportFrame.h"

#ifdef __cplusplus

#include <chrono>
#include <condition_variable>
#include <climits>
#include <cstring>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

using SbnSrc        = unsigned;
using UplinkId      = uint32_t;

static constexpr UplinkId UPLINK_ID_MAX = UINT32_MAX;

UplinkId getUplinkId(const unsigned sbnSrc);

class CircFrameBuf
{
    /**
     * Key to sorting NOAAPort frames in temporal order.
     */
    class Key {
        /// Class for comparing two NOAAPort frames
        class Comparison {
            /**
             * Compares two values. Returns a value that is less than, equal to, or greater than zero
             * depending on whether the first value is considered less than, equal to, or greater than
             * the second, respectively.
             * @param[in] lhs  First value
             * @param[in] rhs  Second value
             * @retval    <0   First value is less than the second
             * @retval     0   First value is equal to the second
             * @retval    >0   First value is greater than the second
             */
            inline static int compare(
                    const unsigned int lhs,
                    const unsigned int rhs) {
                return lhs - rhs > UINT_MAX/2
                        ? -1
                        : lhs == rhs
                          ? 0
                          : 1;
            }

        public:
            const int srcCmp;     ///< Comparison of uplink IDs
            const int prodSeqCmp; ///< Comparison of product sequence numbers
            const int blkNumCmp;  ///< Comparison of data block numbers
            const int fhSeqCmp;   ///< comparison of frame-level sequence numbers

            Comparison(
                    const Key& lhs,
                    const Key& rhs)
                : srcCmp(    compare(lhs.uplinkId,  rhs.uplinkId))
                , prodSeqCmp(compare(lhs.pdhSeqNum, rhs.pdhSeqNum))
                , blkNumCmp( compare(lhs.pdhBlkNum, rhs.pdhBlkNum))
                , fhSeqCmp(  compare(lhs.fhSeqNum,  rhs.fhSeqNum))
            {}

            /**
             * Indicates if a frame was uplinked earlier with not change to the uplink path. This
             * also handles a change to the master ground station (i.e., arbitrary change to the
             * frame-level sequence number).
             * @param[in] srcCmp      Uplink ID comparison
             * @param[in] prodSeqCmp  Product sequence number comparison
             * @param[in] blkNumCmp   Data block number comparison
             * @param[in] fhSeqCmp    Frame-level sequence number comparison
             * @return    true        The frame was uplinked earlier
             * @return    false       The frame was not uplinked earlier
             */
            inline bool earlierAndNoChange() const {
                return srcCmp == 0 && (prodSeqCmp < 0 || (prodSeqCmp == 0 && blkNumCmp < 0));
            }

            inline bool earlierButNcfChange() const {
                return srcCmp < 0;
            }

            inline bool earlierButSrvrChange() const {
                return srcCmp == 0 && fhSeqCmp > 0 && prodSeqCmp < 0;
            }
        };

    public:
        using Clock = std::chrono::steady_clock;
        using Dur   = std::chrono::milliseconds;

        unsigned int      uplinkId;
        unsigned int      fhSource;
        unsigned int      fhSeqNum;
        unsigned int      fhRunNum;
        unsigned int      pdhSeqNum;
        unsigned int      pdhBlkNum;
        Clock::time_point revealTime; ///< When the associated frame *must* be processed

        /**
         * Constructs.
         * @param[in] fh       Frame-level header
         * @param[in] pdh      Product definition header
         * @param[in] timeout  Reveal-time timeout
         * @threadsafety       Unsafe but compatible
         */
        Key(const NbsFH& fh, const NbsPDH& pdh, Dur& timeout)
            : uplinkId(getUplinkId(fh.source))
            , fhSource(fh.source)
            , fhSeqNum(fh.seqno)
            , fhRunNum(fh.runno)
            , pdhSeqNum(pdh.prodSeqNum)
            , pdhBlkNum(pdh.blockNum)
            , revealTime(Clock::now() + timeout)
        {}

        Key()
            : uplinkId(0)
            , fhSource(0)
            , fhSeqNum(0)
            , fhRunNum(0)
            , pdhSeqNum(0)
            , pdhBlkNum(0)
            , revealTime(Clock::now())
        {}

        std::string to_string() const {
            return "{upId=" + std::to_string(uplinkId) +
                    ", fhSrc=" + std::to_string(fhSource) +
                    ", fhRun=" + std::to_string(fhRunNum) +
                    ", fhSeq=" + std::to_string(fhSeqNum) +
                    ", pdhSeq=" + std::to_string(pdhSeqNum) +
                    ", pdhBlk=" + std::to_string(pdhBlkNum) + "}";
        }

        /**
         * Indicates whether this instance is considered less than (i.e., was uplinked before)
         * another instance.
         * Things that can happen:
         *   - When the uplink is switched between primary and backup network control facilities
         *     (NCF):
         *     - The frame-level sequence number changes
         *     - The frame-level source field changes
         *     - The product sequence number is reset
         *   - When the master ground station (MGS) is switched at an NCF:
         *     - The frame-level sequence number changes
         *     - The frame-level source field doesn't change
         *     - The product sequence number increments normally
         *   - When the data uplink servers are switched at an NCF:
         *     - The frame-level sequence number increments normally
         *     - The frame-level source field doesn't change
         *     - The product sequence number is reset
         * Consequently, it's as if:
         *   - The NCF determines the frame-level source field
         *   - The uplink/data server determines the product sequence number
         *   - The MGS determines the frame-level sequence number
         * According to Sathya Sankarasubbu, the NOAAPort uplink will be offline
         *   - 10 to 15 minutes when the NCF is switched;
         *   - 10 to 30 seconds when the MGS is switched; and
         *   - Less than 10 seconds when the data server is switched.
         *
         * @param[in] rhs  The right-hand-side instance
         * @retval true    This instance is considered less than the other
         * @retval false   This instance is not considered less than the other
         */
        bool operator<(const Key& rhs) const {
            const Comparison cmp(*this, rhs);
            return cmp.earlierAndNoChange() ||
                   cmp.earlierButNcfChange() ||
                   cmp.earlierButSrvrChange();
        }
    };

    /**
     * A slot for a frame.
     */
    struct Slot {
        char              data[SBN_FRAME_SIZE]; ///< Frame data
        FrameSize_t       numBytes;             ///< Number of bytes of data in the frame

        Slot(const char* data, FrameSize_t numBytes)
            : data()
            , numBytes(numBytes)
        {
            if (numBytes > sizeof(this->data))
                throw std::runtime_error("Frame is too large: " + std::to_string(numBytes) +
                        " bytes.");
            ::memcpy(this->data, data, numBytes);
        }
    };

    using Mutex   = std::mutex;
    using Cond    = std::condition_variable;
    using Guard   = std::lock_guard<Mutex>;
    using Lock    = std::unique_lock<Mutex>;
    using Index   = unsigned;
    using Indexes = std::map<Key, Index>;
    using Slots   = std::unordered_map<Index, Slot>;

    mutable Mutex mutex;           ///< Supports thread safety
    mutable Cond  cond;            ///< Supports concurrent access
    Index         nextIndex;       ///< Index for next, incoming frame
    Indexes       indexes;         ///< Indexes of frames in sorted (hopefully temporal) order
    Slots         slots;           ///< Slots of frames in unsorted order
    Key           lastOutputKey;   ///< Key of last, returned frame
    bool          frameReturned;   ///< Oldest frame returned?
    Key::Dur      timeout;         ///< Timeout for returning next frame

public:
    /**
     * Constructs.
     *
     * @param[in] timeout    Timeout value, in seconds, for returning oldest
     *                       frame
     * @see                  `getOldestFrame()`
     */
    CircFrameBuf(const double timeout);

    CircFrameBuf(const CircFrameBuf& other) =delete;
    CircFrameBuf& operator=(const CircFrameBuf& rhs) =delete;

    /**
     * Adds a frame. The frame will not be added if
     *   - It is an earlier frame than the last, returned frame
     *   - The frame was already added
     *
     * @param[in] fh            Frame-level header
     * @param[in] pdh           Product-description header
     * @param[in] data          Frame data
     * @param[in] numBytes      Number of bytes in the frame
     * @retval    0             Frame added
     * @retval    1             Frame not added because it arrived too late
     * @retval    2             Frame not added because it's a duplicate
     * @threadsafety            Safe
     * @see                     `getOldestFrame()`
     */
    int add(
            const NbsFH&      fh,
            const NbsPDH&     pdh,
            const char*       data,
            const FrameSize_t numBytes);

    /**
     * Returns the oldest frame. Returns immediately if the next frame is the immediate successor to
     * the previously-returned frame; otherwise, blocks until a frame is available and the timeout
     * occurs.
     *
     * @param[out] frame     Buffer for the frame
     * @threadsafety         Safe
     * @see                  `CircFrameBuf()`
     */
    void getOldestFrame(Frame_t* frame);
};

extern "C" {

#endif // __cplusplus

/**
 * Returns a new circular frame buffer.
 *
 * @param[in] timeout    Timeout, in seconds, before the next frame must be
 *                       returned if it exists
 * @retval    NULL       Fatal error. `log_add()` called.
 * @return               Pointer to a new circular frame buffer
 * @see                  `cfb_getOldestFrame()`
 * @see                  `cfb_delete()`
 */
void* cfb_new(const double timeout);

/**
 * Adds a new frame.
 *
 * @param[in] cfb           Pointer to circular frame buffer
 * @param[in] fh            Frame-level header
 * @param[in] pdh           Product-description header
 * @param[in] prodSeqNum    PDH product sequence number
 * @param[in] dataBlkNum    PDH data-block number
 * @param[in] data          Frame data
 * @param[in] numBytes      Number of bytes of data
 * @retval    0             Success
 * @retval    1             Frame not added because it's too late
 * @retval    2             Frame not added because it's a duplicate
 * @retval    -1            System error. `log_add()` called.
 */
int cfb_add(
        void*             cfb,
        const NbsFH*      fh,
        const NbsPDH*     pdh,
        const char*       data,
        const FrameSize_t numBytes);

/**
 * Returns the next, oldest frame if it exists. Blocks until it does.
 *
 * @param[in]  cfb       Pointer to circular frame buffer
 * @param[out] frame     Buffer to hold the oldest frame
 * @retval    `false`    Fatal error. `log_add()` called.
 */
bool cfb_getOldestFrame(
        void*        cfb,
        Frame_t*     frame);

/**
 * Deletes a circular frame buffer.
 *
 * @param[in]  cfb       Pointer to circular frame buffer
 */
void cfb_delete(void* cfb);

/**
 * Return number of frames in a circular frame buffer.
 *
 * @param[in]  cfb       Pointer to circular frame buffer
 * @param[out] nbf       Number of frames
 */
void cfb_getNumberOfFrames(void* cfb, unsigned* nbf);


#ifdef __cplusplus
}
#endif

#endif /* NOAAPORT_CIRCFRAMEBUF_H_ */
