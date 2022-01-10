/*
 * frameCircBufImpl.h
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#ifndef NOAAPORT_FRAMECIRCBUFIMPL_H_
#define NOAAPORT_FRAMECIRCBUFIMPL_H_

#include <condition_variable>
#include <cstring>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

class FrameCircBuf
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
    };

    /**
     * A slot for a frame.
     *           */
    struct Slot {
        const char  data[5000]; ///< Frame data
        unsigned    numBytes;   ///< Number of bytes of data in the frame

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

    Mutex    mutex;   ///< Supports concurrent access
    Cond     cond;
    Index    nextIndex;
    Indexes  indexes; ///< Indexes of frames in sorted order
    Slots    slots;   ///< Slots for frames

public:
    /**
     * Constructs.
     *
     * @param[in] numFrames  Number of frames to hold
     */
    FrameCircBuf(unsigned numFrames);

    FrameCircBuf(const FrameCircBuf& other) =delete;
    FrameCircBuf& operator=(const FrameCircBuf& rhs) =delete;

    /**
     * Adds a frame.
     *
     * @param[in] runNum    Frame run number
     * @param[in] seqNum    Frame sequence number
     * @param[in] data      Frame data
     * @param[in] numBytes  Number of bytes in the frame
     * @threadsafety        Safe
     */
    void add(
            const unsigned runNum,
            const unsigned seqNum,
            const char*    data,
            const unsigned numBytes);

    /**
     * Returns the oldest frame. Blocks until it's available.
     *
     * @param[out] runNum    Frame run number
     * @param[out] seqNum    Frame sequence number
     * @param[out] data      Frame data.
     * @param[out] numBytes  Number of bytes in the frame
     * @threadsafety         Safe
     * @see                  `releaseFrame()`
     */
    void getOldestFrame(
            const unsigned& runNum,
            const unsigned& seqNum,
            const char*&    data,
            const unsigned& numBytes);

    /**
     * Releases the resources of the frame returned by `getOldestFrame()`.
     *
     * @threadsafety Safe
     * @see          `getOldestFrame()`
     */
    void releaseFrame();
};

#endif /* NOAAPORT_FRAMECIRCBUFIMPL_H_ */
