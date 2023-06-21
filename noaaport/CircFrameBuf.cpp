/*
 * CirFramecBuf.cpp
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#include "config.h"

#include "CircFrameBuf.h"
#include "log.h"
#include "NbsHeaders.h"

#include <stdint.h>
#include <unordered_map>

using FhSrc   = unsigned;
using UplinkId = uint32_t;

class StreamId
{
   unsigned fhSource;
   unsigned fhSeqno;
   unsigned fhRunno;
   unsigned pdhSeqNum;

public:
    StreamId(
            const NbsFH&  fh,
            const NbsPDH& pdh)
        : fhSource(fh.source)
        , fhSeqno(fh.seqno)
        , fhRunno(fh.runno)
        , pdhSeqNum(pdh.prodSeqNum)
    {}

    bool operator==(const StreamId& rhs) {
        return fhSource == rhs.fhSource &&
                fhRunno == rhs.fhRunno &&
                fhSeqno - rhs.fhSeqno < SEQ_NUM_MAX/2 &&
                pdhSeqNum - rhs.pdhSeqNum < SEQ_NUM_MAX/2;
    }
};

bool CircFrameBuf::isConsistent(
        const char*   serverId,
        const NbsFH&  fh,
        const NbsPDH& pdh) {
    return false;
}

/**
 * Returns a monotonically increasing uplink identifier. This identifier increments every time the
 * source field in the frame header changes -- even if it reverts to the previous value. This
 * assumes that the delay-time of frames in the buffer is much less than the time between
 * changes to the uplink site so that all frames from the previous change will be gone from the
 * buffer.
 *
 * @param[in] fhSrc  Frame header's source field
 * @return           Monotonically increasing uplink identifier
 */
UplinkId getUplinkId(const FhSrc fhSrc) {
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

bool CircFrameBuf::KeyFactory::getKey(
        const char*                   serverId,
        const NbsFH&                  fh,
        const NbsPDH&                 pdh,
        const CircFrameBuf::Key::Dur& timeout,
        CircFrameBuf::Key&            key) {
}

CircFrameBuf::CircFrameBuf(
        const double   timeout,
        const unsigned maxNumEarly)
    : mutex()
    , cond()
    , nextIndex(0)
    , indexes()
    , slots()
    , lastOutputKey()
    , frameReturned(false)
    , timeout(std::chrono::duration_cast<Key::Dur>(
            std::chrono::duration<double>(timeout)))
    , numEarly(0)
    , maxNumEarly(maxNumEarly)
    , prevEarlyFhSeq(0)
    , newFrameStream(false)
{}

int CircFrameBuf::tryInsertFrame(
        const Key& key,
        const char* data,
        const FrameSize_t numBytes)
{
    if (!indexes.insert({key, nextIndex}).second)
        return 2; // Frame already added

    slots.emplace(nextIndex, Slot{data, numBytes});
    ++nextIndex;
    cond.notify_one();

    return 0;
}

/**
 * Tries to add a frame.
 *
 * @param[in] serverId        Hostname or IP address and port number of streaming NOAAPort server
 * @param[in] fh              Frame's decoded frame-level header
 * @param[in] pdh             Frame's decoded product-definition header
 * @param[in] data            Frame's bytes
 * @param[in] numBytes        Number of bytes in the frame
 * @retval    0               Success. Frame was added.
 * @retval    1               Frame arrived too late. `log_add()` called.
 * @retval    2               Frame is a duplicate
 * @throw std::runtime_error  Frame is too large
 */
int CircFrameBuf::add(
        const char*       serverId,
        const NbsFH&      fh,
        const NbsPDH&     pdh,
        const char*       data,
        const FrameSize_t numBytes)
{
    Guard guard{mutex}; /// RAII!
    Key   key{fh, pdh, timeout};
    int   status = 1; // Frame arrived too late

    if (!(frameReturned && key < lastOutputKey) || !key.isSameSite(lastOutputKey)) {
        // New frame is not early or from a different uplink site
        numEarly = 0;
        newFrameStream = false;
        status = tryInsertFrame(key, data, numBytes);
    }
    else {
        // New frame is early and from the same uplink site
        if (!newFrameStream)
            log_debug("Frame arrived too late: lastOutputKey=%s, lateKey=%s. Increase delay (-t)?",
                    lastOutputKey.to_string().data(), key.to_string().data());

        if (numEarly == 0) {
            prevEarlyFhSeq = key.fhSeqno;
            numEarly = 1;
        }
        else {
            numEarly = (key.fhSeqno == prevEarlyFhSeq + 1)
                    ? ++numEarly
                    : 0;
        }

        if (numEarly >= maxNumEarly) {
            log_warning("Accepting new frame stream because %u \"early\" frames arrived",
                    maxNumEarly);
            newFrameStream = true;
            status = tryInsertFrame(key, data, numBytes);
        }
    }

    return status;
}

void CircFrameBuf::getOldestFrame(Frame_t* frame)
{
    Lock  lock{mutex}; /// RAII!

    // Wait until the queue is not empty
    if (indexes.empty())
        cond.wait(lock, [&]{return !indexes.empty();});
    /*
     * and the earliest reveal-time has expired.
     */
    auto pred = [&]{return indexes.begin()->first.revealTime <= Key::Clock::now();};
    cond.wait_until(lock, indexes.begin()->first.revealTime, pred);

    // The earliest frame shall be returned
    auto  head = indexes.begin();
    auto  key = head->first;
    auto  index = head->second;
    auto& slot = slots.at(index);

    frame->prodSeqNum   = key.pdhSeqNum;
    frame->dataBlockNum = key.pdhBlkNum;
    ::memcpy(frame->data, slot.data, slot.numBytes);
    frame->nbytes = slot.numBytes;

    slots.erase(index);
    indexes.erase(head);

    lastOutputKey = key;
    frameReturned = true;
}

extern "C" {

	//------------------------- C code ----------------------------------
	void* cfb_new(
	        const double   timeout,
            const unsigned maxNumEarly) {
		void* cfb = nullptr;
		try {
			cfb = new CircFrameBuf(timeout, maxNumEarly);
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
     * @param[in] serverId    Hostname or IP address and port number of streaming NOAAPort server
	 * @param[in] fh          Frame-level header
	 * @param[in] pdh         Product-description header
	 * @param[in] data        NOAAPort frame
	 * @param[in] numBytes    Size of NOAAPort frame in bytes
	 * @retval 0   Success
	 * @retval 1   Frame is too late. `log_add()` called.
	 * @retval 2   Frame is duplicate
	 * @retval -1  System error. `log_add()` called.
	 */
	int cfb_add(
			void*             cfb,
			const char*       serverId,
			const NbsFH*      fh,
			const NbsPDH*     pdh,
			const char*       data,
			const FrameSize_t numBytes) {
        int status;
		try {
			status = static_cast<CircFrameBuf*>(cfb)->add(serverId, *fh, *pdh, data, numBytes);
		}
		catch (const std::exception& ex) {
			log_add("Couldn't add new frame to buffer: %s", ex.what());
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
