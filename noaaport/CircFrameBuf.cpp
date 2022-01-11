/*
 * frameCircBufImpl.c
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#include "config.h"

#include "CircFrameBuf.h"
#include "log.h"

CircFrameBuf::CircFrameBuf(
        unsigned     numFrames,
        const double timeout)
    : mutex()
    , cond()
    , nextIndex(0)
    , indexes()
    , slots(numFrames)
    , lastOldestKey()
    , frameReturned(false)
    , timeout(std::chrono::duration_cast<Dur>(
            std::chrono::duration<double>(timeout)))
{}

void CircFrameBuf::add(
        const unsigned runNum,
        const unsigned seqNum,
        const char*    data,
        const unsigned numBytes)
{
    Guard guard{mutex}; /// RAII!
    Key   key{runNum, seqNum};

    if (frameReturned && key < lastOldestKey)
        return; // Frame arrived too late
    if (!indexes.insert({key, nextIndex}).second)
        return; // Frame already added

    slots.emplace(nextIndex, Slot{data, numBytes});
    ++nextIndex;
    cond.notify_one();
}

void CircFrameBuf::getOldestFrame(
        unsigned*    runNum,
        unsigned*    seqNum,
        const char** data,
        unsigned*    numBytes)
{
    Lock  lock{mutex}; /// RAII!
    cond.wait(lock, [&]{return !indexes.empty();});
    // A frame exists

    auto  head = indexes.begin();
    auto  key = head->first;
    cond.wait_for(lock, timeout, [=]{
        return frameReturned && key.isNextAfter(lastOldestKey);});
    // The next frame must be returned

    auto  index = head->second;
    auto& slot = slots.at(index);

    *runNum   = key.runNum;
    *seqNum   = key.seqNum;
    *data     = slot.data;
    *numBytes = slot.numBytes;

    slots.erase(index);
    indexes.erase(head);

    lastOldestKey = key;
    frameReturned = true;
}

extern "C" {

void* cfb_new(
        unsigned     numFrames,
        const double timeout) {
    void* cfb = nullptr;
    try {
        cfb = new CircFrameBuf(numFrames, timeout);
    }
    catch (const std::exception& ex) {
        log_add("Couldn't allocate new circular frame buffer: %s", ex.what());
    }
    return cfb;
}

void cfb_add(
        void*          cfb,
        const unsigned runNum,
        const unsigned seqNum,
        const char*    data,
        const unsigned numBytes) {
    try {
        static_cast<CircFrameBuf*>(cfb)->add(runNum, seqNum, data, numBytes);
    }
    catch (const std::exception& ex) {
        log_add("Couldn't add new frame: %s", ex.what());
    }
}

void cfb_getOldestFrame(
        void*        cfb,
        unsigned*    runNum,
        unsigned*    seqNum,
        const char** data,
        unsigned*    numBytes) {
    try {
        static_cast<CircFrameBuf*>(cfb)->getOldestFrame(runNum, seqNum, data,
                numBytes);
    }
    catch (const std::exception& ex) {
        log_add("Couldn't get oldest frame: %s", ex.what());
    }
}

void cfb_delete(void* cfb) {
    delete static_cast<CircFrameBuf*>(cfb);
}

}
