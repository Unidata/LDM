/**
 * This file implements the API for reading NOAAPort Broadcast System (NBS)
 * frames.
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

ssize_t
getBytes(int fd, uint8_t* buf, size_t nbytes)
{
    int nleft = nbytes;
    while (nleft > 0)
    {
        ssize_t n = read(fd, buf, nleft);
        //int n = recv(fd, (char *)buf,  nbytes , 0) ;
        if (n < 0 || n == 0)
            return n;
        buf += n;
        nleft -= n;
    }
    return nbytes;
}

/**
 * Ensures that the frame buffer contains a given number of bytes. Reads more
 * if necessary.
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
    int status;
    if (need > sizeof(reader->buf)) {
        log_add("Desired number of bytes (%zu) > available space (%zu)",
                need, sizeof(reader->buf));
        status = NBS_SPACE;
    }
    else if (need <= reader->have) {
        status = NBS_SUCCESS;
    }
    else {
        status = getBytes(reader->fd, reader->buf+reader->have, need-reader->have);
        if (status < 0) {
            log_add_syserr("Couldn't read %zu bytes", need-reader->have);
            status = NBS_IO;
        }
        else if (status == 0) {
            log_add("EOF read");
            status = NBS_EOF;
        }
        else {
            reader->have += status;
            status = NBS_SUCCESS;
        }
    }
    return status;
}

void nbs_init(
        NbsReader* reader,
        const int  fd)
{
    reader->fd = fd;
    reader->have = 0;
    reader->size = 0;
    reader->logSync = true;
}

void nbs_destroy(NbsReader* reader)
{
	close(reader->fd);
}

NbsReader* nbs_newReader(int fd)
{
    NbsReader* reader = malloc(sizeof(NbsReader));
    nbs_init(reader, fd);

    return reader;
}

/**
 * Frees the resources associated with an NBS frame reader. Closes the
 * file descriptor given to `nbs_newReader()`.
 *
 * @param[in] reader  NBS reader
 * @see `nbs_newReader()`
 */
void nbs_freeReader(NbsReader* reader)
{
	nbs_destroy(reader);
	free(reader);
}

/**
 * @retval NBS_SUCCESS  Success
 * @retval NBS_EOF      EOF. `log_add()` called.
 * @retval NBS_IO       I/O failure. `log_add()` called.
 */
static int getFH(NbsReader* reader)
{
    int            status;
    unsigned char* buf = reader->buf;

    // `ensureBytes()` won't return `NBS_SPACE` because `NBS_FH_SIZE < NBS_MAX_FRAME`
    for (status = ensureBytes(reader, NBS_FH_SIZE); status == 0;
            status = ensureBytes(reader, NBS_FH_SIZE)) {
        unsigned char* start = memchr(buf, 255, reader->have);

        if (start == NULL) {
            reader->have = 0;
        }
        else {
            const ptrdiff_t delta = start - buf;

            if (delta) {
                memmove(buf, start, reader->have - delta);
                reader->have -= delta;
            }
            else {
                // `buf[0] == 255`

                unsigned sum = 0;
                for (int i = 0; i < 14; ++i)
                    sum += buf[i];

                NbsFH* fh = &reader->fh;
                fh->checksum = ntohs(*(uint16_t*)(buf+14));
                if (sum != fh->checksum) {
                    log_debug("Frame sum (%u) != checksum (%u). Continuing.", sum, fh->checksum);
                    buf[0] = 0; // Causes search for new start
                }
                else {
                    // Checksums match
                    fh->size = (buf[2] & 0xf) * 4;
                    if (fh->size != NBS_FH_SIZE) {
                        log_debug("Frame header size (%u bytes) != %d bytes. Continuing.",
                                fh->size, NBS_FH_SIZE);
                        buf[0] = 0; // Causes search for new start
                    }
                    else {
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
                        break;
                    } // Frame header looks valid
                } // Checksums match
            } // `buf[0] == 255`
        } // Buffer contains 255 somewhere
    } // Input loop

    return status;
}

