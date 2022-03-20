/**
 * This file implements the API for reading NOAAPort Broadcast System (NBS) frames.
 *
 *        File: NbsFrame.c
 *  Created on: Feb 1, 2022
 *      Author: Steven R. Emmerson
 */

/*
* Apparently, synchronization frames either don't have a PDH or the PDH they have is bogus.
* Here are the log messages for the FH and PDH of such a frame:
*
*   Invalid product-definition header
*   Frame Header:
*     hdlcAddress = 0xff
*     hdlcControl = 0
*         version = 1
*            size = 16 bytes
*         control = 0
*         command = 5                // Synchronization frame
*      datastream = 5
*          source = 33
*     destination = 0
*           seqno = 510515957
*           runno = 0
*        checksum = 920
*   Product-Definition Header:
*             version = 1
*                size = 32 bytes     // Bogus
*        transferType = 0            // Bogus
*            PSH size = 0 bytes
*            blockNum = 25138        // Bogus
*     dataBlockOffset = 22288 bytes  // Bogus
*       dataBlockSize = 12339 bytes  // Bogus
*        recsPerBlock = 47
*        blocksPerRec = 49
*          prodSeqNum = 909062704
*   Synchronizing
*
* Unfortunately, discarding such frames currently makes noaaportIngester(1) report a "gap"
* because it doesn't see the FH sequence number.
*
* Such frames have been observed arriving approximately three per minute. If such frames
* had no PDH, then the bytes read by noaaportIngester(1) for the PDH (and subsequently
* discarded) would actually be the start of the next frame; consequently,
* noaaportIngester(1) would miss the FH sequence number and report many more gaps than it
* does. Therefore, such frames must have bytes following the frame header.
*
* But, how many bytes? The canonical size (16 bytes) or the stated size (32 bytes)?
*
* --SRE 2022-03-17
*/

/*
* The following code causes noaaportIngester(1) to report a gap for every synchronization
* frame:
*
*   if (reader->fh.command == NBS_FH_CMD_SYNC) {
*       reader->buf[0] = 0; // Causes search for start-of-frame
*       continue; // Get next frame
*   }
*/

/*
 * The following statement appears to cause noaaportIngester(1) to log messages like
 * these:
 *
 *   NOTE  SDUS53 KLMK 171723 /pTV0SDF !nids/ inserted NEXRAD3 [cat 99 type 4 ccb 2/0 seq 111001018 size 78369]
 *   ERROR SBN checksum invalid 2593 65271
 *   ERROR SBN checksum invalid 3170 6581
 *   ERROR SBN checksum invalid 2899 25212
 *   ERROR SBN checksum invalid 2459 5145
 *   ERROR SBN checksum invalid 2026 50762
 *   ERROR SBN checksum invalid 1854 64024
 *   ERROR SBN checksum invalid 2351 5209
 *   ERROR SBN checksum invalid 2084 40545
 *   ERROR SBN checksum invalid 1512 43243
 *   ERROR SBN checksum invalid 1849 43898
 *   ERROR SBN checksum invalid 1954 15245
 *   ERROR SBN checksum invalid 2109 52875
 *   ERROR SBN checksum invalid 1866 47555
 *   WARN  Gap in packet sequence: 530382880 to 530382882 [skipped 1]
 *   ERROR Missing fragment in sequence, last 0/111001019 this 2/111001019
 *   NOTE  SDUS53 KLBF 171719 /pDSPLNX !nids/ inserted NEXRAD3 [cat 99 type 4 ccb 2/0 seq 111001020 size 5359]
 *
 * reader->size = reader->fh.size + NBS_PDH_SIZE; // Right amount?
 */

#include "config.h"

#include "NbsFrame.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

