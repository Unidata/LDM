/*
 * frameCircBufImpl.c
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#include "frameCircBufImpl.h"


/*
    struct Key {
    struct Slot {
        Key         key;      ///< Key for this slot
        const char* data;     ///< Frame data

    std::map<Key, int> indexes; ///< Indexes of frames in sorted order
    std::vector<Slot>  slots;   ///< Slots for frames
    Key                head;    ///< Key of oldest frame
    Key                tail;    ///< Key of newest frame
*/

/*
 * Constructor:
 * @param[in] numFrames  Number of frames to hold
 *
 */
FrameCircBuf::FrameCircBuf(unsigned numFrames)
{
}

 /* Adds a frame.
  * @param[in] runNum    Frame run number
  * @param[in] seqNum    Frame sequence number
  * @param[out] data      Frame data.
  * @param[out] numBytes  Number of bytes in the frame
  */
void FrameCircBuf::add(unsigned numFrames)
{
}

 /*
  * Returns the oldest frame. Blocks until it's available.
  *
  * @param[out] runNum    Frame run number
  * @param[out] seqNum    Frame sequence number
  * @param[out] data      Frame data.
  * @param[out] numBytes  Number of bytes in the frame
  */
void FrameCircBuf::getOldestFrame(
            const unsigned& runNum,
            const unsigned& seqNum,
            const char*&    data,
            const unsigned& numBytes)
{
}

/*
 * Releases the resources of the frame returned by `getOldestFrame()`.
 */
void FrameCircBuf::releaseFrame()
{
}

int main()
{
}

