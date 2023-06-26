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

#include <climits>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdint.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>

using FhSrc    = unsigned;
using UplinkId = unsigned;
static constexpr UplinkId UPLINK_ID_MAX = UINT_MAX;

/// Factory for creating uplink IDs
class UplinkIdFactory
{
    /// Number of different sources this algorithm can handle at one time (e.g., primary, backup)
    static constexpr unsigned NUM_SOURCES = 2;
    /**
     * Number of seconds for `deleteEarlier()` to wait before deleting. The amount of time
     * should be less than the minimum amount of time between changes to the uplink site but
     * greater than the maximum latency from a receiving site to this host.
     */
    static constexpr unsigned UPLINK_ID_TIMEOUT     = 300;
    using Mutex     = std::mutex;
    using Guard     = std::lock_guard<Mutex>;
    using UplinkIds = std::unordered_map<FhSrc, UplinkId>;

    Mutex     mutex;        ///< To protect state during concurrent access
    UplinkId  nextUplinkId; ///< Next uplink ID to be used when a switch occurs
    UplinkIds uplinkIds;    ///< Map from frame-header source to uplink ID

    /**
     * Waits and then deletes uplink entries that were created earlier than a given uplink ID.
     * @param[in] uplinkId  The uplink ID for which earlier ones will be deleted after the timeout
     *                      has passed
     */
    void eraseEarlier(const UplinkId uplinkId) {
        ::sleep(UPLINK_ID_TIMEOUT);
        Guard     guard{mutex};
        UplinkIds newUplinkIds(uplinkIds);
        for (const auto& pair : uplinkIds)
            if (uplinkId - pair.second > UPLINK_ID_MAX/2)
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
    UplinkId getUplinkId(const FhSrc fhSrc) {
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
            if (uplinkIds.size() > 1)
                std::thread(&UplinkIdFactory::eraseEarlier, this, nextUplinkId).detach();
            uplinkId = nextUplinkId++;
        }

        return uplinkId;
    }
};

/**
 * Key for sorting NOAAPort frames and revealing them when their time comes
 */
struct Key
{
    using Clock = std::chrono::steady_clock;
    using Dur   = std::chrono::milliseconds;

    UplinkId          uplinkId;
    unsigned          fhSource;
    unsigned          fhSeqno;
    unsigned          fhRunno;
    unsigned          pdhSeqNum;
    unsigned          pdhBlkNum;
    Clock::time_point revealTime; ///< When the associated frame should be processed

    Key(const NbsFH& fh, const NbsPDH& pdh, Dur& timeout, UplinkId uplinkId)
        : uplinkId(uplinkId)
        , fhSource(fh.source)
        , fhSeqno(fh.seqno)
        , fhRunno(fh.runno)
        , pdhSeqNum(pdh.prodSeqNum)
        , pdhBlkNum(pdh.blockNum)
        , revealTime(Clock::now() + timeout)
    {}

    Key(const NbsFH& fh, const NbsPDH& pdh, Dur&& timeout, UplinkId uplinkId)
        : uplinkId(uplinkId)
        , fhSource(fh.source)
        , fhSeqno(fh.seqno)
        , fhRunno(fh.runno)
        , pdhSeqNum(pdh.prodSeqNum)
        , pdhBlkNum(pdh.blockNum)
        , revealTime(Clock::now() + timeout)
    {}

    Key()
        : uplinkId(0)
        , fhSource(0)
        , fhSeqno(0)
        , fhRunno(0)
        , pdhSeqNum(0)
        , pdhBlkNum(0)
        , revealTime(Clock::now())
    {}

    std::string to_string() const {
        return "{upId=" + std::to_string(uplinkId) +
                ", fhSrc=" + std::to_string(fhSource) +
                ", fhRun=" + std::to_string(fhRunno) +
                ", fhSeq=" + std::to_string(fhSeqno) +
                ", pdhSeq=" + std::to_string(pdhSeqNum) +
                ", pdhBlk=" + std::to_string(pdhBlkNum) + "}";
    }

    /**
     * Indicates if this instance is considered less than another.
     *
     * Unfortunately, there's no way to simply and reliably determine the temporal ordering of
     * two arbitrary frames -- so the uplink ID, product sequence number, and product block
     * number are used as a heuristic. This can give incorrect results during a transition to a
     * new uplink site if the duration of the uplink gap is less than the latency from a
     * receiving host to this host.
     *
     * @param[in] rhs    The other instance
     * @retval    true   This instance is considered less than the other
     * @retval    false  This instance is not considered less than the other
     * @see operator==()
     * @see struct Hash
     */
    bool operator<(const Key& rhs) const noexcept {
        if (rhs.uplinkId - uplinkId < UPLINK_ID_MAX/2)
            return true;
        if (rhs.uplinkId == uplinkId) {
            if (rhs.pdhSeqNum - pdhSeqNum < SEQ_NUM_MAX/2)
                return true;
            if (rhs.pdhSeqNum == pdhSeqNum) {
                if (pdhBlkNum < rhs.pdhBlkNum)
                    return true;
            }
        }
        return false;
    }

