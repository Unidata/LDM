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
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>

using FhSrc    = unsigned;

class UplinkIdFactory
{
    // Number of different sources this algorithm can handle at one time (e.g., primary, backup)
    static constexpr unsigned NUM_SOURCES = 2;
    /// Number of seconds for `deleteEarlier()` to wait before executing
    static constexpr unsigned TIMEOUT     = 300;

    using Mutex     = std::mutex;
    using Guard     = std::lock_guard<Mutex>;
    using UplinkIds = std::unordered_map<FhSrc, UplinkId>;

    Mutex     mutex;        ///< To protect state during concurrent access
    UplinkId  nextUplinkId; ///< Next uplink ID to be used when a switch occurs
    UplinkIds uplinkIds;    ///< Map from frame-header source to uplink ID

    /**
     * Deletes uplink entries that were created earlier than a given uplink ID after a certain
     * amount of time has passed.
     * @param[in] uplinkId  The uplink ID for which earlier ones will be deleted after the timeout
     *                      has passed
     */
    void deleteEarlier(const UplinkId uplinkId) {
        ::sleep(300);
        Guard     guard{mutex};
        UplinkIds newUplinkIds(uplinkIds);
        for (const auto& pair : uplinkIds)
            if (uplinkId - pair.second > std::numeric_limits<UplinkId>::max()/2)
                newUplinkIds.erase(pair.first);
        uplinkIds.swap(newUplinkIds);
    }

public:
    UplinkIdFactory()
        : mutex()
        , nextUplinkId(0)
        , uplinkIds(NUM_SOURCES)
    {}

    /**
     * Returns the uplink ID corresponding to a frame-header's source field.
     * @param[in] fhSrc  The frame-header's source field
     */
    unsigned getUplinkId(const FhSrc fhSrc) {
        if (fhSrc > UINT8_MAX)
            throw std::logic_error("Invalid frame-header source: " + std::to_string(fhSrc));

        UplinkId uplinkId;
        Guard    guard{mutex};

        const auto pair = uplinkIds.insert({fhSrc, nextUplinkId});
        if (!pair.second) {
            // Wasn't inserted => known source
            uplinkId = pair.first->second;
        }
        else {
            // Was inserted => new source
            std::thread(&UplinkIdFactory::deleteEarlier, nextUplinkId).detach();
            uplinkId = nextUplinkId++;
        }

        return uplinkId;
    }
};

std::string CircFrameBuf::Key::to_string() const {
    return "{upId=" + std::to_string(uplinkId) +
            ", fhSrc=" + std::to_string(fhSource) +
            ", fhRun=" + std::to_string(fhRunno) +
            ", fhSeq=" + std::to_string(fhSeqno) +
            ", pdhSeq=" + std::to_string(pdhSeqNum) +
            ", pdhBlk=" + std::to_string(pdhBlkNum) + "}";
}

bool CircFrameBuf::Key::operator<(const Key& rhs) const noexcept {
    if (fhSource < rhs.fhSource)
        return true;
    if (rhs.fhSource == fhSource) {
        if (rhs.pdhSeqNum - pdhSeqNum < SEQ_NUM_MAX/2)
            return true;
        if (rhs.pdhSeqNum == pdhSeqNum) {
            if (pdhBlkNum < rhs.pdhBlkNum)
                return true;
        }
    }
    return false;
}

bool CircFrameBuf::Key::operator==(const Key& rhs) const noexcept {
    return fhSource == rhs.fhSource &&
            pdhSeqNum == rhs.pdhSeqNum &&
            pdhBlkNum == rhs.pdhBlkNum;
}

CircFrameBuf::CircFrameBuf(const double timeout)
    : mutex()
    , cond()
    , qFrames()
    , lastOutputKey()
    , frameReturned(false)
    , timeout(std::chrono::duration_cast<Key::Dur>(
            std::chrono::duration<double>(timeout)))
{}

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
    Guard guard{mutex}; // RAII!
    Key   key{fh, pdh, timeout};
    int   status;

    if (frameReturned && ((key < lastOutputKey) || key == lastOutputKey)) {
        status = 1; // Frame is too late
    }
    else {
        if (!qFrames.emplace(std::piecewise_construct,
                std::forward_as_tuple(key), std::forward_as_tuple(data, numBytes)).second) {
            status = 2; // Frame is a duplicate
        }
        else {
            cond.notify_one();
            status = 0;
        }
    }

    return status;
}

void CircFrameBuf::getOldestFrame(Frame_t* frame)
{
    Lock  lock{mutex}; // RAII!

    // Wait until there's a frame in the queue
    if (qFrames.empty())
        cond.wait(lock, [&]{return !qFrames.empty();});
    /*
     * and the first frame should be revealed
     */
    auto pred = [&]{return qFrames.begin()->first.revealTime <= Key::Clock::now();};
    cond.wait_until(lock, qFrames.begin()->first.revealTime, pred);

    // The first frame in the queue shall be returned
    auto  iter = qFrames.begin();
    auto& key = iter->first;
    auto& qFrame = iter->second;

    frame->prodSeqNum   = key.pdhSeqNum;
    frame->dataBlockNum = key.pdhBlkNum;
    ::memcpy(frame->data, qFrame.data, qFrame.numBytes);
    frame->nbytes = qFrame.numBytes;

    lastOutputKey = key;
    frameReturned = true;

    qFrames.erase(iter);
}

extern "C" {

	//------------------------- C code ----------------------------------
	void* cfb_new(const double   timeout) {
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
     * @param[in] serverId    Hostname or IP address and port number of streaming NOAAPort server
	 * @param[in] fh          Frame-level header
	 * @param[in] pdh         Product-description header
	 * @param[in] data        NOAAPort frame
	 * @param[in] numBytes    Size of NOAAPort frame in bytes
	 * @retval  0  Success
	 * @retval  1  Frame is too late. `log_add()` called.
	 * @retval  2  Frame is duplicate
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
