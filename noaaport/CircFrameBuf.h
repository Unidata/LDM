/*
 * frameCircBufImpl.h
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#ifndef NOAAPORT_CIRCFRAMEBUF_H_
#define NOAAPORT_CIRCFRAMEBUF_H_

#ifdef __cplusplus

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

class CircFrameBuf
{
    /**
     * Frame run number and sequence number pair.
     */
    struct Key {
        unsigned runNum;
        unsigned seqNum;

        Key(unsigned runNum, unsigned seqNum)
            : runNum(runNum)
            , seqNum(seqNum)
        {}

        Key()
            : Key(0, 0)
        {}

        bool operator<(const Key& rhs) const {
            return (runNum < rhs.runNum)
                    ? true
                    : (runNum > rhs.runNum)
                          ? false
                          : seqNum < rhs.seqNum;
        }

        bool isNextAfter(const Key& key) const {
            return (runNum == key.runNum) && (seqNum == key.seqNum + 1);
        }
    };

    /**
     * A slot for a frame.
     *           */
    struct Slot {
        char     data[5000]; ///< Frame data
        unsigned numBytes;   ///< Number of bytes of data in the frame

        Slot(const char* data, unsigned numBytes)
            : data()
            , numBytes(numBytes)
        {
            if (numBytes > sizeof(this->data))
                throw std::runtime_error("Too many bytes");
            ::memcpy(this->data, data, numBytes);
        }
    };

    using Mutex   = std::mutex;
    using Cond    = std::condition_variable;
    using Index   = unsigned;
    using Guard   = std::lock_guard<Mutex>;
    using Lock    = std::unique_lock<Mutex>;
    using Indexes = std::map<Key, Index>;
    using Slots   = std::unordered_map<Index, Slot>;
    using Dur     = std::chrono::milliseconds;

    Mutex    mutex;           ///< Supports concurrent access
    Cond     cond;            ///< Supports concurrent access
    Index    nextIndex;       ///< Index for next, incoming frame
    Indexes  indexes;         ///< Indexes of frames in sorted order
    Slots    slots;           ///< Slots for frames
    Key      lastOldestKey;   ///< Key of last, returned frame
    bool     frameReturned; ///< Oldest frame returned?
    Dur      timeout;         ///< Timeout for returning next frame

public:
    /**
     * Constructs.
     *
     * @param[in] numFrames  Initial number of frames to hold
     * @param[in] timeout    Timeout value, in seconds, for returning oldest
     *                       frame
     * @see                  `getOldestFrame()`
     */
    CircFrameBuf(
            const unsigned numFrames,
            const double   timeout);

    CircFrameBuf(const CircFrameBuf& other) =delete;
    CircFrameBuf& operator=(const CircFrameBuf& rhs) =delete;

    /**
     * Adds a frame. The frame will not be added if
     *   - It is an earlier frame than the last, returned frame
     *   - The frame was already added
     *
     * @param[in] runNum    Frame run number
     * @param[in] seqNum    Frame sequence number
     * @param[in] data      Frame data
     * @param[in] numBytes  Number of bytes in the frame
     * @threadsafety        Safe
     * @see                 `getOldestFrame()`
     */
    void add(
            const unsigned runNum,
            const unsigned seqNum,
            const char*    data,
            const unsigned numBytes);

    /**
     * Returns the oldest frame. Returns immediately if the next frame is the
     * immediate successor to the previously-returned frame; otherwise, blocks
     * until a frame is available and the timeout occurs.
     *
     * @param[out] runNum    Frame run number
     * @param[out] seqNum    Frame sequence number
     * @param[out] data      Frame data.
     * @param[out] numBytes  Number of bytes in the frame
     * @threadsafety         Safe
     * @see                  `CircFrameBuf()`
     */
    void getOldestFrame(
            unsigned*     runNum,
            unsigned*     seqNum,
            const char**  data,
            unsigned*     numBytes);
};

extern "C" {

#endif // __cplusplus

void* cfb_new(
        const unsigned numFrames,
        const double   timeout);
void  cfb_add(
        void*          cfb,
        const unsigned runNum,
        const unsigned seqNum,
        const char*    data,
        const unsigned numBytes);
void cfb_getOldestFrame(
        void*        cfb,
        unsigned*    runNum,
        unsigned*    seqNum,
        const char** data,
        unsigned*    numBytes);
void cfb_delete(void* cfb);

#ifdef __cplusplus
}
#endif

#endif /* NOAAPORT_CIRCFRAMEBUF_H_ */
