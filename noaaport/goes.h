#ifndef __goes__
#define __goes__

#include <sys/types.h>
#include <zlib.h> /* Required for compress/uncompress */

extern int inflateFrame;
extern int fillScanlines;

#define YES 1
#define NO 0

/**
 * Returns the status of a frame decommpression
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @param[in]  inBuf    Pointer to the frame buffer
 * @param[in]  inLen    Length of the compressed data
 * @param[out] outBuf   Pointer to uncompressed frame data buffer
 * @param[in]  outLen   Length of uncompressed frame
 * @param[in]  blk      Block position
 * @retval  0           The frame was successfully uncompressed
 * @retval -1           Failed to uncompress
 */
static int inflateData(
    const char* const inBuf,
    unsigned long     inLen,
    const    char*    outBuf,
    unsigned long*    outLen,
    unsigned int      blk);

/**
 * Returns the status of a frame compression
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @param[in]  inBuf    Pointer to the uncompressed frame buffer
 * @param[in]  inLen    Length of the uncompressed data
 * @param[out] outBuf   Pointer to compressed frame data buffer
 * @param[out] outLen   Length of compressed frame
 * @param[in]  blk      Block position
 * @retval  0           The frame was successfully compressed.
 * @retval -1           Failed to compress.
 */
static int deflateData(
    const char* const inBuf,
    unsigned long     inLen,
    const    char*    outBuf,
    unsigned long*    outLen,
    unsigned int      blk);

#endif /* __goes__ */
