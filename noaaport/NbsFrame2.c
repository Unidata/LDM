/**
 * This file implements the API for reading NOAAPort Broadcast System (NBS) frames.
 *
 *        File: NbsFrame.c
 *  Created on: Feb 1, 2022
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "NbsFrame.h"

#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum {
    START,              ///< Initial state. Process frame.
    SYNCHRONIZING,         ///< Buffer starts with a possible frame-level header
    SENTINEL_SEEN,      ///< Sentinel byte of frame-level header seen
    DATA_FH_SEEN,       ///< Data frame-level header seen
    TIME_FH_SEEN,       ///< Time-command frame-level header seen
    OTHER_FH_SEEN,      ///< Non-data, non-time-command frame-level header seen
    PDH_SEEN,           ///< Product-definition header seen
    NEXT_SENTINEL_SEEN, ///< Sentinel byte of next frame-level header seen
    NEXT_FH_SEEN        ///< Next frame-level header seen
} State;

struct NbsReader {
    int      fd;                               ///< Input file descriptor
    State    state;                            ///< State of this finite state machine
    uint8_t  buf[NBS_MAX_FRAME + NBS_FH_SIZE]; ///< Input buffer
    uint8_t* end;                              ///< Pointer to just past last byte in buffer
    NbsFH    fh;                               ///< Decoded frame-level header
    NbsPDH   pdh;                              ///< Decoded product-definition header
    uint8_t* nextFH;                           ///< Pointer to start of next frame-level header
    bool     logError;                         ///< Log error messages?
};

inline static void resetBuf(NbsReader* const reader)
{
    reader->end = reader->buf;
    reader->nextFH = NULL;
}

static void init(
        NbsReader* reader,
        const int  fd)
{
    reader->fd = fd;
    reader->state = START;
    reader->logError = true;
    resetBuf(reader);
}

static void destroy(NbsReader* reader)
{
	close(reader->fd);
}

NbsReader* nbs_new(int fd)
{
    NbsReader* reader = malloc(sizeof(NbsReader));
    if (reader == NULL) {
        log_add_syserr("Couldn't allocate %zu-byte NBS reader structure", sizeof(NbsReader));
    }
    else {
        init(reader, fd);
    }
    return reader;
}

/**
 * Frees the resources associated with an NBS frame reader. Closes the file descriptor given to
 * `nbs_new()`.
 *
 * @param[in] reader  NBS reader
 * @see `nbs_new()`
 */
void nbs_free(NbsReader* reader)
{
	destroy(reader);
	free(reader);
}

static ssize_t getBytes(
        const int    fd,
        uint8_t*     buf,
        const size_t nbytes)
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

static void leftJustify(
        NbsReader* const restrict     reader,
        const uint8_t* const restrict start)
{
    size_t nbytes = reader->end - start;
    memmove(reader->buf, start, nbytes);
    reader->end = reader->buf + nbytes;
}

static int vetFH(
        NbsReader* const restrict     reader,
        const uint8_t* const restrict fh,
        unsigned* const restrict      size,
        unsigned* const restrict      checksum)
{
    log_assert(*fh == 255);

    int status = ensureBytes(reader, (fh - reader->buf) + NBS_FH_SIZE);
    if (status == 0) {
        status = NBS_INVAL;

        const unsigned fhSize = (fh[2] & 0xf) * 4;
        if (fhSize != NBS_FH_SIZE) {
            log_add("Frame-level header size isn't %d bytes: %u", NBS_FH_SIZE, fhSize);
        }
        else {
            // Frame-level header has correct size
            unsigned sum = 0;
            for (int i = 0; i < 14; ++i)
                sum += fh[i];
            if (sum != ntohs(*(uint16_t*)(fh+14))) {
                log_add("Frame-level header checksum isn't %u: %u", sum);
            }
            else {
                // Frame-level header has correct checksum
                *size = fhSize;
                *checksum = sum;
                status = NBS_SUCCESS;
            }
        }
    }

    return status;
}

