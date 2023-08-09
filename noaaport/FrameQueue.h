/**
 * FrameQueue.h
 *
 *  Created on: Jan 10, 2022
 *      Author: Mustapha Iles
 *      Author: Steven R. Emmerson
 */

#ifndef NOAAPORT_FRAMEQUEUE_H_
#define NOAAPORT_FRAMEQUEUE_H_

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

using SbnSrc        = unsigned;
using UplinkId      = uint32_t;

static constexpr UplinkId UPLINK_ID_MAX = UINT32_MAX;

/**
 * Returns a monotonically increasing uplink identifier. This identifier increments every time the
 * source field in the frame header changes -- even if it reverts to the previous value. This
 * assumes that the delay-time of frames in the buffer is much less than the time between
 * changes to the uplink site so that all frames from the previous change will be gone from the
 * buffer.
 *
 * @param[in] fhSrc  Source field in the frame-level header
 * @return           Monotonically increasing uplink identifier
 */
UplinkId getUplinkId(const unsigned sbnSrc);

class FrameQueue
{
    /**
     * Key for sorting NOAAPort frames in temporal order.
     */
    class Key {
        /// Class for comparing two keys
        class Comparison {
            /**
             * Compares two unsigned integers. Returns a value that is less than, equal to, or
             * greater than zero depending on whether the first value is considered less than, equal
             * to, or greater than the second, respectively.
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
            const int uplinkCmp;  ///< Comparison of uplink IDs
            const int prodSeqCmp; ///< Comparison of product sequence numbers
            const int blkNumCmp;  ///< Comparison of data block numbers
            const int fhSeqCmp;   ///< comparison of frame-level header sequence numbers

            /**
             * Constructs.
             * @param[in] lhs  The first frame
             * @param[in] rhs  The second frame
             */
            Comparison(
                    const Key& lhs,
                    const Key& rhs)
                : uplinkCmp (compare(lhs.uplinkId,  rhs.uplinkId))
                , prodSeqCmp(compare(lhs.pdhSeqNum, rhs.pdhSeqNum))
                , blkNumCmp (compare(lhs.pdhBlkNum, rhs.pdhBlkNum))
                , fhSeqCmp  (compare(lhs.fhSeqNum,  rhs.fhSeqNum))
            {}

            /**
             * Indicates if the first frame was uplinked earlier with no significant change to the
             * uplink path. This also handles a change to the master ground station (i.e., an
             * arbitrary change to the frame-level sequence number).
             * @return    true        The frame was uplinked earlier
             * @return    false       The frame was not uplinked earlier
             */
            inline bool earlierAndNoChange() const {
                return uplinkCmp == 0 && (prodSeqCmp < 0 || (prodSeqCmp == 0 && blkNumCmp < 0));
            }

            /**
             * Indicates if the first frame was uplinked earlier even though the NCF changed.
             * @return    true        The frame was uplinked earlier
             * @return    false       The frame was not uplinked earlier
             */
            inline bool earlierButNcfChange() const {
                return uplinkCmp < 0;
            }

            /**
             * Indicates if the first frame was uplinked earlier even though the data server at the
             * NCF changed.
             * @return    true        The frame was uplinked earlier
             * @return    false       The frame was not uplinked earlier
             */
            inline bool earlierButSrvrChange() const {
                return uplinkCmp == 0 && fhSeqCmp > 0 && prodSeqCmp < 0;
            }
        };

    public:
        using Clock = std::chrono::steady_clock; ///< The clock being used
        using Dur   = std::chrono::milliseconds; ///< Duration resolution

        unsigned int      uplinkId;   ///< Monotonically increasing uplink ID
        unsigned int      fhSource;   ///< Source number  in the frame-level header
        unsigned int      fhSeqNum;   ///< Sequence number in the frame-level header
        unsigned int      fhRunNum;   ///< Run number in the frame-level header
        unsigned int      pdhSeqNum;  ///< Product sequence number in the Product Definition Header
        unsigned int      pdhBlkNum;  ///< Block number in the Product Definition Header
        Clock::time_point revealTime; ///< When the associated frame *must* be processed

