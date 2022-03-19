/**
 * This file declares an API for dumping NOAAPort headers. The headers are
 * frame header, product-definition header, and product-specific header.
 *
 * DumpHeaders.h
 *
 *  Created on: Jan 29, 2022
 *      Author: Steven R. Emmerson
 */

#ifndef NOAAPORT_NBSHEADERS_H_
#define NOAAPORT_NBSHEADERS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdint.h>

#define NBS_MAX_FRAME 5200 ///< Maximum size of an NBS frame in bytes

/// Frame header command values:
enum {
    NBS_FH_CMD_DATA = 3,
    NBS_FH_CMD_SYNC = 5,
    NBS_FH_CMD_TEST = 10
};

/// NBS Frame Header
typedef struct NbsFH
{
   unsigned hdlcAddress; ///< 255
   unsigned hdlcControl; ///< Unused
   unsigned version;     ///< SBN version
   unsigned size;        ///< Size of frame header in bytes
   unsigned control;     ///< Unused
   /**
    * SBN command:
    *   3 = Product format data transfer
    *   5 = Synchronize timing
    *   10 = Test message
    */
   unsigned command;
   /**
    * Identifies the channel (data stream):
    *   1 = GOES EAST
    *   2 = GOES WEST
    *   3 = Reserved
    *   4 = NOAAPORT OPT (Non-GOES Imagery/DCP)
    *   5 = NMC (NCEP/NWSTG)
    *   6 = Reserved
    *   7 = Reserved
    */
   unsigned datastream;
   /**
    * Source of data transmission:
    *   1 = Generated at primary NCF
    *   2 = Reserved
    */
   unsigned source;
   unsigned destination; ///< Destination of data transmission: 0 = All
   /**
    * Unique sequence number for each frame. This field is used in detecting
    * lost packets. Currently ARQ or selective repeat is not implemented.
    */
   unsigned seqno;
   /**
    * Unique run identifier. This field will be incremented each time the
    * sequence number is reset.
    */
   unsigned runno;
   /**
    * Checksum is used for frame validation. Unsigned sum of all bytes in frame
    * level header (except this field of 2 bytes).
    */
   unsigned checksum;
} NbsFH;

/// NBS Product-Definition Header
typedef struct NbsPDH
{
    unsigned version; ///< Version
    unsigned size;    ///< Header length in bytes
    /**
     * Transfer type. Indicates the status of a product transfer:
     *    1 = Start of a new product
     *    2 = Product transfer still in progress
     *    4 = End (last packet) of this product
     *    8 = Product error
     *   32 = Product Abort
     *   64 = Option headers follow; e.g., product-specific header
     */
    unsigned transferType;
    /**
     * Total length of product header in bytes for this frame, including options
     */
    unsigned totalSize;
    unsigned pshSize;   ///< Size of PSH in bytes
    /**
     * Used during fragmentation and reassembly to identify the sequence
     * of the fragmented blocks. Blocks are numbered 0 to n.
     */
    unsigned blockNum;
    /**
     * Offset in bytes where the data for this block can be found relative
     * to beginning of data block area.
     */
    unsigned dataBlockOffset;
    unsigned dataBlockSize; ///< Number of data bytes in the data block
     /**
      * Number of records within the data block. This permits multiple
      * records per block.
      */
    unsigned recsPerBlock;
    /**
     * Number of blocks a record spans. Records can span multiple
     * blocks.
     */
    unsigned blocksPerRec;
    /**
     * Unique product sequence number for this product within the
     * logical data stream. Used for product reassembly integrity to
     * verify that blocks belong to the same product.
     */
    unsigned prodSeqNum;
} NbsPDH;

