/*
 * frameCircBufImpl.h
 *
 *  Created on: Jan 10, 2022
 *      Author: miles
 */

#ifndef NOAAPORT_CIRCFRAMEBUF_H_
#define NOAAPORT_CIRCFRAMEBUF_H_

#include "NbsHeaders.h"
#include "noaaportFrame.h"

#ifdef __cplusplus

/// Interface for a NOAAPort circular frame buffer
class CircFrameBuf
{
public:
    /**
     * Creates an instance
     *
     * @param[in] timeout  Timeout value, in seconds, for returning oldest frame
     * @see                `getOldestFrame()`
     */
    static CircFrameBuf* create(const double timeout);

    /**
     * Destroys.
     */
    virtual ~CircFrameBuf() =default;

    /**
     * Trys to add a frame. The frame will not be added if
     *   - It is an earlier frame than the last, returned frame
     *   - The frame was already added
     *
     * @param[in] serverId      Hostname or IP address and port number of streaming NOAAPort server
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
            const char*       serverId,
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
     * @see                  `CircFrameBuf()`
     */
    virtual void getOldestFrame(Frame_t* frame) =0;
};

extern "C" {

#endif // __cplusplus

/**
 * Returns a new circular frame buffer.
 *
 * @param[in] timeout      Timeout, in seconds, before the next frame must be returned if it exists
 * @retval    NULL         Fatal error. `log_add()` called.
 * @return                 Pointer to a new circular frame buffer
 * @see                    `cfb_getOldestFrame()`
 * @see                    `cfb_delete()`
 */
void* cfb_new(const double timeout);

/**
 * Adds a new frame.
 *
 * @param[in] cfb           Pointer to circular frame buffer
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
int cfb_add(
        void*             cfb,
        const char*       serverId,
        const NbsFH*      fh,
        const NbsPDH*     pdh,
        const char*       data,
        const FrameSize_t numBytes);

/**
 * Returns the next, oldest frame if it exists. Blocks until it does.
 *
 * @param[in]  cfb       Pointer to circular frame buffer
 * @param[out] frame     Buffer to hold the oldest frame
 * @retval    `false`    Fatal error. `log_add()` called.
 */
bool cfb_getOldestFrame(
        void*        cfb,
        Frame_t*     frame);

/**
 * Deletes a circular frame buffer.
 *
 * @param[in]  cfb       Pointer to circular frame buffer
 */
void cfb_delete(void* cfb);

/**
 * Return number of frames in a circular frame buffer.
 *
 * @param[in]  cfb       Pointer to circular frame buffer
 * @param[out] nbf       Number of frames
 */
void cfb_getNumberOfFrames(void* cfb, unsigned* nbf);


#ifdef __cplusplus
}
#endif

#endif /* NOAAPORT_CIRCFRAMEBUF_H_ */
