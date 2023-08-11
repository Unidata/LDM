/**
 * FramecQueue.cpp
 *
 *  Created on: Jan 10, 2022
 *      Author: Mustapha Iles
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "FrameQueue.h"
#include "log.h"
#include "NbsHeaders.h"

#include <cstdint>
#include <unordered_map>

using FhSrc    = unsigned;
using UplinkId = uint32_t;

UplinkId getUplinkId(const FhSrc fhSrc)
{
    static bool     initialized = false;
    static UplinkId uplinkId = 0;
    static FhSrc    saveFhSrc;

    if (!initialized) {
        initialized = true;
        saveFhSrc = fhSrc;
    }
    else if (saveFhSrc != fhSrc) {
        saveFhSrc = fhSrc;
        ++uplinkId;
    }

    return uplinkId;
}

FrameQueue::FrameQueue(const double timeout)
    : mutex()
    , cond()
    , frames()
    , lastOutputKey()
    , frameReturned(false)
    , timeout(std::chrono::duration_cast<Key::Dur>(std::chrono::duration<double>(timeout)))
{}

int FrameQueue::add(
        const NbsFH&      fh,
        const NbsPDH&     pdh,
        const char*       data,
        const FrameSize_t numBytes)
{
    Guard guard{mutex}; /// RAII!
    Key   key{fh, pdh, timeout};

    if (frameReturned && key < lastOutputKey) {
        //log_add("Frame arrived too late: lastOutputKey=%s, lateKey=%s. Increase delay (-t)?",
                //lastOutputKey.to_string().data(), key.to_string().data());
        return 1; // Frame arrived too late
    }

    // Avoid unnecessary frame construction
    if (frames.count(key))
        return 2; // Frame already added

    frames.emplace(key, Frame{data, numBytes});
    cond.notify_one();
    return 0;
}

void FrameQueue::getOldestFrame(Frame_t* frame)
{
    Lock  lock{mutex}; /// RAII!

    // Wait until the queue is not empty
    if (frames.empty())
        cond.wait(lock, [&]{return !frames.empty();});
    /*
     * and the earliest reveal-time has expired.
     */
    auto pred = [&]{return frames.begin()->first.revealTime <= Key::Clock::now();};
    cond.wait_until(lock, frames.begin()->first.revealTime, pred);

    // The earliest frame shall be returned
    auto  head = frames.begin();
    auto& key = head->first; // NB: reference
    auto& slot = head->second;

    frame->prodSeqNum   = key.pdhSeqNum;
    frame->dataBlockNum = key.pdhBlkNum;
    ::memcpy(frame->data, slot.bytes, slot.numBytes);
    frame->nbytes = slot.numBytes;

    lastOutputKey = key; // Must come before `frames.erase(head)` because `key` is a reference
    frameReturned = true;

    frames.erase(head);
}

extern "C" {
	void* fq_new(const double timeout) {
		void* fq = nullptr;
		try {
			fq = new FrameQueue(timeout);
		}
		catch (const std::exception& ex) {
			log_add("Couldn't allocate new frame queue: %s", ex.what());
		}
		return fq;
	}

	int fq_add(
			void*             fq,
			const NbsFH*      fh,
			const NbsPDH*     pdh,
			const char*       data,
			const FrameSize_t numBytes) {
        int status;
		try {
			status = static_cast<FrameQueue*>(fq)->add(*fh, *pdh, data, numBytes);
		}
		catch (const std::exception& ex) {
			log_add("Couldn't add new frame to buffer: %s", ex.what());
			status = -1;
		}
		return status;
	}

	bool fq_getOldestFrame(
			void*        fq,
			Frame_t*     frame) {
		bool success = false;
		try {
			static_cast<FrameQueue*>(fq)->getOldestFrame(frame);
			success = true;
		}
		catch (const std::exception& ex) {
			log_add("Couldn't get oldest frame: %s", ex.what());
		}
		return success;
	}

	void fq_delete(void* fq) {
		delete static_cast<FrameQueue*>(fq);
	}
}
