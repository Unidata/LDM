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
        inline static int compare16(
                const unsigned lhs,
                const unsigned rhs) {
            return lhs - rhs > UINT16_MAX
                    ? -1
                    : lhs == rhs
                      ? 0
                      : 1;
        }

        inline static int compare32(
                const unsigned lhs,
                const unsigned rhs) {
            return lhs - rhs > UINT32_MAX
                    ? -1
                    : lhs == rhs
                      ? 0
                      : 1;
        }

    public:
        using Clock = std::chrono::steady_clock;
        using Dur   = std::chrono::milliseconds;

        unsigned          uplinkId;
        unsigned          fhSource;
        unsigned          fhSeqNum;
        unsigned          fhRunNum;
        unsigned          pdhSeqNum;
        unsigned          pdhBlkNum;
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
         *   - About 20 minutes when the NCF is switched;
         *   - Less than 10 seconds when the data server is switched; and
         *   - An amount yet to be learned when the MGS is switched.
         *
         * @param[in] rhs  The right-hand-side instance
         * @retval true    This instance is considered less than the other
         * @retval false   This instance is not considered less than the other
         */
        bool operator<(const Key& rhs) const {
#if 0
            if (uplinkId - rhs.uplinkId > UPLINK_ID_MAX/2)
                return true;
            if (uplinkId == rhs.uplinkId) {
                if (fhSeqNum - rhs.fhSeqNum > SEQ_NUM_MAX/2)
                    return true;
                if (fhSeqNum == rhs.fhSeqNum) {
                    if (pdhSeqNum - rhs.pdhSeqNum > SEQ_NUM_MAX/2)
                        return true;
                    if (pdhSeqNum == rhs.pdhSeqNum) {
                        if (pdhBlkNum - rhs.pdhBlkNum > BLK_NUM_MAX/2)
                            return true;
                    }
                }
            }
#else
            const int srcCmp = compare32(this->uplinkId, rhs.uplinkId);

            // NCF is changed (=> uplink ID is incremented)
            if (srcCmp < 0)
                return true;

            const int prodSeqCmp = compare32(this->pdhSeqNum, rhs.pdhSeqNum);
            const int blkNumCmp = compare16(this->pdhBlkNum, rhs.pdhBlkNum);

            /*
             * NCF & data server are unchanged. MGS is changed (=> product sequence number
             * and data block number increment normally).
             */
            if (srcCmp == 0 && (prodSeqCmp < 0 || (prodSeqCmp == 0 && blkNumCmp < 0)))
                return true;

            const int fhSeqCmp = compare32(this->fhSeqNum, rhs.fhSeqNum);

            // NCF and MGS are unchanged. Data server is changed (=> product sequence number reset).
            if (srcCmp == 0 && fhSeqCmp < 0 && prodSeqCmp > 0)
                return true;
#endif
            return false;
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
     * Returns the oldest frame. Returns immediately if the next frame is the
     * immediate successor to the previously-returned frame; otherwise, blocks
     * until a frame is available and the timeout occurs.
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
