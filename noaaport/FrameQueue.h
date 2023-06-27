/*
 * FrameQueue.h
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#ifndef NOAAPORT_FRAMEQUEUE_H_
#define NOAAPORT_FRAMEQUEUE_H_

#include "NbsHeaders.h"
#include "noaaportFrame.h"

#ifdef __cplusplus

/// Interface for a thread-safe queue of NOAAPort frames in temporal order
class FrameQueue
{
public:
    /**
     * Creates an instance
     *
     * @param[in] timeout  Timeout value, in seconds, for returning oldest frame
     * @see                `getOldestFrame()`
     */
    static FrameQueue* create(const double timeout);

    /**
     * Destroys.
     */
    virtual ~FrameQueue() =default;

    /**
     * Trys to add a frame. The frame will not be added if
     *   - It is an earlier frame than the last, returned frame
     *   - The frame was already added
     *
     * @param[in] fh            Frame-level header
     * @param[in] pdh           Product-description header
     * @param[in] data          Frame data
     * @param[in] numBytes      Number of bytes in the frame
     * @retval    0             Frame added
     * @retval    1             Frame not added because it arrived too late
     * @retval    2             Frame not added because it's a duplicate
     * @threadsafety            Safe
     * @see                     `getOldestFrame()`
     */
    virtual int tryAddFrame(
            const NbsFH&      fh,
            const NbsPDH&     pdh,
            const char*       data,
            const FrameSize_t numBytes) =0;

    /**
     * Returns the oldest frame. Returns immediately if the next frame is the
     * immediate successor to the previously-returned frame; otherwise, blocks
     * until a frame is available and the timeout occurs.
     *
     * @param[out] frame     Buffer for the frame
     * @threadsafety         Safe
     * @see                  `FrameQueue()`
     */
    virtual void getOldestFrame(Frame_t* frame) =0;
};

extern "C" {

#endif // __cplusplus

/**
 * Returns a new frame queue.
 *
 * @param[in] timeout      Timeout, in seconds, before the next frame must be returned if it exists
 * @retval    NULL         Fatal error. `log_add()` called.
 * @return                 Pointer to a new frame queue
 * @see                    `fq_getOldestFrame()`
 * @see                    `fq_delete()`
 */
void* fq_new(const double timeout);

/**
 * Adds a new frame.
 *
 * @param[in] fq            Pointer to frame queue
 * @param[in] serverId      Hostname or IP address and port number of streaming NOAAPort server
 * @param[in] fh            Frame-level header
 * @param[in] pdh           Product-description header
 * @param[in] prodSeqNum    PDH product sequence number
 * @param[in] dataBlkNum    PDH data-block number
 * @param[in] data          Frame data
 * @param[in] numBytes      Number of bytes of data
 * @retval    0             Success
 * @retval    1             Frame not added because it's too late
 * @retval    2             Frame not added because it's a duplicate
 * @retval    -1            System error. `log_add()` called.
 */
int fq_add(
        void*             fq,
        const char*       serverId,
        const NbsFH*      fh,
        const NbsPDH*     pdh,
        const char*       data,
        const FrameSize_t numBytes);

/**
 * Returns the next, oldest frame if it exists. Blocks until it does.
 *
 * @param[in]  fq        Pointer to frame queue
 * @param[out] frame     Buffer to hold the oldest frame
 * @retval    `false`    Fatal error. `log_add()` called.
 */
bool fq_getOldestFrame(
        void*    fq,
        Frame_t* frame);

/**
 * Deletes a frame queue.
 *
 * @param[in]  fq        Pointer to frame queue
 */
void fq_delete(void* fq);

/**
 * Return number of frames in a frame queue.
 *
 * @param[in]  fq        Pointer to frame queue
 * @param[out] nbf       Number of frames
 */
void fq_getNumberOfFrames(void* fq, unsigned* nbf);


#ifdef __cplusplus
}
#endif

#endif /* NOAAPORT_FRAMEQUEUE_H_ */
