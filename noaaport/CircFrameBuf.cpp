/*
 * CirFramecBuf.cpp
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#include "config.h"

#include <stdint.h>
#include "CircFrameBuf.h"
#include "log.h"

CircFrameBuf::CircFrameBuf(const double timeout)
    : mutex()
    , cond()
    , nextIndex(0)
    , indexes()
    , slots()
    , lastOldestKey()
    , frameReturned(false)
    , timeout(std::chrono::duration_cast<Slot::Dur>(
            std::chrono::duration<double>(timeout)))
{}

int CircFrameBuf::add(
<<<<<<< HEAD
        const unsigned    seqNum,
        const unsigned    blkNum,
=======
        const SeqNum_t    seqNum,
        const BlkNum_t    blkNum,
>>>>>>> branch 'find_next_frame' of git@github.com:Unidata/LDM.git
        const char*       data,
        const FrameSize_t numBytes)
{
    Guard guard{mutex}; /// RAII!
    Key   key{seqNum, blkNum};

    if (frameReturned && key < lastOldestKey)
        return 1; // Frame arrived too late
    if (!indexes.insert({key, nextIndex}).second)
        return 2; // Frame already added

    slots.emplace(nextIndex, Slot{data, numBytes, timeout});
    ++nextIndex;
    cond.notify_one();
    return 0;
}

void CircFrameBuf::getOldestFrame(Frame_t* frame)
{
    Lock  lock{mutex}; /// RAII!

    /*
    do {
        cond.wait_for(lock, timeout,
                [&]{return !indexes.empty() && Slot::Clock::now() >=
                slots.at(indexes.begin()->second).inserted + timeout;});
    } while (indexes.empty());
    */

    cond.wait(lock, [&]{return !indexes.empty() &&
            Slot::Clock::now() >= slots.at(indexes.begin()->second).revealTime;});

    // The oldest frame shall be returned
    auto  head = indexes.begin();
    auto  key = head->first;
    auto  index = head->second;
    auto& slot = slots.at(index);

    frame->dataBlockNum   = key.blkNum;
    frame->prodSeqNum   = key.seqNum;
    ::memcpy(frame->data, slot.data, slot.numBytes);
    frame->nbytes = slot.numBytes;

    slots.erase(index);
    indexes.erase(head);

    lastOldestKey = key;
    frameReturned = true;
}

extern "C" {

	//------------------------- C code ----------------------------------
	void* cfb_new(const double timeout) {
		void* cfb = nullptr;
		try {
			cfb = new CircFrameBuf(timeout);
		}
		catch (const std::exception& ex) {
			log_add("Couldn't allocate new circular frame buffer: %s", ex.what());
		}
		return cfb;
	}

	//------------------------- C code ----------------------------------
	/**
	 * Inserts a data-transfer frame into the circular frame buffer.
	 *
	 * @param[in] cfb         Circular frame buffer
	 * @param[in] seqNum      PDH product sequence number
	 * @param[in] blkNum      PDH data-block number
	 * @param[in] data        NOAAPort frame
	 * @param[in] numBytes    Size of NOAAPort frame in bytes
	 * @retval 0   Success
	 * @retval 1   Frame is too late
	 * @retval 2   Frame is duplicate
	 * @retval -1  System error. `log_add()` called.
	 */
	int cfb_add(
			void*             cfb,
<<<<<<< HEAD
			const unsigned    seqNum,
			const unsigned    blkNum,
=======
			const SeqNum_t    seqNum,
			const BlkNum_t    blkNum,
>>>>>>> branch 'find_next_frame' of git@github.com:Unidata/LDM.git
			const char*       data,
			const FrameSize_t numBytes) {
        int status;
		try {
			status = static_cast<CircFrameBuf*>(cfb)->add(seqNum, blkNum, data, numBytes);
		}
		catch (const std::exception& ex) {
			log_add("Couldn't add new frame: %s", ex.what());
			status = -1;
		}
		return status;
	}

	//------------------------- C code ----------------------------------
	bool cfb_getOldestFrame(
			void*        cfb,
			Frame_t*     frame) {
		bool success = false;
		try {
			static_cast<CircFrameBuf*>(cfb)->getOldestFrame(frame);
			success = true;
		}
		catch (const std::exception& ex) {
			log_add("Couldn't get oldest frame: %s", ex.what());
		}
		return success;
	}

	//------------------------- C code ----------------------------------
	void cfb_delete(void* cfb) {
		delete static_cast<CircFrameBuf*>(cfb);
	}

}