struct NbsReader {
    NbsFH    fh;                               ///< Decoded frame header
    NbsPDH   pdh;                              ///< Decoded product-definition header
    uint8_t* end;                              ///< One byte beyond buffer contents
    uint8_t* nextFH;                           ///< Start of next frame header in buffer
    size_t   size;                             ///< Active frame size in bytes
    int      fd;                               ///< Input file descriptor
    bool     logSync;                          ///< Log synchronizing message?
    uint8_t  buf[NBS_MAX_FRAME + NBS_FH_SIZE]; ///< Frame buffer
};

static void nbs_init(
        NbsReader* reader,
        const int  fd)
{
    reader->fd = fd;
    reader->end = reader->buf;
    reader->nextFH = NULL;
    reader->size = 0;
    reader->logSync = true;
}

static void nbs_destroy(NbsReader* reader)
{
	close(reader->fd);
}

NbsReader* nbs_new(int fd)
{
    NbsReader* reader = malloc(sizeof(NbsReader));
    if (reader == NULL) {
        log_add_syserr("Couldn't allocated %zu-byte NBS reader structure", sizeof(NbsReader));
    }
    else {
        nbs_init(reader, fd);
    }
    return reader;
}

/**
 * Frees the resources associated with an NBS frame reader. Closes the
 * file descriptor given to `nbs_newReader()`.
 *
 * @param[in] reader  NBS reader
 * @see `nbs_newReader()`
 */
void nbs_free(NbsReader* reader)
{
	nbs_destroy(reader);
	free(reader);
}

ssize_t
getBytes(int fd, uint8_t* buf, size_t nbytes)
{
    int nleft = nbytes;
    while (nleft > 0)
    {
        ssize_t n = read(fd, buf, nleft);
        if (n < 0 || n == 0)
            return n;
        buf += n;
        nleft -= n;
    }
    return nbytes;
}

/**
 * Ensures that the frame buffer contains a given number of bytes. Reads more if necessary.
 *
 * @param[in] reader    NBS reader structure
 * @param[in] need      Number of bytes needed in buffer
 * @retval NBS_SUCCESS  Success
 * @retval NBS_SPACE    Insufficient space. `log_add()` called.
 * @retval NBS_EOF      EOF. `log_add()` called.
 * @retval NBS_IO       I/O failure. `log_add()` called.
 */
static int ensureBytes(
        NbsReader* const reader,
        const size_t     need)
{
    ssize_t status;
    if (need > sizeof(reader->buf)) {
        log_add("Desired number of bytes (%zu) > available space (%zu)",
                need, sizeof(reader->buf));
        status = NBS_SPACE;
    }
    else if (need <= reader->end - reader->buf) {
        status = NBS_SUCCESS;
    }
    else {
        size_t nbytes = reader->buf + need - reader->end;
        status = getBytes(reader->fd, reader->end, nbytes);
        if (status < 0) {
            log_add_syserr("Couldn't read %zu bytes", nbytes);
            status = NBS_IO;
        }
        else if (status == 0) {
            log_add("EOF read");
            status = NBS_EOF;
        }
        else {
            reader->end += status;
            status = NBS_SUCCESS;
        }
    }
    return status;
}

/**
 * Appends bytes to the buffer until the next frame header is appended.
 *
 * @param[in,out] reader  NBS reader
 * @retval NBS_SUCCESS    Success
 * @retval NBS_SPACE      Buffer is too small
 * @retval NBS_EOF        EOF. `log_add()` called.
 * @retval NBS_IO         I/O failure. `log_add()` called.
 */
