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
#include <functional>
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
     * Key for sorting NOAAPort frames and revealing them when their time comes
     */
    struct Key {
        using Clock = std::chrono::steady_clock;
        using Dur   = std::chrono::milliseconds;

        UplinkId          uplinkId;
        unsigned          fhSource;
        unsigned          fhSeqno;
        unsigned          fhRunno;
        unsigned          pdhSeqNum;
        unsigned          pdhBlkNum;
        Clock::time_point revealTime; ///< When the associated frame should be processed

        Key(const NbsFH& fh, const NbsPDH& pdh, Dur& timeout)
            : uplinkId(getUplinkId(fh.source))
            , fhSource(fh.source)
            , fhSeqno(fh.seqno)
            , fhRunno(fh.runno)
            , pdhSeqNum(pdh.prodSeqNum)
            , pdhBlkNum(pdh.blockNum)
            , revealTime(Clock::now() + timeout)
        {}

        Key(const NbsFH& fh, const NbsPDH& pdh, Dur&& timeout)
            : uplinkId(getUplinkId(fh.source))
            , fhSource(fh.source)
            , fhSeqno(fh.seqno)
            , fhRunno(fh.runno)
            , pdhSeqNum(pdh.prodSeqNum)
            , pdhBlkNum(pdh.blockNum)
            , revealTime(Clock::now() + timeout)
        {}

        Key()
            : uplinkId(0)
            , fhSource(0)
            , fhSeqno(0)
            , fhRunno(0)
            , pdhSeqNum(0)
            , pdhBlkNum(0)
            , revealTime(Clock::now())
        {}

        std::string to_string() const;

        /**
         * Indicates if this instance is considered less than another.
         *
         * Unfortunately, there's no way to simply and reliably determine the temporal ordering of
         * two arbitrary frames -- so the source ID, product sequence number, and product block
         * number are used as a heuristic. This can give incorrect results during a transition to a
         * new uplink site if the duration of the uplink gap is less than the latency from a
         * receiving host to this host.
         *
         * @param[in] rhs    The other instance
         * @retval    true   This instance is considered less than the other
         * @retval    false  This instance is not considered less than the other
         */
        bool operator<(const Key& rhs) const noexcept;

        /**
         * Indicates if this instance is considered equal to another.
         * @param[in] rhs    The other instance
         * @retval    true   This instance is considered less than the other
         * @retval    false  This instance is not considered less than the other
         * @see operator<()
         */
        bool operator==(const Key& rhs) const noexcept;

        /**
         * Returns the hash value of this instance.
         * @return The hash value of this instance
         * @see operator<()
         */
        struct Hash {
            size_t operator()(const Key& key) const noexcept {
                static auto myHash = std::hash<uint32_t>{};
                return myHash(key.fhSource) ^ myHash(key.pdhSeqNum) ^ myHash(key.pdhBlkNum);
            }
        };
    };

    /**
     * A frame in the queue.
     */
    struct Qframe {
        char              data[SBN_FRAME_SIZE]; ///< Frame data
        FrameSize_t       numBytes;             ///< Number of bytes of data in the frame

        Qframe(const char* data, FrameSize_t numBytes)
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

    mutable Mutex mutex;           ///< Supports thread safety
    mutable Cond  cond;            ///< Supports concurrent access
    using Qframes = std::unordered_map<Key, Qframe, Key::Hash>;
    Qframes       qFrames;         ///< Queue of frames
    Key           lastOutputKey;   ///< Key of last, returned frame
    bool          frameReturned;   ///< Oldest frame returned?
    Key::Dur      timeout;         ///< Timeout for returning next frame

public:
    /**
     * Constructs.
     *
     * @param[in] timeout  Timeout value, in seconds, for returning oldest frame
     * @see                `getOldestFrame()`
     */
    CircFrameBuf(const double  timeout);

    CircFrameBuf(const CircFrameBuf& other) =delete;
    CircFrameBuf& operator=(const CircFrameBuf& rhs) =delete;

    /**
     * Adds a frame. The frame will not be added if
     *   - It is an earlier frame than the last, returned frame
     *   - The frame was already added
     *
     * @param[in] serverId      Hostname or IP address and port number of streaming NOAAPort server
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
            const char*       serverId,
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
 * @param[in] timeout      Timeout, in seconds, before the next frame must be returned if it exists
 * @retval    NULL         Fatal error. `log_add()` called.
 * @return                 Pointer to a new circular frame buffer
 * @see                    `cfb_getOldestFrame()`
 * @see                    `cfb_delete()`
 */
void* cfb_new(const double timeout);

/**
 * Adds a new frame.
 *
 * @param[in] cfb           Pointer to circular frame buffer
 * @param[in] serverId      Hostname or IP address and port number of streaming NOAAPort server
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
        const char*       serverId,
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
