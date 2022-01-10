/*
 * frameCircBufImpl.c
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#include "config.h"

#include "CircFrameBuf.h"

#include "log.h"


CircFrameBuf::CircFrameBuf(unsigned numFrames)
    : mutex()
    , cond()
    , nextIndex(0)
    , indexes()
    , slots(numFrames)
    , lastOldestKey()
    , getOldestCalled(false)
{}

bool CircFrameBuf::add(
        const unsigned runNum,
        const unsigned seqNum,
        const char*    data,
        const unsigned numBytes)
{
    Guard guard{mutex}; /// RAII!
    Key   key{runNum, seqNum};

    if (getOldestCalled && key < lastOldestKey)
        return false; // Frame arrived too late
    if (!indexes.insert({key, nextIndex}).second)
        return false; // Frame already added

    slots.emplace(nextIndex, Slot{data, numBytes});
    ++nextIndex;
    cond.notify_one();
    return true;
}

void CircFrameBuf::getOldestFrame(
        unsigned&    runNum,
        unsigned&    seqNum,
        const char*& data,
        unsigned&    numBytes)
{
    Lock  lock{mutex}; /// RAII!
    cond.wait(lock, [&]{return !indexes.empty();});

    auto  head = indexes.begin();
    auto  key = head->first;
    auto  index = head->second;
    auto& slot = slots.at(index);

    runNum = key.runNum;
    seqNum = key.seqNum;
    data   = slot.data;
    numBytes = slot.numBytes;

    slots.erase(index);
    indexes.erase(head);

    lastOldestKey = key;
    getOldestCalled = true;
}

extern "C" {

void* cfb_new(unsigned numFrames) {
    void* cfb = nullptr;
    try {
        cfb = new CircFrameBuf(numFrames);
    }
    catch (const std::exception& ex) {
        log_add("Couldn't allocate new circular frame buffer: %s", ex.what());
    }
    return cfb;
}

bool cfb_add(
        void*          cfb,
        const unsigned runNum,
        const unsigned seqNum,
        const char*    data,
        const unsigned numBytes) {
    bool success = false;
    try {
        static_cast<CircFrameBuf*>(cfb)->add(runNum, seqNum, data, numBytes);
        success = true;
    }
    catch (const std::exception& ex) {
        log_add("Couldn't add new frame: %s", ex.what());
    }
    return success;
}

bool cfb_getOldestFrame(
        void*        cfb,
        unsigned&    runNum,
        unsigned&    seqNum,
        const char*& data,
        unsigned&    numBytes) {
    bool success = false;
    try {
        static_cast<CircFrameBuf*>(cfb)->getOldestFrame(runNum, seqNum, data,
                numBytes);
        success = true;
    }
    catch (const std::exception& ex) {
        log_add("Couldn't get oldest frame: %s", ex.what());
    }
    return success;
}

void cfb_delete(void* cfb) {
    delete static_cast<CircFrameBuf*>(cfb);
}

}
