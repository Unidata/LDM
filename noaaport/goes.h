#ifndef __goes__
#define __goes__

#include <sys/types.h>
#include <zlib.h> /* Required for compress/uncompress */

int inflateFrame;
int fillScanlines;

#define YES 1
#define NO 0
int inflateData(
    char*    const    inBuf,                  /**< [in] Pointer to the frame buffer   */
    unsigned long     inLen,                  /**< [in] Length of the compressed data */
    const    char*    outBuf,                 /**< [out] Pointer to uncompressed frame 
                                                *  data buffer  */
    unsigned long*     outLen,                 /**< [out] Length of uncompressed frame */  
    unsigned int       blk);                   /** Block position   **/
int deflateData(
    char*    const    inBuf,                  /**< [in] Pointer to the frame buffer   */
    unsigned long     inLen,                  /**< [in] Length of the uncompressed data */
    const    char*    outBuf,                 /**< [out] Pointer to compressed frame 
                                                *  data buffer  */
    unsigned long*     outLen,                 /**< [out] Length of compressed frame */  
    unsigned int       blk);

#endif /* __goes__ */
