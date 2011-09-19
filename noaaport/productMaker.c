/*
 *   Copyright © 2011, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#include <config.h>

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <limits.h>
#include <unistd.h>

#include <ldm.h>
#include <ulog.h>
#include <pq.h>
#include <md5.h>

#include "log.h"
#include "fifo.h"
#include "ldmProductQueue.h"
#include "nport.h"
#include "productMaker.h"     /* Eat own dog food */

struct productMaker {
    Fifo*                   fifo;           /**< Pointer to FIFO from which to
                                              *  read data */
    LdmProductQueue*        ldmProdQueue;   /**< LDM product-queue into which to
                                              *  put data-products */
    MD5_CTX*                md5ctxp;
    pthread_mutex_t         mutex;          /**< Object access lock */
    unsigned long           npackets;       /**< Number of packets received */
    unsigned long           nmissed;        /**< Number of missed packets */
    unsigned long           nprods;         /**< Number of data-products
                                              *  successfully inserted */
    sbn_struct              sbn;
    pdh_struct              pdh;
    psh_struct              psh;
    pdb_struct              pdb;
    ccb_struct              ccb;
    int                     status;     /**< Termination status */
    unsigned char           buf[10000]; /**< Read buffer */
};

datastore*  ds_alloc(void);

/**
 * Returns a new product-maker.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Usage failure. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
 */
int pmNew(
    Fifo* const             fifo,           /**< [in] Pointer to FIFO from
                                              *  which to get data */
    LdmProductQueue* const  lpq,            /**< [in] LDM product-queue into
                                              *  which to put data-products */
    ProductMaker** const    productMaker)   /**< [out] Pointer to pointer to
                                              *  returned product-maker */
{
    int             status = 2;             /* default failure */
    ProductMaker*   w = (ProductMaker*)malloc(sizeof(ProductMaker));

    if (NULL == w) {
        LOG_SERROR0("Couldn't allocate new product-maker");
    }
    else {
        MD5_CTX*    md5ctxp = new_MD5_CTX();

        if (NULL == md5ctxp) {
            LOG_SERROR0("Couldn't allocate MD5 object");
        }
        else {
            if ((status = pthread_mutex_init(&w->mutex, NULL)) != 0) {
                LOG_ERRNUM0(status, "Couldn't initialize product-maker mutex");
                status = 2;
            }
            else {
                w->fifo = fifo;
                w->ldmProdQueue = lpq;
                w->npackets = 0;
                w->nmissed = 0;
                w->nprods = 0;
                w->md5ctxp = md5ctxp;
                w->status = 0;
                *productMaker = w;
            }
        }
    }

    return status;
}

/**
 * Executes a product-maker.
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @retval (void*)0    The FIFO was closed.
 * @retval (void*)1    Usage failure. \c log_start() called.
 * @retval (void*)2    O/S failure. \c log_start() called.
 */
