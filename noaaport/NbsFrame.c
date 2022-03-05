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
 * @param[in] reader  NBS reader structure
 * @param[in] need    Number of bytes needed in buffer
 * @retval NBS_SPACE  Insufficient space. `log_add()` called.
 * @retval NBS_EOF    EOF. `log_add()` called.
 * @retval NBS_IO     I/O failure. `log_add()` called.
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

struct NbsReader {
    int     fd;                 ///< Input file descriptor
    size_t  have;               ///< Number of bytes in buffer
    bool    logSync;            ///< Log synchronizing message?
    NbsFH   fh;                 ///< Decoded frame header
    NbsPDH  pdh;                ///< Decoded product-definition header
    NbsPSH  psh;                ///< Decoded product-specific header
    uint8_t buf[NBS_MAX_FRAME]; ///< Frame buffer
};

NbsReader* nbs_newReader(int fd)
{
    NbsReader* reader = malloc(sizeof(NbsReader));
    reader->fd = fd;
    reader->have = 0;
    reader->logSync = true;

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
	close(reader->fd);
	free(reader);
}

int nbs_getFrame(
        NbsReader* const      reader,
        const uint8_t** const buf,
        size_t*               size,
        const NbsFH** const   fh,
        const NbsPDH** const  pdh,
        const NbsPSH** const  psh)
{
    int status;

    for (;;) {
        // Ensure buffer contains possible frame header
        size_t  need = NBS_FH_SIZE;
        if ((status = ensureBytes(reader, need)) != NBS_SUCCESS) {
            log_add("Couldn't read frame header");
            break;
        }

        // Decode and verify frame header
        if (nbs_decodeFH(reader->buf, reader->have, &reader->fh) != 0) {
            nbs_logFH(&reader->fh);
            log_add("Invalid frame header");
        }
        else {
            if (fh)
                *fh = &reader->fh;

            // Read rest of frame header
            need = reader->fh.size;
            if ((status = ensureBytes(reader, need)) != NBS_SUCCESS) {
                log_add("Couldn't read rest of frame header");
                break;
            }

            // Ensure buffer contains possible product-definition header
            need = reader->fh.size + NBS_PDH_SIZE ;
            if ((status = ensureBytes(reader, need)) != NBS_SUCCESS) {
                log_add("Couldn't read product-definition header");
                break;
            }

            // Decode and verify product-definition header
            if (nbs_decodePDH(reader->buf+reader->fh.size,
                    reader->have, &reader->fh, &reader->pdh) != 0) {
                nbs_logFH(&reader->fh);
                nbs_logPDH(&reader->pdh);
                log_add("Invalid product-definition header");
            }
            else {
                if (pdh)
                    *pdh = &reader->pdh;

                // Read rest of product-definition header
                need = reader->fh.size + reader->pdh.size;
                if ((status = ensureBytes(reader, need)) != NBS_SUCCESS) {
                    log_add("Couldn't read rest of product-definition header");
                    break;
                }

                if (reader->pdh.pshSize == 0) {
                    if (psh)
                        *psh = NULL;
                }
                else {
                    // Ensure buffer contains product-specific header
                    need = reader->fh.size + reader->pdh.size +
                            reader->pdh.pshSize;
                    if ((status = ensureBytes(reader, need)) != NBS_SUCCESS) {
                        log_add("Couldn't read product-specific header");
                        break;
                    }

                    // Decode and verify product-specific header
                    if (nbs_decodePSH(
                            reader->buf+reader->fh.size+reader->pdh.size,
                            reader->have, &reader->pdh, &reader->psh) != 0) {
                        nbs_logFH(&reader->fh);
                        nbs_logPDH(&reader->pdh);
                        nbs_logPSH(&reader->psh);
                        log_add("Invalid product-specific header");
                        if (psh)
                           *psh = NULL;
                        status = NBS_INVAL;
                    }
                    else {
                        if (psh)
                            *psh = &reader->psh;
                    }
                }

                if (status == 0) {
                    if (reader->pdh.dataBlockSize) {
                        // Read data block
                        need += reader->pdh.dataBlockSize;
                        if ((status = ensureBytes(reader, need)) != NBS_SUCCESS) {
                            log_add("Couldn't read data block");
                            if (status != NBS_SPACE)
                                break;
                        }
                    }

                    if (buf)
                        *buf = reader->buf;
                    if (size)
                        *size = reader->fh.size + reader->pdh.size + reader->pdh.pshSize +
                                reader->pdh.dataBlockSize;

                    reader->have = 0;
                    reader->logSync = true;

                    return NBS_SUCCESS;
                } // All headers read and verified
            } // Valid PDH
        } // Valid FH

        // A header is invalid

        // Shift bytes down by one and append a byte
        memmove(reader->buf, reader->buf+1, reader->have-1);
        if ((status = getBytes(reader->fd, reader->buf+reader->have-1, 1))
                <= 0) {
            log_add("Couldn't append a byte to buffer");
            status = (status == 0) ? NBS_EOF : NBS_IO;
            break;
        }

        if (!reader->logSync) {
            log_clear();
        }
        else {
            log_flush_debug(); // Logs headers
            log_warning("Synchronizing");
            reader->logSync = false;
        }
    } // Indefinite loop

    // An unrecoverable error occurred
    log_flush_error();

    return status;
}