        /**
         * Constructs.
         * @param[in] fh       Frame-level header
         * @param[in] pdh      Product definition header
         * @param[in] timeout  Reveal-time timeout
         * @threadsafety       Unsafe but compatible
         */
        Key(    const NbsFH&  fh,
                const NbsPDH& pdh,
                Dur&          timeout)
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
     * A NOAAPort frame.
     */
    struct Frame {
        char              bytes[SBN_FRAME_SIZE]; ///< Frame bytes
        FrameSize_t       numBytes;              ///< Number of bytes in the frame

        /**
         * Constructs.
         * @param[in] bytes     Frame bytes
         * @param[in] numBytes  Number of bytes in the frame
         */
        Frame(  const char*       bytes,
                const FrameSize_t numBytes)
            : bytes()
            , numBytes(numBytes)
        {
            if (numBytes > sizeof(this->bytes))
                throw std::runtime_error("Frame is too large: " + std::to_string(numBytes) +
                        " bytes.");
            ::memcpy(this->bytes, bytes, numBytes);
        }
    };

    using Mutex   = std::mutex;
    using Cond    = std::condition_variable;
    using Guard   = std::lock_guard<Mutex>;
    using Lock    = std::unique_lock<Mutex>;
    using Frames  = std::map<Key, Frame>;

    mutable Mutex mutex;           ///< Supports thread safety
    mutable Cond  cond;            ///< Supports concurrent access
    Frames        frames;          ///< Frames in temporally sorted order
    Key           lastOutputKey;   ///< Key of last, returned frame
    bool          frameReturned;   ///< Oldest frame returned?
    Key::Dur      timeout;         ///< Timeout for returning next frame

public:
    /**
     * Constructs.
     *
     * @param[in] timeout    Timeout value, in seconds, before unconditionally returning the oldest
     *                       frame if it exists
     * @see                  `getOldestFrame()`
     */
    FrameQueue(const double timeout);

    FrameQueue(const FrameQueue& other) =delete;
    FrameQueue& operator=(const FrameQueue& rhs) =delete;

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
     * @see                  `FrameQueue()`
     */
    void getOldestFrame(Frame_t* frame);
};

extern "C" {

#endif // __cplusplus

/**
 * Returns a new frame queue.
 *
 * @param[in] timeout    Timeout, in seconds, before unconditionally returning the oldest frame if
 *                       it exists
 * @retval    NULL       Fatal error. `log_add()` called.
 * @return               Pointer to a new frame queue
 * @see                  `fq_getOldestFrame()`
 * @see                  `fq_delete()`
 */
void* fq_new(const double timeout);

/**
 * Adds a new frame.
 *
 * @param[in] fq            Pointer to frame queue
 * @param[in] fh            Frame-level header
 * @param[in] pdh           Product-description header
 * @param[in] prodSeqNum    PDH product sequence number
 * @param[in] dataBlkNum    PDH data-block number
 * @param[in] data          Frame data
 * @param[in] numBytes      Number of bytes of data
 * @retval    0             Success
 * @retval    1             Frame not added because it arrived too late
 * @retval    2             Frame not added because it's a duplicate
 * @retval    -1            System error. `log_add()` called.
 */
int fq_add(
        void*             fq,
        const NbsFH*      fh,
        const NbsPDH*     pdh,
        const char*       data,
        const FrameSize_t numBytes);

/**
 * Returns the oldest frame if it exists. Blocks until it does.
 *
 * @param[in]  fq        Pointer to frame queue
 * @param[out] frame     Buffer to hold the oldest frame
 * @retval    `false`    Fatal error. `log_add()` called.
 */
bool fq_getOldestFrame(
        void*        fq,
        Frame_t*     frame);

/**
 * Deletes a frame queue.
 *
 * @param[in]  fq        Pointer to frame queue
 */
void fq_delete(void* fq);

/**
 * Return number of frames in a frame queue.
 *
 * @param[in]  fq        Pointer to frame queue
 * @param[out] nbf       Number of frames
 */
void fq_getNumberOfFrames(
        void*     fq,
        unsigned* nbf);

#ifdef __cplusplus
}
#endif

#endif /* NOAAPORT_FRAMEQUEUE_H_ */