static int appendThruNextFH(NbsReader* reader)
{
    int      status;
    uint8_t* buf = reader->buf;
    uint8_t* start = reader->end;

    for (;;) {
        status = ensureBytes(reader, start - buf + NBS_FH_SIZE);
        if (status)
            break;
        // `NBS_FH_SIZE` bytes have been appended to buffer

        start = memchr(start, 255, reader->end - start);
        if (start == NULL) {
            start = reader->end;
            continue;
        }
        // Start-of-frame sentinel exists somewhere in appended `NBS_FH_SIZE` bytes

        status = ensureBytes(reader, start - buf + NBS_FH_SIZE);
        if (status)
            break;

        const unsigned fhSize = (start[2] & 0xf) * 4;
        if (fhSize != NBS_FH_SIZE) {
            ++start;
            continue; // Try again
        }
        // Frame header referenced by `start` has correct size

        unsigned sum = 0;
        for (int i = 0; i < 14; ++i)
            sum += start[i];
        const unsigned checksum = ntohs(*(uint16_t*)(start+14));
        if (sum != checksum) {
            ++start;
            continue; // Try again
        }
        // Frame header referenced by `start` has correct checksum
        // Buffer ends with frame header referenced by `start`

        reader->size = start - reader->buf; // Number of bytes before appended frame header
        reader->nextFH = start;

        break;
    } // Input loop

    return status;
}

/**
 * Ensures that the buffer starts with a (decoded) frame header. If necessary, bytes are appended to
 * the buffer until the next frame header is seen. The next frame header is moved to the beginning
 * of the buffer and decoded.
 *
 * @param[in,out] reader  NBS reader
 * @retval NBS_SUCCESS    Success. `reader->nextFH` will be NULL.
 * @retval NBS_EOF        EOF. `log_add()` called.
 * @retval NBS_IO         I/O failure. `log_add()` called.
 */
static int ensureFH(NbsReader* reader)
{
    if (reader->nextFH) {
        log_assert(reader->end != reader->buf);
        log_assert(reader->end >= reader->nextFH);
    }
    if (reader->end == reader->buf) {
        log_assert(reader->nextFH == NULL);
    }

    int status = reader->nextFH ? 0 : appendThruNextFH(reader);

    if (status == 0) {
        // Move the next frame header to the beginning of the buffer
        size_t excess = reader->end - reader->nextFH;
        memmove(reader->buf, reader->nextFH, excess);
        reader->end = reader->buf + excess;
        reader->nextFH = NULL;

        uint8_t* buf = reader->buf;
        NbsFH*   fh = &reader->fh;

        fh->size = (buf[2] & 0xf) * 4;
        fh->checksum = ntohs(*(uint16_t*)(buf+14));
        fh->hdlcAddress = buf[0];
        fh->hdlcControl = buf[1];
        fh->version = buf[2] >> 4;
        fh->control = buf[3];
        fh->command = buf[4];
        fh->datastream = buf[5];
        fh->source = buf[6];
        fh->destination = buf[7];
        fh->seqno = ntohl(*(uint32_t*)(buf+8));
        fh->runno = ntohs(*(uint16_t*)(buf+12));
    }

    return status;
}

/**
 * Reads and decodes the product-definition header, which follows the frame header, which is at the
 * beginning of the buffer,
 *
 * @param[in,out] reader  NBS reader
 * @retval NBS_SUCCESS    Success
 * @retval NBS_INVAL      PDH is invalid. `log_add()` called.
 * @retval NBS_EOF        EOF. `log_add()` called.
 * @retval NBS_IO         I/O failure. `log_add()` called.
 */