#if 0
int nbs_getFrame(
        const int fd,
        uint8_t   buf,
        size_t    size)
{
    int    status;
    size_t have = 0;       ///< Number of bytes in buffer
    bool   logSync = true; ///< Log synchronizing message?

    for (;;) {
        // Ensure buffer contains possible frame header
        size_t  need = NBS_FH_SIZE;
        if ((status = ensureBytes(fd, buf, sizeof(buf), need, &have)) !=
                NBS_SUCCESS) {
            log_add("Couldn't read frame header");
            if (status != NBS_SPACE)
                break;
        }

        // Decode and verify frame header
        NbsFrameHeader fh;
        if (nbs_decodeFH(buf, have, &fh) != 0) {
            nbs_logFH(&fh);
            log_add("Invalid frame header");
        }
        else {
            // Read rest of frame header
            need = fh.size;
            if ((status = ensureBytes(fd, buf, sizeof(buf), need, &have)) !=
                    NBS_SUCCESS) {
                log_add("Couldn't read rest of frame header");
                if (status != NBS_SPACE)
                    break;
            }

            // Ensure buffer contains possible product-definition header
            need = fh.size + NBS_PDH_SIZE ;
            if ((status = ensureBytes(fd, buf, sizeof(buf), need, &have)) !=
                    NBS_SUCCESS) {
                log_add("Couldn't read product-definition header");
                if (status != NBS_SPACE)
                    break;
            }

            // Decode and verify product-definition header
            NbsProdDefHeader pdh;
            if (nbs_decodePDH(buf+fh.size, have, &pdh) != 0) {
                nbs_logPDH(&pdh);
                log_add("Invalid product-definition header");
            }
            else {
                // Read rest of product-definition header
                need = fh.size + pdh.size;
                if ((status = ensureBytes(fd, buf, sizeof(buf), need, &have))
                        != NBS_SUCCESS) {
                    log_add("Couldn't read rest of product-definition header");
                    if (status != NBS_SPACE)
                        break;
                }

                if (pdh.pshSize) {
                    // Ensure buffer contains possible product-specific header
                    need = fh.size + pdh.size + pdh.pshSize;
                    if ((status = ensureBytes(fd, buf, sizeof(buf), need,
                            &have)) != NBS_SUCCESS) {
                        log_add("Couldn't read product-specific header");
                        if (status != NBS_SPACE)
                            break;
                    }

                    // Decode and verify product-specific header
                    NbsProdSpecHeader psh;
                    if (nbs_decodePSH(buf+fh.size+pdh.size, have,
                            &pdh, &psh) != 0) {
                        nbs_logPSH(&psh);
                        log_add("Invalid product-specific header");
                    }
                    else {
                        need += pdh.dataBlockSize;
                        if ((status = ensureBytes(fd, buf, sizeof(buf), need,
                                &have)) != NBS_SUCCESS) {
                            log_add("Couldn't read data block");
                            if (status != NBS_SPACE)
                                break;
                        }

                        // Copy frame to slot

                        have = 0;
                        logSync = true;
                        continue;
                    } // Valid PSH
                } // PSH exists
            } // Valid PDH
        } // Valid FH

        // A header is invalid

        // Shift bytes down by one and append a byte
        memmove(buf, buf+1, have-1);
        if ((status = getBytes(fd, buf+have-1, 1)) <= 0) {
            log_add("Couldn't append a byte to buffer");
            status = (status == 0) ? NBS_EOF : NBS_IO;
            break;
        }

        if (logSync) {
            log_flush_debug(); // Logs headers
            log_warning("Synchronizing");
            logSync = false;
        }
    } // Indefinite loop

    // An unrecoverable error occurred
    log_flush_error();

    return status;
}
#endif