    /**
     * Indicates if this instance is considered equal to another.
     * @param[in] rhs    The other instance
     * @retval    true   This instance is considered less than the other
     * @retval    false  This instance is not considered less than the other
     * @see operator<()
     * @see struct Hash
     */
    bool operator==(const Key& rhs) const noexcept {
        return uplinkId == rhs.uplinkId &&
                pdhSeqNum == rhs.pdhSeqNum &&
                pdhBlkNum == rhs.pdhBlkNum;
    }

    /**
     * Returns the hash value of this instance.
     * @return The hash value of this instance
     * @see operator<()
     * @see operator==()
     */
    struct Hash {
        size_t operator()(const Key& key) const noexcept {
            static auto myHash = std::hash<uint32_t>{};
            return myHash(key.uplinkId) ^ myHash(key.pdhSeqNum) ^ myHash(key.pdhBlkNum);
        }
    };
};

/**
 * A NOAAPort frame in the queue.
 */
struct Qframe
{
    char        data[SBN_FRAME_SIZE]; ///< Frame data
    FrameSize_t numBytes;             ///< Number of bytes of data in the frame

    Qframe(const char* data, FrameSize_t numBytes)
        : data()
        , numBytes(numBytes)
    {
        if (numBytes > sizeof(this->data))
            throw std::runtime_error("Frame is too large: " + std::to_string(numBytes) +
                    " bytes.");
        ::memcpy(this->data, data, numBytes);
    }
};

/// An implementation of a NOAAPort circular frame buffer.
class CircFrameBufImpl : public CircFrameBuf
{
    using Mutex      = std::mutex;
    using Cond       = std::condition_variable;
    using Guard      = std::lock_guard<Mutex>;
    using Lock       = std::unique_lock<Mutex>;
    using Index      = unsigned;
    using Qframes    = std::unordered_map<Key, Qframe, Key::Hash>;
    using RevealPred = std::function<bool()>;

    mutable Mutex   mutex;           ///< Supports thread safety
    mutable Cond    cond;            ///< Supports concurrent access
    Qframes         qFrames;         ///< Queue of frames
    Key             lastOutputKey;   ///< Key of last, returned frame
    bool            frameReturned;   ///< Oldest frame returned?
    Key::Dur        timeout;         ///< Timeout for unconditionally returning next frame
    UplinkIdFactory uplinkIdFactory; ///< Factory for creating uplink IDs
    RevealPred      revealPred;      ///< Predicate for revealing a frame

public:
    CircFrameBufImpl(const double timeout)
        : mutex()
        , cond()
        , qFrames()
        , lastOutputKey()
        , frameReturned(false)
        , timeout(std::chrono::duration_cast<Key::Dur>(std::chrono::duration<double>(timeout)))
        , uplinkIdFactory()
        , revealPred([&]{return qFrames.begin()->first.revealTime <= Key::Clock::now();})
    {}

    CircFrameBufImpl(const CircFrameBuf& other) =delete;
    CircFrameBufImpl& operator=(const CircFrameBuf& rhs) =delete;

    int tryAddFrame(
            const char*       serverId,
            const NbsFH&      fh,
            const NbsPDH&     pdh,
            const char*       data,
            const FrameSize_t numBytes) override {
        Guard guard{mutex}; // RAII!
        Key   key{fh, pdh, timeout, uplinkIdFactory.getUplinkId(fh.source)};
        int   status;

        if (frameReturned && ((key < lastOutputKey) || key == lastOutputKey)) {
            status = 1; // Frame arrived too late
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

    void getOldestFrame(Frame_t* frame) override {
        Lock  lock{mutex}; // RAII!

        // Wait until there's a frame in the queue
        if (qFrames.empty())
            cond.wait(lock, [&]{return !qFrames.empty();});
        // and the first frame should be revealed
        cond.wait_until(lock, qFrames.begin()->first.revealTime, revealPred);

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
};

CircFrameBuf* CircFrameBuf::create(const double timeout)
{
    return new CircFrameBufImpl(timeout);
}

extern "C" {

	//------------------------- C code ----------------------------------
	void* cfb_new(const double timeout) {
		void* cfb = nullptr;
		try {
			cfb = CircFrameBuf::create(timeout);
		}
		catch (const std::exception& ex) {
			log_add("Couldn't allocate new circular frame buffer: %s", ex.what());
		}
		return cfb;
	}

	//------------------------- C code ----------------------------------
	int cfb_add(
			void*             cfb,
			const char*       serverId,
			const NbsFH*      fh,
			const NbsPDH*     pdh,
			const char*       data,
			const FrameSize_t numBytes) {
        int status;
		try {
			status = static_cast<CircFrameBuf*>(cfb)->tryAddFrame(serverId, *fh, *pdh, data, numBytes);
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