static int decodeFH(NbsReader* const reader)
{
    uint8_t* buf = reader->buf;
    NbsFH*   fh = &reader->fh;
    int      status = vetFH(reader, buf, &fh->size, &fh->checksum);

    if (status == 0) {
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

static int ensurePDH(NbsReader* const reader)
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
    }

    return status;
}

static int ensureTCH(NbsReader* const reader)
{
    int status = ensureBytes(reader, reader->fh.size + NBS_TCH_SIZE);
    if (status == NBS_SUCCESS) {
        unsigned size = ntohs(*(uint16_t*)((reader->buf + reader->fh.size)+2));

        if (size != 32) {
            log_add("Time-command header size in bytes (%u) != 32", size);
            status = NBS_INVAL;
        }
        else {
            status = NBS_SUCCESS;
        }
    }

    return status;
}

int nbs_getFrame(
        NbsReader* const restrict reader,
        uint8_t** const restrict  frame,
        size_t* const restrict    size,
        NbsFH** const restrict    fhArg,
        NbsPDH** const restrict   pdhArg)
{
    /*
     * The following code implements a non-deterministic finite state machine for parsing a NOAAPort
     * frame. At least it's supposed to. Considering that the NOAAPort documentation on the format
     * of frames is out-of-date, incomplete, and ambiguous, it'll be a small miracle if this works.
     * SRE 2022-03-22T18:13:31-0600
     */
    int status = 0;

    for (;;) {
        switch (reader->state) {
            case START: {
                log_debug("Reading frame-level header number of bytes");
                // Read a frame-level header's worth of bytes into an empty buffer.
                resetBuf(reader);
                status = ensureBytes(reader, NBS_FH_SIZE);
                if (status == NBS_SUCCESS)
                    reader->state = SYNCHRONIZING;
                break;
            }
            case SYNCHRONIZING: {
                log_debug("Looking for frame-level header sentinel");
                /*
                 * Buffer starts with at least NBS_FH_SIZE bytes. Look for the frame-level header's
                 * sentinel byte.
                 */
                uint8_t* start = memchr(reader->buf, 255, reader->end - reader->buf);
                if (start == NULL) {
                    // Try again
                    reader->state = START;
                }
                else {
                    leftJustify(reader, start);
                    reader->state = SENTINEL_SEEN;
                }
                break;
            }
            case SENTINEL_SEEN: {
                log_debug("Decoding frame-level header");
                // Buffer starts with the frame-level header's sentinel byte
                status = ensureBytes(reader, NBS_FH_SIZE);
                if (status == NBS_SUCCESS) {
                    status = decodeFH(reader);
                    if (status == NBS_INVAL) {
                        log_add("Invalid frame-level header");
                        reader->buf[0] = 0;
                        reader->state = SYNCHRONIZING;
                    }
                    else if (status == NBS_SUCCESS) {
                        if (reader->fh.command == NBS_FH_CMD_DATA) {
                            reader->state = DATA_FH_SEEN;
                        }
                        else if (reader->fh.command == NBS_FH_CMD_TIME) {
                            reader->state = TIME_FH_SEEN;
                        }
                        else {
                            reader->nextFH = reader->buf + reader->fh.size;
                            reader->state = OTHER_FH_SEEN;
                        }
                    }
                }
                break;
            }
            case DATA_FH_SEEN: {
                log_debug("Getting product-definition header");
                /*
                 * Buffer contains a (decoded) data-transfer frame-level header. Product-definition
                 * header is next.
                 */
                status = ensurePDH(reader);
                if (status == NBS_SUCCESS) {
                    reader->state = PDH_SEEN;
                }
                else if (status == NBS_INVAL) {
                    if (reader->logError) {
                        log_add("Invalid product-definition header");
                        nbs_logFH(&reader->fh);
                        nbs_logPDH(&reader->pdh);
                    }

                    reader->buf[0] = 0;
                    reader->state = SYNCHRONIZING;
                    status = 0;
                }
                break;
            }
            case PDH_SEEN: {
                log_debug("Reading data-block");
                /*
                 * Buffer contains a (decoded) frame-level header and a (decoded) product-definition
                 * header. Optional headers and data block are next
                 */
                status = ensureBytes(reader, reader->fh.size + reader->pdh.totalSize +
                        reader->pdh.dataBlockSize);
                // NBS_SPACE isn't possible because PDH was vetted
                if (status == NBS_SUCCESS) {
                    *frame = reader->buf;
                    *size = reader->end - reader->buf;
                    *fhArg = &reader->fh;
                    *pdhArg = &reader->pdh;
                    reader->logError = true; // Log next error
                    reader->state = START;
                    return 0;
                }
                break;
            }
            case TIME_FH_SEEN: {
                log_debug("Reading time-command header");
                /*
                 * Buffer contains a (decoded) time-command frame-level header. Time-command header
                 * is next.
                 */
                status = ensureTCH(reader);
                if (status == NBS_SUCCESS) {
                    *frame = reader->buf;
                    *size = reader->end - reader->buf;
                    *fhArg = &reader->fh;
                    *pdhArg = NULL;
                    reader->logError = true; // Log next error
                    reader->state = START;
                    return 0;
                }
                else if (status == NBS_INVAL) {
                    log_add("Invalid time-command header");

                    // Give up
                    reader->buf[0] = 0;
                    reader->state = SYNCHRONIZING;
                    status = 0;
                }
                break;
            }
            case OTHER_FH_SEEN: {
                log_debug("Searching for next frame-level header sentinel");
                /*
                 * Buffer contains at least a (decoded) frame-level header that indicates an unknown
                 * frame format. Find the start of the next frame-level header.
                 */
                for (uint8_t* nextFH = reader->nextFH; ; nextFH = reader->end - NBS_FH_SIZE) {
                    nextFH = memchr(nextFH, 255, reader->end - nextFH);
                    if (nextFH) {
                        reader->nextFH = nextFH;
                        reader->state = NEXT_SENTINEL_SEEN;
                        break;
                    }
                    status = ensureBytes(reader, (reader->end - reader->buf) + NBS_FH_SIZE);
                    if (status == NBS_SPACE) {
                        // Give up
                        reader->state = START;
                        status = 0;
                        break;
                    }
                    if (status != NBS_SUCCESS)
                        break;
                }
                break;
            }
            case NEXT_SENTINEL_SEEN: {
                log_debug("Vetting next frame-level header");
                /*
                 * Buffer contains a frame-level header that indicates an unknown frame, zero or
                 * more bytes, and at least the sentinel byte of a possible next FH. Vet the next
                 * FH.
                 */
                unsigned sz, ckSum;
                status = vetFH(reader, reader->nextFH, &sz, &ckSum); // Reads bytes if necessary
                if (status == NBS_SPACE) {
                    // Give up
                    reader->state = START;
                    status = 0;
                }
                else if (status == NBS_INVAL) {
                    ++reader->nextFH;
                    reader->state = OTHER_FH_SEEN;
                    status = 0;
                }
                else if (status == NBS_SUCCESS) {
                    *frame = reader->buf;
                    *size = reader->nextFH - reader->buf;
                    *fhArg = &reader->fh;
                    *pdhArg = NULL;
                    reader->logError = true; // Log next error
                    reader->state = NEXT_FH_SEEN;
                    return 0;
                }
                break;
            }
            case NEXT_FH_SEEN: {
                log_debug("Moving next frame-level header to start of buffer");
                /*
                 * Buffer contains the previous, processed frame and the next frame-level header.
                 * Move the next frame-level header to the start of the buffer.
                 */
                leftJustify(reader, reader->nextFH);
                reader->nextFH = NULL;
                reader->state = SENTINEL_SEEN;
                break;
            }
            default:
                log_fatal("Invalid state: %d", reader->state);
                abort();
        }

        if (status)
            break; // Severe error. `log_add()` called.

        // Non-fatal log messages are queued
        if (!reader->logError) {
            log_clear();
        }
        else {
            log_flush_warning();
            reader->logError = false; // Don't log subsequent errors
        }
    }

    return status;
}
