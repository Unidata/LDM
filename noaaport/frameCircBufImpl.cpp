/*
 * frameCircBufImpl.c
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#include "frameCircBufImpl.h"
#include "log.h"


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
    : mutex()
    , cond()
    , indexes()
    , slots(numFrames)
{}

 /* Adds a frame.
  * @param[in] runNum    Frame run number
  * @param[in] seqNum    Frame sequence number
  * @param[out] data      Frame data.
  * @param[out] numBytes  Number of bytes in the frame
  */
void FrameCircBuf::add(
        const unsigned runNum,
        const unsigned seqNum,
        const char*    data,
        const unsigned numBytes)
{
    Guard guard{mutex}; /// RAII!
    Key   key{runNum, seqNum};
    indexes.insert({key, nextIndex});
    slots.emplace(nextIndex, Slot{data, numBytes});
    ++nextIndex;
    cond.notify_one();
}

 /**
  * Returns the oldest frame. Blocks until it's available.
  *
  * @param[out] runNum    Frame run number
  * @param[out] seqNum    Frame sequence number
  * @param[out] data      Frame data.
  * @param[out] numBytes  Number of bytes in the frame
  */
void FrameCircBuf::getOldestFrame(
        unsigned&    runNum,
        unsigned&    seqNum,
        const char*& data,
        unsigned&    numBytes)
{
    Lock  lock{mutex}; /// RAII!
    cond.wait(lock, []{return !indexes.empty();});

    auto head = indexes.begin();
    auto key = head->first;
    auto index = head->second;
    auto slot = slots[index];

    runNum = key.runNum;
    seqNum = key.seqNum;
    data   = slot.data;
    numBytes = slot.numBytes;

    slots.erase(index);
    indexes.erase(head);
}

extern "C" {

void* cfb_new(unsigned numFrames) {
    void* cfb = nullptr;
    try {
        cfb = new FrameCircBuf(numFrames);
    }
    catch (const std::exception& ex) {
        log_error("Couldn't allocate new circular frame buffer: %s", ex.what());
    }
    return cfb;
}

void  cfb_add(
        void*          cfb,
        const unsigned runNum,
        const unsigned seqNum,
        const char*    data,
        const unsigned numBytes) {
    try {
        static_cast<FrameCircBuf*>(cfb)->add(runNum, seqNum, data, numBytes);
    }
    catch (const std::exception& ex) {
        log_error("Couldn't add new frame: %s", ex.what());
    }
}
void cfb_getOldestFrame(
        void*        cfb,
        unsigned&    runNum,
        unsigned&    seqNum,
        const char*& data,
        unsigned&    numBytes) {
    try {
        static_cast<FrameCircBuf*>(cfb)->getOldestFrame(runNum, seqNum, data,
                numBytes);
    }
    catch (const std::exception& ex) {
        log_error("Couldn't get oldest frame: %s", ex.what());
    }
}

void cfb_delete(void* cfb) {
    delete static_cast<FrameCircBuf*>(cfb);
}

}
