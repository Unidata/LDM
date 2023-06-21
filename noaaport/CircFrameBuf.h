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
     * NOAAPort's product-definition header's product sequence number and data-block number
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
        Clock::time_point revealTime; ///< When the associated frame *must* be processed

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

        std::string to_string() const {
            return "{upId=" + std::to_string(uplinkId) +
                    ", fhSrc=" + std::to_string(fhSource) +
                    ", fhRun=" + std::to_string(fhRunno) +
                    ", fhSeq=" + std::to_string(fhSeqno) +
                    ", pdhSeq=" + std::to_string(pdhSeqNum) +
                    ", pdhBlk=" + std::to_string(pdhBlkNum) + "}";
        }

        bool operator<(const Key& rhs) const {
            if (uplinkId - rhs.uplinkId > UPLINK_ID_MAX/2)
                return true;
            if (uplinkId == rhs.uplinkId) {
                if (fhSeqno - rhs.fhSeqno > SEQ_NUM_MAX/2)
                    return true;
                if (fhSeqno == rhs.fhSeqno) {
                    if (pdhSeqNum - rhs.pdhSeqNum > SEQ_NUM_MAX/2)
                        return true;
                    if (pdhSeqNum == rhs.pdhSeqNum) {
                        if (pdhBlkNum - rhs.pdhBlkNum > BLK_NUM_MAX/2)
                            return true;
                    }
                }
            }
            return false;
        }

        bool isSameSite(const Key& rhs) const {
            return (uplinkId == rhs.uplinkId) && (fhRunno == rhs.fhRunno);
        }
    };

    /**
     * Factory for generating keys.
     */
    class KeyFactory {
        std::unordered_map<const char*, bool> goneRetro;
        Key                                   thresholdKey;
        bool                                  thresholdKeySet;

    public:
        KeyFactory(const int numServers)
            : goneRetro(numServers)
            , thresholdKey()
            , thresholdKeySet(false)
        {}

        /**
         * Adds a server.
         * @param[in] serverId  Hostname or IP address and port number of streaming NOAAPort server.
         *                      Caller must not free.
         */
        void add(const char* serverId);

        /**
         * Removes a server.
         * @param[in] serverId  Hostname or IP address and port number of streaming NOAAPort server.
         *                      Caller may free.
         */
        void remove(const char* serverId);

        /**
         * Sets the threshold key. `getKey()` will not return a key that compares less than or equal
         * to the threshold key.
         * @param[in] key  Threshold key
         */
        void setThreshold(const Key& key) {
            thresholdKey = key;
            thresholdKeySet = true;
        }

        /**
         * Returns the key corresponding to the input.
         * @param[in]  serverId  Hostname or IP address and port number of streaming NOAAPort
         *                       server. Caller must not free.
         * @param[in]  fh        NOAAPort frame header
         * @param[in]  pdh       NOAAPort product-definition header
         * @param[in]  timeout   How long to keep the frame before revealing it
         * @param[out] key       The key corresponding to the input
         * @retval     true      Success. `key` is set.
         * @retval     false     The returned key would be less than or equal to the threshold key.
         *                       `key` is not set.
         */
        bool getKey(
                const char*     serverId,
                const NbsFH&    fh,
                const NbsPDH&   pdh,
                const Key::Dur& timeout,
                Key&            key);
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
    unsigned      numEarly;        ///< Number of contiguous, early frames
    unsigned      maxNumEarly;     ///< Maximum number of contiguous, early frames before deciding
                                   ///< to just accept that the frame stream is valid
    unsigned      prevEarlyFhSeq;  ///< Previous, early, frame header sequence number
    bool          newFrameStream;  ///< Are we accepting a new frame stream?

    /**
     * Indicates if a frame is consistent with the frames being received from the other servers.
     * @param[in] serverId  Hostname or IP address and port number of streaming NOAAPort server from
     *                      which the frame was received. Caller must not free or modify.
     * @param[in] fh        Frame-level header
     * @param[in] pdh       Product-description header
     * @retval    true      The frame is consistent
     * @retval    false     The frame is not consistent
     */
    bool isConsistent(
            const char*   serverId,
            const NbsFH&  fh,
            const NbsPDH& pdh);

    int tryInsertFrame(
            const Key&        key,
            const char*       data,
            const FrameSize_t numBytes);

public:
    /**
     * Constructs.
     *
     * @param[in] timeout      Timeout value, in seconds, for returning oldest
     *                         frame
     * @param[in] maxNumEarly  Maximum number of contiguous, early frames before deciding
                               to just accept that the frame stream is valid
     * @see                  `getOldestFrame()`
     */
    CircFrameBuf(
            const double   timeout,
            const unsigned maxNumEarly);

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
 * @param[in] timeout      Timeout, in seconds, before the next frame must be
 *                         returned if it exists
 * @param[in] maxNumEarly  Maximum number of contiguous, early frames before deciding to just
 *                              accept the frame stream
 * @retval    NULL         Fatal error. `log_add()` called.
 * @return                 Pointer to a new circular frame buffer
 * @see                    `cfb_getOldestFrame()`
 * @see                    `cfb_delete()`
 */
void* cfb_new(
        const double   timeout,
        const unsigned maxNumEarly);

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
