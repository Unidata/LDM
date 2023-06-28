/*
 * FrameQueue.cpp
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#include "FrameQueue.h"

#include "config.h"

#include "log.h"
#include "NbsHeaders.h"

#include <climits>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <set>
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
     * greater than the maximum latency from a receiving site to this host (taking into account
     * congestion and TCP retransmissions).
     *
     * According to a NOAA affiliate on the NOAAPort team, Sathya Sankarasubbu, any uplink site
     * will be active for much longer than this time interval:
     *
     *     During planned maintenance, we swap SBN operations at MGS from primary to backup and stay
     *     there depending on the length of the maintenance window. It can vary from ~24 hrs to a
     *     few days. In case of emergency MGS issues, we will keep the SBN active at backup MGS till
     *     the issue is resolved at the primary site.
     *
     *     When we do a full switch of operations (moving from ANCF to BNCF and vice versa), usually
     *     the backup will be operational for 1-2 weeks. As part of monthly security patching, we
     *     will do a full switch once every month and stay for 1 week.
     *
     * --Steven Emmerson 2023-06-28
     */
    static constexpr unsigned UPLINK_ID_TIMEOUT = 15*60; ///< 15 minutes
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
class Key
{
private:
    /**
     * Function for pre-computing the hash code
     * @param[in] uplinkId   Uplink ID
     * @param[in] pdhSeqNum  Product definition header sequence number
     * @param[in] pdhBlkNum  Product definition header block number
     */
    inline static size_t hash(
        UplinkId          uplinkId,
        unsigned          pdhSeqNum,
        unsigned          pdhBlkNum) {
            static auto myHash = std::hash<uint32_t>{};
            return myHash(uplinkId) ^ myHash(pdhSeqNum) ^ myHash(pdhBlkNum);
    }

public:
    using Clock = std::chrono::steady_clock;
    using Dur   = std::chrono::milliseconds;

    UplinkId          uplinkId;
    unsigned          fhSource;
    unsigned          fhSeqno;
    unsigned          fhRunno;
    unsigned          pdhSeqNum;
    unsigned          pdhBlkNum;
    Clock::time_point revealTime; ///< When the associated frame should be processed
    size_t            hashCode;   ///< Hash code. Pre-computed because it's used a lot.
    /**
     * Default constructs.
     */
    Key()
        : uplinkId(0)
        , fhSource(0)
        , fhSeqno(0)
        , fhRunno(0)
        , pdhSeqNum(0)
        , pdhBlkNum(0)
        , revealTime(Clock::now())
        , hashCode(0)
    {}

    /**
     * Constructs.
     * @param[in] fh        Frame-level header
     * @param[in] pdh       Product definition header
     * @param[in] timeout   Time, in seconds, to wait before revealing a frame
     * @param[in] uplinkId  Uplink ID
     */
    Key(const NbsFH& fh, const NbsPDH& pdh, Dur& timeout, UplinkId uplinkId)
        : uplinkId(uplinkId)
        , fhSource(fh.source)
        , fhSeqno(fh.seqno)
        , fhRunno(fh.runno)
        , pdhSeqNum(pdh.prodSeqNum)
        , pdhBlkNum(pdh.blockNum)
        , revealTime(Clock::now() + timeout)
        , hashCode(hash(uplinkId, pdhSeqNum, pdhBlkNum))
    {}

    /**
     * Returns a string representation.
     * @return A string representation
     */
    std::string to_string() const {
        return "{upId=" + std::to_string(uplinkId) +
                ", fhSrc=" + std::to_string(fhSource) +
                ", fhRun=" + std::to_string(fhRunno) +
                ", fhSeq=" + std::to_string(fhSeqno) +
                ", pdhSeq=" + std::to_string(pdhSeqNum) +
                ", pdhBlk=" + std::to_string(pdhBlkNum) + "}";
    }