static int readPDH(NbsReader* reader)
{
    int status = ensureBytes(reader, reader->fh.size + NBS_PDH_SIZE);

    if (status == NBS_SUCCESS) {
        status = NBS_INVAL;

        NbsFH*         fh = &reader->fh;
        unsigned char* buf = reader->buf + fh->size;
        NbsPDH*        pdh = &reader->pdh;

        memset(pdh, 0, sizeof(*pdh));

        pdh->size = (buf[0] & 0xf) * 4;
        pdh->version = buf[0] >> 4;
        pdh->transferType = buf[1];
        pdh->totalSize = ntohs(*(uint16_t*)(buf+2)); // PDH size + PSH size
        pdh->pshSize = pdh->totalSize - pdh->size;
        pdh->blockNum = ntohs(*(uint16_t*)(buf+4));
        pdh->dataBlockOffset = ntohs(*(uint16_t*)(buf+6));
        pdh->dataBlockSize = ntohs(*(uint16_t*)(buf+8));
        pdh->recsPerBlock = buf[10];
        pdh->blocksPerRec = buf[11];
        pdh->prodSeqNum = ntohl(*(uint32_t*)(buf+12));

        if (pdh->size < 16) {
            log_add("Product-definition header size (%u bytes) < 16 bytes", pdh->size);
        }
        else if (fh->size + pdh->size > sizeof(reader->buf)) {
            log_add("Product-definition header size is too large: %u bytes", pdh->size);
        }
        else if (pdh->totalSize < pdh->size) {
            log_add("PDH size + PSH size (%u bytes) < PDH size (%u) bytes", pdh->totalSize,
                    pdh->size);
        }
        else if (fh->size + pdh->totalSize > sizeof(reader->buf)) {
            log_add("Size of PDH + PSH headers is too large: %u bytes", pdh->totalSize);
        }
        else {
            const size_t frameSize = fh->size + pdh->totalSize + pdh->dataBlockSize;
            if (frameSize > sizeof(reader->buf)) {
                log_add("Frame size is too large: %u bytes", frameSize);
            }
            else {
                status = 0;
            }
        }
    } // Have potential PDH

    return status;
}

static int processFrame(NbsReader* reader)
{
    int status;

    if (reader->fh.command != NBS_FH_CMD_DATA) {
        // The format of the frame is unknown => append bytes until the next frame header is seen
        status = appendThruNextFH(reader);
        if (status) {
            log_add("Couldn't get next frame header");
        }
        else {
            /*
             * Buffer starts with a (decoded) frame header, contains a complete frame, and ends
             * with the next frame header
             */
            reader->size = reader->nextFH - reader->buf;
        }
    } // Frame is not a data frame (e.g., synchronization frame, test frame)
    else {
        /*
         * The format of the frame is known => read the stated frame bytes. This is more efficient
         * than the previous "if", which scans every byte looking for a frame header.
         */
        status = readPDH(reader);
        if (status == NBS_INVAL) {
            if (reader->logSync) {
                log_add("Invalid product-definition header");
                nbs_logFH(&reader->fh);
                nbs_logPDH(&reader->pdh);
            }
        }
        else if (status) {
            log_add("Couldn't get product-definition header");
        }
        else {
            size_t need = reader->fh.size + reader->pdh.totalSize + reader->pdh.dataBlockSize;
            status = ensureBytes(reader, need);
            if (status) {
                // PDH is valid => must be severe failure in `ensureBytes()`
                log_add("Couldn't read data block");
            }
            else {
                // Buffer starts with a complete frame
                reader->size = need;
            }
        }
    } // Frame is a data frame

    reader->end = reader->buf; // Causes buffer contents to be ignored next time

    return status;
}

int nbs_getFrame(
        NbsReader* const reader,
        uint8_t** const  frame,
        size_t* const    size,
        NbsFH** const    fh,
        NbsPDH** const   pdh)
{
    int status;

    for (;;) {
        status = ensureFH(reader); // `reader->nextFH` will be NULL on success
        if (status)
            break; // `NBS_MAX_FRAME < sizeof(reader->buf)` => must be a severe error
        // Buffer now starts with a (decoded) frame header

        status = processFrame(reader); // Frame now referenced by `reader->buf` & `reader->size`
        if (status == 0) {
            reader->logSync = true;
            *frame = reader->buf;
            *size = reader->size;
            *fh = &reader->fh;
            *pdh = (reader->fh.command == NBS_FH_CMD_DATA)
                    ? &reader->pdh
                    : NULL;
            break;
        }
        log_add("Couldn't process frame");

        if (status != NBS_INVAL && status != NBS_SPACE)
            break; // Severe error

        // Log messages are queued
        if (!reader->logSync) {
            log_clear();
        }
        else {
            log_add("Synchronizing");
            log_flush_notice();
            reader->logSync = false;
        }
    } // Indefinite loop

    return status;
}