/// NBS Product-Specific Header
typedef struct NbsPSH
{
    unsigned optFieldNum;
    unsigned optFieldType;
    unsigned size;    ///< Size of product-specific header in bytes
    unsigned version; ///< AWIPS product-specific header version number.
     /**
      * Indicates the status of a product transfer:
      *     1 = Start of a new product
      *     2 = Product transfer still in progress
      *     4 = End (last packet) of this product
      *     8 = Product error
      *    16 = Product Retransmit
      *    32 = Product Abort
      *
      *    Unpublished Information:
      *     - Transfer Type is a bit mask of possible values
      *     - Value 0x40 is defined as “XFR_MORE_HDR”
      */
    unsigned flag;
    unsigned awipsSize;   ///< Length of AWIPS product-specific header in bytes
    unsigned bytesPerRec; ///< For GOES: Number of bytes per scan line.
    /**
     * Identifies the type of product
     *  1 = GOES EAST
     *  2 = GOES WEST
     *  3 = NOAAPORT OPT (Non-GOES Imagery)
     *  4 = NWSTG (NCEP/NWSTG)
     *  5 = NEXRAD
     */
    unsigned type;
    /**
     * Identifies the category of the product, i.e., image,
     * graphic, text, grid, point, binary, other.
     */
    unsigned category;
    /**
     * Identifies the code of the product. (Numeric value of 0 to 255)
     */
    unsigned prodCode;
    /**
     * Total number of blocks or fragments this product was broken into:
     *    0 = multiple products in this frame
     *    # = number of fragments
     *   -1 = unknown
     */
    int      numFrags;
    /**
     * Offset in bytes from the beginning of this product-specific header to
     * the next product-specific header. Reserved for future consideration.
     */
    unsigned nextHeadOff;
#if 1
    /*
     * The following is from
     *   NOAAPORT Broadcast System Document
     *   1 October 1997
     *   Contract No. 50-SPNA-3-00001
     *   SOW C.3.1.2.5.8.1
     *   CDRL A010A
     *   Doc. ID No. AWP.DSN.NPBRD-04.00
     */
    unsigned reserved; ///< Reserved
    /**
     * Product original source at central interface (e. g., NWSTG PVC, etc).
     */
    unsigned  source;
    /**
     * Original product sequence number as sent by NCF.
     * Number Used during retransmit only; otherwise, the value is 0.
     */
    unsigned seqNum;
    unsigned ncfRecvTime; ///< Time that product started being received at NCF
    unsigned ncfSendTime; ///< Time that product started transmit from NCF
    /**
     * Unique product-specific run identifier (parm for retransmission)
     */
    unsigned currRunId;
    /**
     * Original run ID for product (used during retransmit only)
     */
    unsigned origRunId;
#else
    /*
     * The following is from
     *   NOAA Weather Wire Service
     *   (NWWS) Project
     *   NWWS Interface Information Document
     *   Version 0.3
     *   August 26, 2014
     */
    unsigned baseProdSeqNum; ///< Product sequence number as sent by NCF
    unsigned baseProdSrc:    ///< Product source identification
    unsigned prodStartTime;  ///< Time that product started being processed
    unsigned ncfRecvTime;    ///< Time that product started being received at NCF
    unsigned ncfSendTime;    ///< Time that product started transmit from NCF
    unsigned procCntrlFlag;  ///< Control Flag: 0=OK 1=Abort receive list 2=Discard
    unsigned lastBuf;        ///< Last Buffer to put on list
    unsigned firstBufOnList; ///< Buffer number currently at head of list
    unsigned expectBufNum;   ///< Total Buffers expected for the product
    /**
     * Unique product-specific run identifier
     */
    unsigned currRunId;
#endif
} NbsPSH;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Decodes an NBS frame header
 *
 * @param[in]  buf      Start of frame header
 * @param[in]  size     Size of buffer in bytes
 * @param[out] fh       Frame header
 * @retval     0        Success. `fh` is set.
 * @retval     EINVAL   `buf == NULL || fh == NULL` or `size` is too small.
 *                      `log_add()` called.
 * @retval     EBADMSG  Invalid frame header. `log_add()` called.
 */
int nbs_decodeFH(
        const uint8_t*        buf,
        const size_t          size,
        NbsFH* const fh);

/**
 * Adds log messages for a frame header by calling `log_add()`. Doesn't call
 * `log_flush()`.
 *
 * @param[in] fh  Frame header
 */
void nbs_logFH(const NbsFH* fh);

/**
 * Decodes an NBS product-definition header
 *
 * @param[in]  buf      Start of product-definition header
 * @param[in]  size     Size of buffer in bytes
 * @param[in]  fh       Frame header
 * @param[out] pdh      Product-definition header
 * @retval     0        Success. `pdh` is set.
 * @retval     EINVAL   `buf == NULL || pdh == NULL` or `size` is too small.
 *                      `log_add()` called.
 * @retval     EBADMSG  Invalid product-definition header. `log_add()` called.
 */
int nbs_decodePDH(
        const uint8_t* buf,
        const size_t   size,
        const NbsFH*   fh,
        NbsPDH* const  pdh);

/**
 * Adds log messages for a product-definition header by calling `log_add()`.
 * Doesn't call `log_flush()`.
 *
 * @param[in] pdh  Product-definition header
 */
void nbs_logPDH(const NbsPDH* pdh);

/**
 * Decodes an NBS product-specific header
 *
 * @param[in]  buf      Start of product-specific header
 * @param[in]  size     Size of buffer in bytes
 * @param[in]  pdh      Product-definition header
 * @param[out] psh      Product-specific header
 * @retval     0        Success. `psh` is set.
 * @retval     EINVAL   `buf == NULL || pdh == NULL || psh == NULL` or `size` is
 *                      too small. `log_add()` called.
 * @retval     EBADMSG  Invalid product-specific header. `log_add()` called.
 */
int nbs_decodePSH(
        const uint8_t* buf,
        const size_t   size,
        const NbsPDH*  pdh,
        NbsPSH* const  psh);

/**
 * Adds log messages for a product-specific header by calling `log_add()`.
 * Doesn't call `log_flush()`.
 *
 * @param[in] psh  Product-specific header
 */
void nbs_logPSH(const NbsPSH* psh);

/**
 * Adds log messages for all, undecoded NBS headers by calling `log_add()`.
 * Doesn't call `log_flush()`.
 *
 * @param[in]  buf      Start of frame header
 * @param[in]  size     Size of buffer in bytes
 * @retval     0        Success
 * @retval     EINVAL   `buf == NULL` or `size` is too small. `log_add()` called.
 * @retval     EBADMSG  Invalid header. `log_add()` called.
 */
int nbs_logHeaders(
        const uint8_t* buf,
        const size_t   size);

#ifdef __cplusplus
}
#endif

#endif /* NOAAPORT_NBSHEADERS_H_ */