/**
 * @retval NBS_SUCCESS  Success
 * @retval NBS_INVAL    Invalid header
 * @retval NBS_EOF      EOF. `log_add()` called.
 * @retval NBS_IO       I/O failure. `log_add()` called.
 */
static int readPDH(NbsReader* reader)
{
    int status = ensureBytes(reader, reader->fh.size + NBS_PDH_SIZE);

    if (status != NBS_SUCCESS) {
        log_add("Couldn't read product-definition header");
    }
    else {
        status = NBS_INVAL;

        NbsFH*         fh = &reader->fh;
        unsigned char* buf = reader->buf + fh->size;
        NbsPDH*        pdh = &reader->pdh;

        memset(pdh, 0, sizeof(*pdh));

        pdh->size = (buf[0] & 0xf) * 4;
        if (pdh->size < 16) {
            log_add("Product-definition header size (%u bytes) < 16 bytes", pdh->size);
        }
        else if (fh->size + pdh->size > sizeof(reader->buf)) {
            log_add("Product-definition header size is too large: %u bytes", pdh->size);
        }
        else {
            pdh->totalSize = ntohs(*(uint16_t*)(buf+2)); // PDH size + PSH size

            if (pdh->totalSize < pdh->size) {
                log_add("PDH size + PSH size (%u bytes) < PDH size (%u) bytes", pdh->totalSize,
                        pdh->size);
            }
            else if (fh->size + pdh->totalSize > sizeof(reader->buf)) {
                log_add("Size of PDH + PSH headers is too large: %u bytes", pdh->totalSize);
            }
            else {
#if 1
                pdh->dataBlockSize = ntohs(*(uint16_t*)(buf+8));
                const unsigned long frameSize = fh->size + pdh->totalSize + pdh->dataBlockSize;
                if (frameSize > sizeof(reader->buf)) {
                    log_add("Frame size is too large: %u bytes", frameSize);
                }
                else {
                    pdh->pshSize = pdh->totalSize - pdh->size;
                    pdh->transferType = buf[1];
                    pdh->version = buf[0] >> 4;
                    pdh->prodSeqNum = ntohl(*(uint32_t*)(buf+12));
                    pdh->blockNum = ntohs(*(uint16_t*)(buf+4));
                    pdh->dataBlockOffset = ntohs(*(uint16_t*)(buf+6));
                    log_debug("pdh->dataBlockOffset=%u", pdh->dataBlockOffset);
                    pdh->recsPerBlock = buf[10];
                    pdh->blocksPerRec = buf[11];

                    status = 0;
                }
#else
                if (pdh->pshSize && ((pdh->transferType & 1) == 0)) {
                    log_add("Frame isn't start-of-product but PSH size is %u bytes", pdh->pshSize);
                }
                else if (pdh->pshSize && ((pdh->transferType & 64) == 0)) {
                    log_add("Product-specific header shouldn't exist but PSH size is %u bytes",
                            pdh->pshSize);
                }
                else {
                    if (pdh->pshSize == 0 && (fh->command == NBS_FH_CMD_SYNC ||
                            pdh->transferType == 0)) {
                        status = 0;
                    }
                    else {
                        pdh->dataBlockSize = ntohs(*(uint16_t*)(buf+8));

                        const unsigned long frameSize = fh->size + pdh->size + pdh->pshSize +
                                pdh->dataBlockSize;
                        if (frameSize > sizeof(reader->buf)) {
                            log_add("Frame size is too large: %u bytes", frameSize);
                        }
                        else {
                            pdh->version = buf[0] >> 4;
                            pdh->prodSeqNum = ntohl(*(uint32_t*)(buf+12));
                            pdh->blockNum = ntohs(*(uint16_t*)(buf+4));
                            pdh->dataBlockOffset = ntohs(*(uint16_t*)(buf+6));
                            pdh->recsPerBlock = buf[10];
                            pdh->blocksPerRec = buf[11];

                            status = 0;
                        } // Valid frame size
                    } // Frame contains data
                } // PSH size is consistent with transfer type
#endif
            } // PDH size + PSH size >= PDH size
        } // PDH size >= 16 bytes
    } // Read potential PDH

    return status;
}

