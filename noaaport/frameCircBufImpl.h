/*
 * frameCircBufImpl.h
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#ifndef NOAAPORT_FRAMECIRCBUFIMPL_H_
#define NOAAPORT_FRAMECIRCBUFIMPL_H_

#include <cstdint>
#include <map>
#include <vector>

class FrameCircBuf
{
    /**
     * Frame run number and sequence number pair.
     */
    struct Key {
        unsigned runNum;
        unsigned seqNum;
    };
    /**
     * A slot for a frame.
     *           */
    struct Slot {
        Key         key;      ///< Key for this slot
        const char* data;     ///< Frame data
        unsigned    numBytes; ///< Number of bytes of data in the frame
    };

    std::map<Key, int> indexes; ///< Indexes of frames in sorted order
    std::vector<Slot>  slots;   ///< Slots for frames
    Key                head;    ///< Key of oldest frame
    Key                tail;    ///< Key of newest frame

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
