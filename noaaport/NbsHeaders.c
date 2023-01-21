/**
 * This file implements an API for dumping NOAAPort headers. The headers are
 * frame header, product-definition header, and product-specific header.
 * DumpHeaders.c
 *
 *  Created on: Jan 29, 2022
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "NbsHeaders.h"

#include "log.h"

#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>

int nbs_decodeFH(
        const uint8_t* buf,
        const size_t   nbytes,
        NbsFH* const   fh)
{
    int status = EINVAL;

    if (buf == NULL || fh == NULL) {
        log_add("NULL argument");
    }
    else if (nbytes < 16) {
        log_add("Frame header is too small: %zu bytes", nbytes);
    }
    else {
        status = EBADMSG;

        fh->hdlcAddress = buf[0];
        if (fh->hdlcAddress != 255) {
            log_add("255 sentinel isn't present");
        }
        else {
            fh->hdlcControl = buf[1];
            fh->version = buf[2] >> 4;
            fh->size = (buf[2] & 0xf) * 4;
            if (fh->size != 16) {
                log_add("Frame header size (%u bytes) != 16 bytes");
            }
            else {
                fh->control = buf[3];
                fh->command = buf[4];
                fh->datastream = buf[5];
                fh->source = buf[6];
                fh->destination = buf[7];
                fh->seqno = ntohl(*(uint32_t*)(buf+8));
                fh->runno = ntohs(*(uint16_t*)(buf+12));
                fh->checksum = ntohs(*(uint16_t*)(buf+14));

                unsigned sum = 0;
                for (int i = 0; i < 14; ++i)
                    sum += buf[i];
                if (sum != fh->checksum) {
                    log_add("Frame sum (%u) != checksum (%u)", sum, fh->checksum);
                }
                else {
                    status = 0;
                }
            }
        }
    }

    return status;
}

void nbs_logFH(const NbsFH* fh)
{
    log_add("Frame Header:\n"
"  hdlcAddress = %#x\n"
"  hdlcControl = %#x\n"
"   FH version = %u\n"
"      FH size = %u bytes\n"
"      control = %#x\n"
"      command = %u\n"
"   datastream = %u\n"
"       source = %u\n"
"  destination = %u\n"
"     FH seqno = %u\n"
"     FH runno = %u\n"
"     checksum = %u",
           fh->hdlcAddress,
           fh->hdlcControl,
           fh->version,
           fh->size,
           fh->control,
           fh->command,
           fh->datastream,
           fh->source,
           fh->destination,
           fh->seqno,
           fh->runno,
           fh->checksum);
}

int nbs_decodePDH(
        const uint8_t* buf,
        const size_t   nbytes,
        const NbsFH*   fh,
        NbsPDH* const  pdh)
{
    int status = EINVAL;

    if (buf == NULL || pdh == NULL) {
        log_add("NULL argument");
    }
    else if (nbytes < 16) {
        log_add("Product-definition header is too small: %zu bytes", nbytes);
    }
    else {
        status = EBADMSG;

        pdh->version = buf[0] >> 4;
        pdh->size = (buf[0] & 0xf) * 4;
        if (pdh->size < 16) {
            log_add("Product-definition header size (%u bytes) < 16 bytes",
                    pdh->size);
        }
        else {
            pdh->transferType = buf[1];
            unsigned totalSize = ntohs(*(uint16_t*)(buf+2));
            if (totalSize < pdh->size) {
                log_add("PDH size + PSH size (%u bytes) < PDH size (%u) bytes",
                        totalSize, pdh->size);
            }
            else {
                pdh->totalSize = totalSize;
                pdh->pshSize = totalSize - pdh->size;

                if (pdh->pshSize == 0 && (fh->command == NBS_FH_CMD_TIME ||
                        pdh->transferType == 0)) {
                    // Ensure proper values in these PDH-s
                    pdh->blockNum = 0;
                    pdh->dataBlockOffset = 0;
                    pdh->dataBlockSize = 0;
                    pdh->recsPerBlock = 0;
                    pdh->blocksPerRec = 0;
                }
                else {
                    pdh->blockNum = ntohs(*(uint16_t*)(buf+4));
                    pdh->dataBlockOffset = ntohs(*(uint16_t*)(buf+6));
                    pdh->dataBlockSize = ntohs(*(uint16_t*)(buf+8));
                    pdh->recsPerBlock = buf[10];
                    pdh->blocksPerRec = buf[11];
                }
                pdh->prodSeqNum = ntohl(*(uint32_t*)(buf+12));

                if (pdh->pshSize && ((pdh->transferType & 1) == 0)) {
                    log_add("Frame isn't start-of-product but PSH "
                            "size is %u bytes", pdh->pshSize);
                }
                else if (pdh->pshSize && ((pdh->transferType & 64) == 0)) {
                    log_add("Product-specific header not specified but PSH "
                            "size is %u bytes", pdh->pshSize);
                }
                else {
                    const unsigned long frameSize = fh->size + pdh->size +
                            pdh->pshSize + pdh->dataBlockSize;
                    if (frameSize > NBS_MAX_FRAME) {
                        log_add("Total specified frame size is too large: "
                                "%u bytes", frameSize);
                    }
                    else {
                        status = 0;
                    }
                } // PSH size is consistent with transfer type
            } // PDH size + PSH size >= PDH size
        } // PDH size > 16 bytes
    } // Have sufficient bytes for PDH

    return status;
}

void nbs_logPDH(const NbsPDH* const pdh)
{
    log_add("Product-Definition Header:\n"
"      PDH version = %u\n"
"         PDH size = %u bytes\n"
"     transferType = %#x\n"
"       total size = %u bytes\n"
"         PSH size = %u bytes\n"
"         blockNum = %u\n"
"  dataBlockOffset = %u bytes\n"
"    dataBlockSize = %u bytes\n"
"     recsPerBlock = %u\n"
"     blocksPerRec = %u\n"
"       prodSeqNum = %u\n",
            pdh->version,
            pdh->size,
            pdh->transferType,
            pdh->totalSize,
            pdh->pshSize,
            pdh->blockNum,
            pdh->dataBlockOffset,
            pdh->dataBlockSize,
            pdh->recsPerBlock,
            pdh->blocksPerRec,
            pdh->prodSeqNum);
}

int nbs_decodePSH(
        const uint8_t* buf,
        const size_t   nbytes,
        const NbsPDH*  pdh,
        NbsPSH* const  psh)
{
    int status = EINVAL;

    if (buf == NULL || pdh == NULL || psh == NULL) {
        log_add("NULL argument");
    }
    else if (nbytes < 36) {
        log_add("Product-specific header is too small: %zu bytes", nbytes);
    }
    else {
        status = EBADMSG;

        psh->optFieldNum = buf[0];
        psh->optFieldType = buf[1];
        psh->size = ntohs(*(uint16_t*)(buf+2));
        if (psh->size != pdh->pshSize) {
            log_add("Product-specific header size (%u bytes) != that in "
                    "product-definition header (%u bytes)", psh->size,
                    pdh->pshSize);
        }
        else {
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

void nbs_logPSH(const NbsPSH* psh)
{
    log_add("Product-Specific Header:\n"
"   optFieldNum = %u\n"
"  optFieldType = %u\n"
"          size = %u bytes\n"
"       version = %u\n"
"          flag = %#x\n"
"     awipsSize = %u bytes\n"
"   bytesPerRec = %u\n"
"          type = %u\n"
"      category = %u\n"
"      prodCode = %u\n"
"      numFrags = %u\n"
"   nextHeadOff = %u\n"
"      reserved = %u\n"
"        source = %u\n"
"        seqNum = %u\n"
"   ncfRecvTime = %u\n"
"   ncfSendTime = %u\n"
"     currRunId = %u\n"
"     origRunId = %u\n",
            psh->optFieldNum,
            psh->optFieldType,
            psh->size,
            psh->version,
            psh->flag,
            psh->awipsSize,
            psh->bytesPerRec,
            psh->type,
            psh->category,
            psh->prodCode,
            psh->numFrags,
            psh->nextHeadOff,
            psh->reserved,
            psh->source,
            psh->seqNum,
            psh->ncfRecvTime,
            psh->ncfSendTime,
            psh->currRunId,
            psh->origRunId);
}

int nbs_logHeaders(
        const uint8_t* buf,
        size_t         nbytes)
{
    int status = EINVAL;

    if (buf == NULL) {
        log_add("NULL argument");
    }
    else {
        NbsFH fh;

        status = nbs_decodeFH(buf, nbytes, &fh);
        if (status) {
            log_add("Invalid frame header");
        }
        else {
            buf += fh.size;
            nbytes -= fh.size;
            nbs_logFH(&fh);

            NbsPDH pdh = {};
            status = nbs_decodePDH(buf, nbytes, &fh, &pdh);
            if (status) {
                log_add("Invalid product-definition header");
            }
            else {
                buf += pdh.size;
                nbytes -= pdh.size;
                nbs_logPDH(&pdh);

                if (pdh.pshSize) {
                    NbsPSH psh;
                    status = nbs_decodePSH(buf, nbytes, &pdh, &psh);
                    if (status) {
                        log_add("Invalid product-specific header");
                    }
                    else {
                        nbs_logPSH(&psh);
                    }
                }
            }
        }
    }

    return status;
}