/**
 * @retval NBS_SUCCESS  Success
 * @retval NBS_INVAL    Invalid header
 * @retval NBS_EOF      EOF. `log_add()` called.
 * @retval NBS_IO       I/O failure. `log_add()` called.
 */
static int readPSH(NbsReader* reader)
{
    int status = ensureBytes(reader, reader->fh.size + reader->pdh.size + reader->pdh.pshSize);
    if (status) {
        log_add("Couldn't read product-specific header");
    }
    else {
        status = NBS_INVAL;

        unsigned char* buf = reader->buf + reader->fh.size + reader->pdh.size;
        NbsPDH*        pdh = &reader->pdh;
        NbsPSH*        psh = &reader->psh;

        psh->size = ntohs(*(uint16_t*)(buf+2));
        if (psh->size != pdh->pshSize) {
            log_add("Product-specific header size (%u bytes) != that in "
                    "product-definition header (%u bytes)", psh->size,
                    pdh->pshSize);
        }
        else {
            psh->optFieldNum = buf[0];
            psh->optFieldType = buf[1];
            psh->version = buf[4];
            psh->flag = buf[5];
            psh->awipsSize = ntohs(*(uint16_t*)(buf+6));
            psh->bytesPerRec = ntohs(*(uint16_t*)(buf+8));
            psh->type = buf[10];
            psh->category = buf[11];
            psh->prodCode = ntohs(*(uint16_t*)(buf+12));
            psh->numFrags = ntohs(*(uint16_t*)(buf+14));
            psh->nextHeadOff = ntohs(*(uint16_t*)(buf+16));
            psh->reserved = buf[18]; ///< Reserved
            psh->source = buf[19];
            psh->seqNum = ntohl(*(uint32_t*)(buf+20));
            psh->ncfRecvTime = ntohl(*(uint32_t*)(buf+24));
            psh->ncfSendTime = ntohl(*(uint32_t*)(buf+28));
            psh->currRunId = ntohs(*(uint16_t*)(buf+32));
            psh->origRunId = ntohs(*(uint16_t*)(buf+34));
            status = 0;
        }
    }

    return status;
}

int nbs_getFrame(NbsReader* const reader)
{
    int status;

    const ssize_t excess = reader->have - reader->size;
    if (excess > 0)
        memmove(reader->buf, reader->buf+reader->size, excess);
    reader->have = excess;
    reader->size = 0;

    for (;;) {
        status = getFH(reader);
        if (status) {
            log_add("Couldn't get frame header");
            break;
        }

        status = readPDH(reader);
        if (status) {
            log_add("Couldn't read product-definition header");
            if (status != NBS_INVAL)
                break;
            if (reader->logSync) {
                nbs_logFH(&reader->fh);
                nbs_logPDH(&reader->pdh);
            }
        }
        else {
#if 0
            if (reader->pdh.pshSize != 0) {
                status = readPSH(reader);
                if (status) {
                    log_add("Couldn't read product-specific header");
                    if (status != NBS_INVAL)
                        break;
                    if (reader->logSync) {
                        nbs_logFH(&reader->fh);
                        nbs_logPDH(&reader->pdh);
                        nbs_logPSH(&reader->psh);
                    }
                }
            } // Product-specific header exists
#endif

            if (status == 0) {
                size_t need = reader->fh.size + reader->pdh.totalSize +
                        //reader->pdh.dataBlockOffset +
                        reader->pdh.dataBlockSize;
                status = ensureBytes(reader, need);
                if (status) {
                    log_add("Couldn't read data block");
                    break;
                }
                reader->size = need;
                reader->logSync = true;

                return 0;
            } // All headers read and verified
        } // Valid PDH

        // Invalid PDH or PSH added to log messages
        if (reader->logSync) {
            log_add("Synchronizing");
            log_flush_info();
            reader->logSync = false;
        }
        else {
            log_clear(); // Just in case
        }
    } // Indefinite loop

    // An unrecoverable error occurred
    log_flush_error();

    return status;
}