void* pmStart(
    void* const         arg)          /**< [in/out] Pointer to the
                                        *  product-maker to be executed */
{
    ProductMaker* const productMaker = (ProductMaker*)arg;
    int                 status;
    Fifo* const         fifo = productMaker->fifo;
    unsigned char*      buf = productMaker->buf;
    sbn_struct*         sbn = &productMaker->sbn;
    pdh_struct*         pdh = &productMaker->pdh;
    psh_struct*         psh = &productMaker->psh;
    pdb_struct*         pdb = &productMaker->pdb;
    ccb_struct*         ccb = &productMaker->ccb;
    unsigned long       last_sbn_seqno;
    unsigned long       last_sbn_runno = ULONG_MAX;
    int                 PNGINIT = 0;
    char*               memheap = NULL;
    MD5_CTX*            md5ctxp = productMaker->md5ctxp;
    int                 logResync = 1;
    prodstore           prod;

    prod.head = NULL;
    prod.tail = NULL;

    for (;;) {
        unsigned char       b1;
        long                IOFF;
        int                 NWSTG;
        int                 GOES;
        int                 PROD_COMPRESSED;
        size_t              heapcount;
        size_t              heapsize;
        char                PROD_NAME[1024];
        int                 dataoff;
        int                 datalen;
        datastore*          pfrag;
        int                 nscan;
        int                 deflen;
        static const char*  FOS_TRAILER = "\015\015\012\003";
        int                 cnt;

        /* Look for first byte == 255  and a valid SBN checksum */
        if ((status = fifoRead(fifo, buf, 1)) != 0) {
            if (3 == status)
                status = 0;
            break;
        }
        if ((b1 = (unsigned char)buf[0]) != 255) {
            if (logResync) {
                uinfo("Trying to resync %u", b1);
                logResync = 0;
            }
            continue;
        }
        logResync = 1;

        if (fifoRead(fifo, buf + 1, 15) != 0) {
            if (ulogIsDebug())
                udebug("couldn't read 16 bytes for sbn");
            continue;
        }

        while ((status = readsbn(buf, sbn)) != 0) {
            if (ulogIsDebug())
                udebug("Not SBN start");

            IOFF = 1;

            while ((IOFF < 16) && 
                    ((b1 = (unsigned char) buf[IOFF]) != 255))
                IOFF++;

            if (IOFF > 15) {
                break;
            }
            else {
                int ch;

                for (ch = IOFF; ch < 16; ch++)
                    buf[ch - IOFF] = buf[ch];

                if (fifoRead(fifo, buf + 16 - IOFF, IOFF)
                        != 0) {
                    if (ulogIsDebug())
                        udebug("Couldn't read bytes for SBN, resync");
                    break;
                }
            }
        }

        if (status != 0) {
            if (ulogIsDebug())
                udebug("SBN status continue");
            continue;
        }

        IOFF = 0;

        if (fifoRead(fifo, buf + 16, 16) != 0) {
            if (ulogIsDebug())
                udebug("error reading Product Definition Header");
            continue;
        }

        if (ulogIsDebug())
            udebug("***********************************************");
        if (last_sbn_runno != sbn->runno) {
            last_sbn_runno = sbn->runno;
        }
        else {
            unsigned long   delta = sbn->seqno - last_sbn_seqno;
#           define          MAX_SEQNO 0xFFFFFFFFu

            if (0 == delta || MAX_SEQNO/2 < delta) {
                uwarn("Retrograde packet number: previous=%lu, latest=%lu, "
                        "difference=%lu", last_sbn_seqno, sbn->seqno, 
                        0 == delta ? 0ul : MAX_SEQNO - delta + 1);
            }
            else {
                if (1 != delta) {
                    unsigned long   gap = delta - 1;

                    uwarn("Gap in packet sequence: %lu to %lu [skipped %lu]",
                             last_sbn_seqno, sbn->seqno, gap);

                    (void)pthread_mutex_lock(&productMaker->mutex);
                    productMaker->nmissed += gap;
                    (void)pthread_mutex_unlock(&productMaker->mutex);
                }

                (void)pthread_mutex_lock(&productMaker->mutex);
                productMaker->npackets++;
                (void)pthread_mutex_unlock(&productMaker->mutex);
            }                           /* non-retrograde packet number */
        }                               /* "last_sbn_seqno" initialized */
        last_sbn_seqno = sbn->seqno;

        if (ulogIsVerbose())
            uinfo("SBN seqnumber %ld", sbn->seqno);
        if (ulogIsVerbose())
            uinfo("SBN datastream %d command %d", sbn->datastream,
                sbn->command);
        if (ulogIsDebug())
            udebug("SBN version %d length offset %d", sbn->version, sbn->len);
        if (((sbn->command != 3) && (sbn->command != 5)) || 
                (sbn->version != 1)) {
            uerror("Unknown sbn command/version %d PUNT", sbn->command);
            continue;
        }

        switch (sbn->datastream) {
        case 7:       /* test */
        case 6:       /* was reserved...now nwstg2 */
        case 5:
            NWSTG = 1;
            GOES = 0;
            break;
        case 1:
        case 2:
        case 4:
            NWSTG = 0;
            GOES = 1;
            break;
        default:
            uerror("Unknown NOAAport channel %d PUNT", sbn->datastream);
            continue;
        }

        /* End of SBN version low 4 bits */

        if (readpdh(buf + IOFF + sbn->len, pdh) == -1) {
            uerror("problem with pdh, PUNT");
            continue;
        }
        if (pdh->len > 16) {
            if (fifoRead(fifo, buf + sbn->len + 16,
                        pdh->len - 16) != 0)
                continue;
        }

        if (ulogIsDebug())
            udebug("Product definition header version %d pdhlen %d",
                pdh->version, pdh->len);

        if (pdh->version != 1) {
            uerror("Error: PDH transfer type %u, PUNT", pdh->transtype);
            continue;
        }
        else if (ulogIsDebug()) {
            udebug("PDH transfer type %u", pdh->transtype);
        }

        if ((pdh->transtype & 8) > 0)
            uerror("Product transfer flag error %u", pdh->transtype);
        if ((pdh->transtype & 32) > 0)
            uerror("Product transfer flag error %u", pdh->transtype);

        if ((pdh->transtype & 16) > 0) {
            PROD_COMPRESSED = 1;

            if (ulogIsDebug())
                udebug("Product transfer flag compressed %u", pdh->transtype);
        }
        else {
            PROD_COMPRESSED = 0;
        }

        if (ulogIsDebug())
            udebug("header length %ld [pshlen = %d]", pdh->len + pdh->pshlen,
                pdh->pshlen);
        if (ulogIsDebug())
            udebug("blocks per record %ld records per block %ld\n",
                pdh->blocks_per_record, pdh->records_per_block);
        if (ulogIsDebug())
            udebug("product seqnumber %ld block number %ld data block size "
                "%ld", pdh->seqno, pdh->dbno, pdh->dbsize);

        /* Stop here if no psh */
        if ((pdh->pshlen == 0) && (pdh->transtype == 0)) {
            IOFF = IOFF + sbn->len + pdh->len;
            continue;
        }

        if (pdh->pshlen != 0) {
            if (fifoRead(fifo, buf + sbn->len + pdh->len,
                        pdh->pshlen) != 0) {
                uerror("problem reading psh");
                continue;
            }
            else {
                if (ulogIsDebug())
                    udebug("read psh %d", pdh->pshlen);
            }

            /* Timing block */
            if (sbn->command == 5) {
                if (ulogIsDebug())
                    udebug("Timing block recieved %ld %ld\0", psh->olen,
                        pdh->len);
                /*
                 * Don't step on our psh of a product struct of prod in
                 * progress.
                 */
                continue;
            }

            if (readpsh(buf + IOFF + sbn->len + pdh->len, psh) == -1) {
                uerror("problem with readpsh");
                continue;
            }
            if (psh->olen != pdh->pshlen) {
                uerror("ERROR in calculation of psh len %ld %ld", psh->olen,
                    pdh->len);
                continue;
            }
            if (ulogIsDebug())
                udebug("len %ld", psh->olen);
            if (ulogIsDebug())
                udebug("product header flag %d, version %d", psh->hflag,
                    psh->version);
            if (ulogIsDebug())
                udebug("prodspecific data length %ld", psh->psdl);
            if (ulogIsDebug())
                udebug("bytes per record %ld", psh->bytes_per_record);
            if (ulogIsDebug())
                udebug("Fragments = %ld category %d ptype %d code %d",
                    psh->frags, psh->pcat, psh->ptype, psh->pcode);
            if (psh->frags < 0)
                uerror("check psh->frags %d", psh->frags);
            if (psh->origrunid != 0)
                uerror("original runid %d", psh->origrunid);
            if (ulogIsDebug())
                udebug("next header offset %ld", psh->nhoff);
            if (ulogIsDebug())
                udebug("original seq number %ld", psh->seqno);
            if (ulogIsDebug())
                udebug("receive time %ld", psh->rectime);
            if (ulogIsDebug())
                udebug("transmit time %ld", psh->transtime);
            if (ulogIsDebug())
                udebug("run ID %ld", psh->runid);
            if (ulogIsDebug())
                udebug("original run id %ld", psh->origrunid);
            if (prod.head != NULL) {
                uerror("OOPS, start of new product [%ld ] with unfinished "
                    "product %ld", pdh->seqno, prod.seqno);

                ds_free();

                prod.head = NULL;
                prod.tail = NULL;

                if (PNGINIT != 0) {
                    pngout_end();
                    PNGINIT = 0;
                }

                uerror("Product definition header version %d pdhlen %d",
                        pdh->version, pdh->len);
                uerror("PDH transfer type %u", pdh->transtype);

                if ((pdh->transtype & 8) > 0)
                    uerror("Product transfer flag error %u", pdh->transtype);
                if ((pdh->transtype & 32) > 0)
                    uerror("Product transfer flag error %u", pdh->transtype);

                uerror("header length %ld [pshlen = %d]",
                    pdh->len + pdh->pshlen, pdh->pshlen);
                uerror("blocks per record %ld records per block %ld",
                    pdh->blocks_per_record, pdh->records_per_block);
                uerror("product seqnumber %ld block number %ld data block "
                    "size %ld", pdh->seqno, pdh->dbno, pdh->dbsize);
                uerror("product header flag %d", psh->hflag);
                uerror("prodspecific data length %ld", psh->psdl);
                uerror("bytes per record %ld", psh->bytes_per_record);
                uerror("Fragments = %ld category %d", psh->frags, psh->pcat);

                if (psh->frags < 0)
                    uerror("check psh->frags %d", psh->frags);
                if (psh->origrunid != 0)
                    uerror("original runid %d", psh->origrunid);

                uerror("next header offset %ld", psh->nhoff);
                uerror("original seq number %ld", psh->seqno);
                uerror("receive time %ld", psh->rectime);
                uerror("transmit time %ld", psh->transtime);
                uerror("run ID %ld", psh->runid);
                uerror("original run id %ld", psh->origrunid);
            }

            prod.seqno = pdh->seqno;
            prod.nfrag = psh->frags;

            ds_init(prod.nfrag);

            /* NWSTG CCB = dataoff, WMO = dataoff + 24 */

            if (fifoRead(fifo, buf + sbn->len + pdh->len + 
                        pdh->pshlen, pdh->dbsize) != 0) {
                uerror("problem reading datablock");
                continue;
            }
            if (sbn->datastream == 4) {
                if (psh->pcat != 3) {
                    GOES = 0;
                    NWSTG = 1;
                }
            }

            heapcount = 0;

            MD5Init(md5ctxp);

            if (GOES == 1) {
                if (readpdb(buf + IOFF + sbn->len + pdh->len + 
                            pdh->pshlen,
                        psh, pdb, PROD_COMPRESSED, pdh->dbsize) == -1) {
                    uerror("Error reading pdb, punt");
                    continue;
                }

                (void)memcpy(PROD_NAME, psh->pname, sizeof(PROD_NAME));

                if (ulogIsDebug())
                    udebug("Read GOES %d %d %d [%d] %d", sbn->len, pdh->len,
                        pdh->pshlen, sbn->len + pdh->len + pdh->pshlen,
                        pdb->len);

                /* Data starts at first block after pdb */
                ccb->len = 0;
                heapsize = prodalloc(psh->frags, 5152, &memheap);
            }
            if (NWSTG == 1) {
                memset(psh->pname, 0, sizeof(psh->pname));

                if (readccb(buf + IOFF + sbn->len + pdh->len + 
                            pdh->pshlen,
                        ccb, psh, pdh->dbsize) == -1)
                    uerror("Error reading ccb, using default name");
                if (ulogIsDebug())
                    udebug("look at ccb start %d %d", ccb->b1, ccb->len);

                if (ulogIsVerbose())
                    uinfo("%s", psh->pname);

                memcpy(PROD_NAME, psh->pname, sizeof(PROD_NAME));

                heapsize = prodalloc(psh->frags, 4000 + 15, &memheap);
                /*
                 * We will only compute md5 checksum on the data, 11 FOS
                 * characters at start
                 */
                sprintf(memheap, "\001\015\015\012%03d\040\015\015\012",
                    ((int) pdh->seqno) % 1000);

                heapcount += 11;

                if (psh->metaoff > 0)
                    psh->metaoff = psh->metaoff + 11;
            }
        }
        else {
            /* If a continuation record...don't let psh->pcat get missed */
            if ((sbn->datastream == 4) && (psh->pcat != 3)) {
                GOES = 0;
                NWSTG = 1;
            }

            ccb->len = 0;

            if (ulogIsDebug())
                udebug("continuation record");
            if ((pdh->transtype & 4) > 0) {
                psh->frags = 0;
            }
            if (fifoRead(fifo, buf + sbn->len + pdh->len + 
                        pdh->pshlen, pdh->dbsize) != 0) {
                uerror("problem reading datablock (cont)");
                continue;
            }
            if (prod.head == NULL) {
                if (ulogIsVerbose())
                    uinfo("found data block before header, "
                        "skipping sequence %d frag #%d", pdh->seqno, pdh->dbno);
                continue;
            }
        }

        /* Get the data */
        dataoff = IOFF + sbn->len + pdh->len + pdh->pshlen + ccb->len;
        datalen = pdh->dbsize - ccb->len;

        if (ulogIsDebug())
            udebug("look at datalen %d", datalen);

        pfrag = ds_alloc();
        pfrag->seqno = pdh->seqno;
        pfrag->fragnum = pdh->dbno;
        pfrag->recsiz = datalen;
        pfrag->offset = heapcount;
        pfrag->next = NULL;

        if (GOES == 1) {
            if (pfrag->fragnum > 0) {
                if ((pfrag->fragnum != prod.tail->fragnum + 1) || 
                        (pfrag->seqno != prod.seqno)) {
                    uerror("Missing GOES fragment in sequence, "
                        "last %d/%d this %d/%d\0", prod.tail->fragnum,
                        prod.seqno, pfrag->fragnum, pfrag->seqno);
                    ds_free();

                    prod.head = NULL;
                    prod.tail = NULL;

                    continue;
                }

                if ((PNGINIT != 1) && (!PROD_COMPRESSED)) {
                    uerror("failed pnginit %d %d %s", sbn->datastream,
                            psh->pcat, PROD_NAME);
                    continue;
                }
                if (pdh->records_per_block < 1) {
                    uerror("records_per_block %d blocks_per_record %d "
                        "nx %d ny %d", pdh->records_per_block,
                        pdh->blocks_per_record, pdb->nx, pdb->ny);
                    uerror("source %d sector %d channel %d", pdb->source,
                        pdb->sector, pdb->channel);
                    uerror("nrec %d recsize %d date %02d%02d%02d %02d%02d "
                        "%02d.%02d", pdb->nrec, pdb->recsize, pdb->year,
                        pdb->month, pdb->day, pdb->hour, pdb->minute,
                        pdb->second, pdb->sechunds);
                    uerror("pshname %s", psh->pname);
                }
                if (!PROD_COMPRESSED) {
                    for (nscan = 0; (nscan * pdb->nx) < pdh->dbsize; nscan++) {
                        if (ulogIsDebug())
                            udebug("png write nscan %d", nscan);
                        if (nscan >= pdh->records_per_block) {
                            uerror("nscan exceeding records per block %d [%d "
                                "%d %d]", pdh->records_per_block, nscan,
                                pdb->nx, pdh->dbsize);
                        }
                        else {
                          pngwrite(buf + dataoff + (nscan * pdb->nx));
                        }
                    }
                }
                else {
                    memcpy(memheap + heapcount, buf + dataoff, datalen);
                    MD5Update(md5ctxp, (unsigned char *) (memheap + heapcount),
                        datalen);
                    heapcount += datalen;
                }
            }
            else {
                if (!PROD_COMPRESSED) {
                    png_set_memheap(memheap, md5ctxp);
                    png_header(buf + dataoff, datalen);
                    /*
                     * Add 1 to number of scanlines, image ends with 
                     * f0f0f0f0...
                     */
                    pngout_init(pdb->nx, pdb->ny + 1);

                    PNGINIT = 1;
                }
                else {
                    memcpy(memheap + heapcount, buf + dataoff, datalen);
                    MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount),
                        datalen);
                    heapcount += datalen;
                }
                unotice("records_per_block %d blocks_per_record %d nx %d ny %d",
                    pdh->records_per_block, pdh->blocks_per_record, pdb->nx,
                    pdb->ny);
                unotice("source %d sector %d channel %d", pdb->source,
                    pdb->sector, pdb->channel);
                unotice("nrec %d recsize %d date %02d%02d%02d %02d%02d "
                    "%02d.%02d", pdb->nrec, pdb->recsize, pdb->year, pdb->month,
                    pdb->day, pdb->hour, pdb->minute, pdb->second,
                    pdb->sechunds);
                unotice("pshname %s", psh->pname);
            }
            deflen = 0;
        }
        else {
            /* If the product already has a FOS trailer, don't add
             * another....this will match what pqing(SDI) sees
             */
            if ((prod.nfrag != 0) && (prod.tail != NULL)) {
                if ((pfrag->fragnum != prod.tail->fragnum + 1) ||
                        (pfrag->seqno != prod.seqno)) {
                    uerror("Missing fragment in sequence, last %d/%d this "
                        "%d/%d\0", prod.tail->fragnum, prod.seqno,
                        pfrag->fragnum, pfrag->seqno);
                    ds_free();

                    prod.head = NULL;
                    prod.tail = NULL;

                    continue;
                }
            }
            if ((prod.nfrag == 0) || (prod.nfrag == (pfrag->fragnum + 1))) {
                char testme[4];

                while (datalen > 4) {
                    memcpy(testme, buf + (dataoff + datalen - 4), 4);

                    if (memcmp(testme, FOS_TRAILER, 4) == 0) {
                        datalen -= 4;

                        if (ulogIsDebug())
                            udebug("removing FOS trailer from %s", PROD_NAME);
                    }
                    else {
                        break;
                    }
                }
            }
            if (heapcount + datalen > heapsize) {
                /*
                 * this above wasn't big enough heapsize =
                 * prodalloc(psh->frags,4000+15,&memheap);
                 */
                uerror("Error in heapsize %d product size %d [%d %d], Punt!\0",
                    heapsize, (heapcount + datalen), heapcount, datalen);
                continue;
            }

            memcpy(memheap + heapcount, buf + dataoff, datalen);

            deflen = datalen;

            MD5Update(md5ctxp, (unsigned char *) (memheap + heapcount),
                deflen);
        }

        pfrag->recsiz = deflen;
        heapcount += deflen;

        if (prod.head == NULL) {
            prod.head = pfrag;
            prod.tail = pfrag;
        }
        else {
            prod.tail->next = pfrag;
            prod.tail = pfrag;
        }

        if ((prod.nfrag == 0) || (prod.nfrag == (pfrag->fragnum + 1))) {
            if (GOES == 1) {
                if (PNGINIT == 1) {
                    pngout_end();
                    heapcount = png_get_prodlen();
                }
                else {
                    if (ulogIsDebug())
                        udebug("GOES product already compressed %d", heapcount);
                }
            }
            if (ulogIsVerbose())
              uinfo("we should have a complete product %ld %ld/%ld %ld /heap "
                  "%ld", prod.seqno, pfrag->seqno, prod.nfrag, pfrag->fragnum,
                 (long) heapcount);
            if ((NWSTG == 1) && (heapcount > 4)) {
                cnt = 4;                /* number of bytes to add for TRAILER */

                /*
                 * Do a DDPLUS vs HDS check for NWSTG channel only
                 */
                if (sbn->datastream == 5) {
                    /* nwstg channel */
                    switch (psh->pcat) {
                    case 1:
                    case 7:
                      /* Do a quick check for non-ascii text products */
                      if (!prod_isascii(PROD_NAME, memheap, heapcount))
                        psh->pcat += 100;       /* call these HDS */
                      break;
                    }
                }

                if (cnt > 0) {
                    memcpy(memheap + heapcount, FOS_TRAILER + 4 - cnt, cnt);
                    MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount),
                        cnt);
                    heapcount += cnt;
                }
            }

            process_prod(prod, PROD_NAME, memheap, heapcount,
                md5ctxp, productMaker->ldmProdQueue, psh, sbn);
            ds_free();

            prod.head = NULL;
            prod.tail = NULL;
            PNGINIT = 0;

            (void)pthread_mutex_lock(&productMaker->mutex);
            productMaker->nprods++;
            (void)pthread_mutex_unlock(&productMaker->mutex);
        }
        else {
            if (ulogIsDebug())
                udebug("processing record %ld [%ld %ld]", prod.seqno,
                    prod.nfrag, pfrag->fragnum);
            if ((pdh->transtype & 4) > 0) {
                uerror("Hmmm....should call completed product %ld [%ld %ld]",
                    prod.seqno, prod.nfrag, pfrag->fragnum);
            }
        }

        IOFF += (sbn->len + pdh->len + pdh->pshlen + pdh->dbsize);

        if (ulogIsDebug())
            udebug("look IOFF %ld datalen %ld (deflate %ld)", IOFF, datalen,
                deflen);
    }

    if (NULL != memheap)
        free(memheap);

    productMaker->status = status;

    return NULL;
}

/**
 * Returns statistics since the last time this function was called or \link
 * pmStart() \endlink was called.
 */
void pmGetStatistics(
    ProductMaker* const     productMaker,       /**< [in] Pointer to the
                                                  *  product-maker */
    unsigned long* const    packetCount,        /**< [out] Number of packets */
    unsigned long* const    missedPacketCount,  /**< [out] Number of missed
                                                  *  packets */
    unsigned long* const    prodCount)          /**< [out] Number of products 
                                                  *  inserted into the
                                                  *  product-queue */
{
    (void)pthread_mutex_lock(&productMaker->mutex);

    *packetCount = productMaker->npackets;
    *missedPacketCount = productMaker->nmissed;
    *prodCount = productMaker->nprods;

    productMaker->npackets = 0;
    productMaker->nmissed = 0;
    productMaker->nprods = 0;

    (void)pthread_mutex_unlock(&productMaker->mutex);
}

/**
 * Returns the termination status of a product-maker
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @retval 0    The FIFO was closed.
 * @retval 1    Usage failure. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
 */
int pmStatus(
    ProductMaker* const productMaker)   /**< [in] Pointer to the product-maker
                                          */
{
    return productMaker->status;
}