    /**
     * Copy assigns.
     * @param[in] rhs  Right hand side
     * @return         Reference to this instance
     */
    Key& operator=(const Key& rhs) {
        uplinkId = rhs.uplinkId;
        fhSource = rhs.fhSource;
        fhSeqno = rhs.fhSeqno;
        fhRunno = rhs.fhRunno;
        pdhSeqNum = rhs.pdhSeqNum;
        pdhBlkNum = rhs.pdhBlkNum;
        revealTime = rhs.revealTime;
        hashCode = rhs.hashCode;
        return *this;
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
     * @see std::hash<Key>()
     * @see UPLINK_ID_TIMEOUT
     */
    bool operator<(const Key& rhs) const noexcept {
        if (uplinkId - rhs.uplinkId > UPLINK_ID_MAX/2)
            return true;
        if (uplinkId == rhs.uplinkId) {
            if (pdhSeqNum - rhs.pdhSeqNum > SEQ_NUM_MAX/2)
                return true;
            if (pdhSeqNum == rhs.pdhSeqNum) {
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
     * @see std::hash<::Key>()
     */
    bool operator==(const Key& rhs) const noexcept {
        return uplinkId == rhs.uplinkId &&
                pdhSeqNum == rhs.pdhSeqNum &&
                pdhBlkNum == rhs.pdhBlkNum;
    }
};

/**
 * Hash function for the Key class for unordered sets and maps.
 * @see Key::operator<()
 * @see Key::operator==()
 */
template<> struct std::hash<Key> {
    size_t operator()(const Key& key) const {
        return key.hashCode;
    }
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

/// An implementation of a thread-safe queue of NOAAPort frames in temporal order
class FrameQueueImpl : public FrameQueue
{
    using Mutex      = std::mutex;
    using Cond       = std::condition_variable;
    using Guard      = std::lock_guard<Mutex>;
    using Lock       = std::unique_lock<Mutex>;
    using Index      = unsigned;
    using Frames     = std::unordered_map<Key, Qframe>;
    using KeyQueue   = std::set<Key>;
    using RevealPred = std::function<bool()>;

    mutable Mutex   mutex;           ///< Supports thread safety
    mutable Cond    cond;            ///< Supports concurrent access
    Frames          qFrames;         ///< NOAAPort frames in the queue
    KeyQueue        keys;            ///< Queue of keys
    Key             lastOutputKey;   ///< Key of last, returned frame
    bool            frameReturned;   ///< Oldest frame returned?
    Key::Dur        timeout;         ///< Timeout for unconditionally returning next frame
    UplinkIdFactory uplinkIdFactory; ///< Factory for creating uplink IDs
    RevealPred      revealPred;      ///< Predicate for revealing a frame

public:
    /**
     * Constructs.
     * @param[in] timeout  Duration, in seconds, over which to accumulate frames
     */
    FrameQueueImpl(const double timeout)
        : mutex()
        , cond()
        , qFrames()
        , keys()
        , lastOutputKey()
        , frameReturned(false)
        , timeout(std::chrono::duration_cast<Key::Dur>(std::chrono::duration<double>(timeout)))
        , uplinkIdFactory()
        , revealPred([&]{return keys.begin()->revealTime <= Key::Clock::now();})
    {}

    FrameQueueImpl(const FrameQueue& other) =delete;
    FrameQueueImpl& operator=(const FrameQueue& rhs) =delete;

    int tryAddFrame(
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
                keys.insert(key);
                cond.notify_one();
                status = 0;
            }
        }

        return status;
    }

    void getOldestFrame(Frame_t* frame) override {
        Lock  lock{mutex}; // RAII!

        // Wait until there's a frame in the queue
        if (keys.empty())
            cond.wait(lock, [&]{return !keys.empty();});
        // and the first frame in the queue should be revealed
        cond.wait_until(lock, keys.begin()->revealTime, revealPred);

        // The first frame in the queue shall be returned
        auto  iter = keys.begin();
        auto  key = *iter;
        auto& qFrame = qFrames.at(key);
        keys.erase(iter);

        frame->prodSeqNum   = key.pdhSeqNum;
        frame->dataBlockNum = key.pdhBlkNum;
        ::memcpy(frame->data, qFrame.data, qFrame.numBytes);
        frame->nbytes = qFrame.numBytes;

        lastOutputKey = key;
        frameReturned = true;

        qFrames.erase(key);
    }
};

FrameQueue* FrameQueue::create(const double timeout)
{
    return new FrameQueueImpl(timeout);
}

extern "C" {

	void* fq_new(const double timeout) {
		void* fq = nullptr;
		try {
			fq = FrameQueue::create(timeout);
		}
		catch (const std::exception& ex) {
			log_add("Couldn't allocate new frame queue: %s", ex.what());
		}
		return fq;
	}

	int fq_add(
			void*             fq,
			const char*       serverId,
			const NbsFH*      fh,
			const NbsPDH*     pdh,
			const char*       data,
			const FrameSize_t numBytes) {
        int status;
		try {
			status = static_cast<FrameQueue*>(fq)->tryAddFrame(*fh, *pdh, data, numBytes);
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
