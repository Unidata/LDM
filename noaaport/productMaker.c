/*
 *   Copyright © 2016, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#include <config.h>

#include "fifo.h"
#include "goes.h"
#include "ldm.h"
#include "log.h"
#include "md5.h"
#include "nport.h"
#include "pq.h"
#include "productMaker.h"     /* Eat own dog food */

#include <ctype.h> /* Required for character classification routines - isalpha */
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <zlib.h> /* Required for compress/uncompress */

#ifdef RETRANS_SUPPORT
#include "retrans.h"
#include "acq_shm_lib.h"

static ulong       total_prods_handled;    /* prods retrans requested */
static ulong       total_prods_lost_err;
static ulong       total_frame_cnt;
static ulong       total_frame_err;
static int         global_cpio_fd;
static ACQ_TABLE   *global_acq_tbl;
#endif

/*
 * The following functions are declared here because they're not declared in
 * header-files:
 */
extern datastore* ds_alloc(void);
extern void       ds_free();
extern void       pngout_end();
extern void       ds_init(int nfrags);
extern size_t     prodalloc(long int nfrags, long int dbsize, char **heap);
extern void       pngwrite(char *memheap);
extern void       png_set_memheap(char *memheap, MD5_CTX *md5ctxp);
extern void       png_header(char *memheap, int length);
extern void       pngout_init(int width, int height);
extern int        png_get_prodlen();
extern int        prod_isascii(char *pname, char *prod, size_t psize);
extern void       process_prod(
    prodstore                   nprod,
    char*                       PROD_NAME,
    char*                       memheap,
    size_t                      heapsize,
    MD5_CTX*                    md5try,
    LdmProductQueue* const      lpq,
    psh_struct*                 psh,
    sbn_struct*                 sbn);
static int        prod_get_WMO_nnnxxx_offset (char *wmo_buff, int max_search, int *p_len);
static int        prod_get_WMO_offset (char *buf, size_t buflen, size_t *p_wmolen);
static int        getIndex (char *arr, int pos, int sz);
static char*      decode_zlib_err (int err);

#ifdef RETRANS_SUPPORT
extern CPIO_TABLE cpio_tbl;
#endif

struct productMaker {
    Fifo*                   fifo;           /**< Pointer to FIFO from which to
                                              *  read data */
    LdmProductQueue*        ldmProdQueue;   /**< LDM product-queue into which to
                                              *  put data-products */
    MD5_CTX*                md5ctxp;
    pthread_mutex_t         mutex;          /**< Object access lock */
    unsigned long           nframes;        /**< Number of frames received */
    unsigned long           nmissed;        /**< Number of missed frames */
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

extern int      inflateFrame;
extern int      fillScanlines;

/**
 * Returns a new product-maker.
 *
 * This function is thread-safe.
 *
 * @retval 0    Success.
 * @retval 1    Usage failure. \c log_add() called.
 * @retval 2    O/S failure. \c log_add() called.
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
        log_syserr("Couldn't allocate new product-maker");
    }
    else {
        MD5_CTX*    md5ctxp = new_MD5_CTX();

        if (NULL == md5ctxp) {
            log_syserr("Couldn't allocate MD5 object");
            free(w);
        }
        else {
            if ((status = pthread_mutex_init(&w->mutex, NULL)) != 0) {
                log_errno(status, "Couldn't initialize product-maker mutex");
                free(w);
                free_MD5_CTX(md5ctxp);
                status = 2;
            }
            else {
                w->fifo = fifo;
                w->ldmProdQueue = lpq;
                w->nframes = 0;
                w->nmissed = 0;
                w->nprods = 0;
                w->md5ctxp = md5ctxp;
                w->status = 0;
                *productMaker = w;
            }
        } // `md5cctxp` allocated
    } // `w` allocated

    return status;
}

/**
 * Frees a product-maker.
 *
 * @param[in,out] pm  The product-maker to be freed or NULL.
 */
void pmFree(
        ProductMaker* const pm)
{
    if (pm) {
        free_MD5_CTX(pm->md5ctxp);
        free(pm);
    }
}

/**
 * Executes a product-maker.
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @retval (void*)0    The FIFO was closed.
 * @retval (void*)1    Usage failure. \c log_add() called.
 * @retval (void*)2    O/S failure. \c log_add() called.
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
    bool                firstFrameSeen = false;
    unsigned long       last_sbn_seqno = 0; // Change this type carefully
    unsigned long       last_sbn_runno = ULONG_MAX;
    unsigned            prevProdSeqNum;
    unsigned            prevBlockNum;
    int                 PNGINIT = 0;
    char*               memheap = NULL;
    MD5_CTX*            md5ctxp = productMaker->md5ctxp;
    int                 logResync = 1;
    prodstore           prod;
    
    int unCompress = NO; /* By default uncompress is disabled */
    int fillScan = NO;   /* By default scanlines are not filled *
                             * for missing satellite GOES imagery  */

#define CHUNK_SZ 5700
#define MAXBYTES_DATA   5700    
#define PDB_LEN 512
#define DEFAULT_COMPRESSION_LEVEL Z_DEFAULT_COMPRESSION

#define BEGIN_BLK   0
#define ANY_BLK     1
#define END_BLK     2

unsigned long int uncomprLen;
unsigned char uncomprBuf[MAXBYTES_DATA];

unsigned long int comprLen;
unsigned char comprBuf[MAXBYTES_DATA];

unsigned long int comprDataLen;
unsigned char comprDataBuf[CHUNK_SZ];

char  GOES_BLANK_FRAME[MAXBYTES_DATA];
unsigned int GOES_BLNK_FRM_LEN;

size_t wmolen;
int    nxlen;
int    wmo_offset;
int    nnnxxx_offset;

#ifdef RETRANS_SUPPORT
    long cpio_addr_tmp;
    int cpio_fd_tmp;
 /*   char transfer_type[10]={0};*/
    int retrans_val,idx;
    long retrans_tbl_size;
    time_t orig_arrive_time;
    int new_key; /* shm key */
    ACQ_TABLE *acq_tbl = NULL;
    long num_prod_discards=0;
    int save_prod = 1;
    int discard_prod = 0;
    long proc_orig_prod_seqno_last_save=0;
    int genRetransReq = 0;
#endif

    prod.head = NULL;
    prod.tail = NULL;

    unCompress = inflateFrame;
    fillScan = fillScanlines;
    memset(GOES_BLANK_FRAME, 0, MAXBYTES_DATA);

        /*** For Retranmission Purpose  ***/
#ifdef RETRANS_SUPPORT
    log_debug(" retrans_xmit_enable [%d]   transfer_type [%s] sbn_channel_name [%s] "
            " unCompress [%d] fillScan [%d]\n", retrans_xmit_enable,
            transfer_type, sbn_channel_name, unCompress, fillScan);

       if((retrans_xmit_enable == OPTION_ENABLE) && (!strcmp(transfer_type,"MHS") || !strcmp(transfer_type,"mhs"))){
                idx = get_cpio_addr(mcastAddr);
                if( idx >= 0 && idx < NUM_CPIO_ENTRIES){
                    global_cpio_fd = cpio_tbl[idx].cpio_fd;
                    global_cpio_addr = cpio_tbl[idx].cpio_addr;
                    log_debug("Global cpio_addr  = 0x%x Global cpio_fd = %d \n",
                            global_cpio_addr,global_cpio_fd);
                }
                else{
                    log_error_q("Invalid multicast address provided");
                    status = -1;
                    return NULL;        
                }

                 retrans_tbl_size = sizeof(PROD_RETRANS_TABLE);

                /** Modified to setup retrans table on per channel basis - Sathya - 14-Mar'2013 **/

                 retrans_tbl_size += (sizeof(PROD_RETRANS_ENTRY) * GET_RETRANS_CHANNEL_ENTRIES(sbn_type));

                /****   
                 retrans_tbl_size += (sizeof(PROD_RETRANS_ENTRY) * DEFAULT_RETRANS_ENTRIES_NMC);
                 retrans_tbl_size += (sizeof(PROD_RETRANS_ENTRY) * DEFAULT_RETRANS_ENTRIES_NMC1);
                 retrans_tbl_size += (sizeof(PROD_RETRANS_ENTRY) * DEFAULT_RETRANS_ENTRIES_NMC2);
                 retrans_tbl_size += (sizeof(PROD_RETRANS_ENTRY) * DEFAULT_RETRANS_ENTRIES_NMC3);
                 retrans_tbl_size += (sizeof(PROD_RETRANS_ENTRY) * DEFAULT_RETRANS_ENTRIES_GOES_EAST);
                 retrans_tbl_size += (sizeof(PROD_RETRANS_ENTRY) * DEFAULT_RETRANS_ENTRIES_NOAAPORT_OPT);
                ****/


                p_prod_retrans_table = (PROD_RETRANS_TABLE *) malloc (retrans_tbl_size);
                if(p_prod_retrans_table == NULL){
                   log_error_q("Unable to allocate memory for retrans table..Quitting.\n");
                   status = -1;
                   return NULL; 
                }

                if( init_retrans(&p_prod_retrans_table) < 0 ){
                    log_error_q("Error in initializing retrans table \n");
                    if (p_prod_retrans_table)
                                free(p_prod_retrans_table);

                    status = -1;
                    return NULL;
                }       

       GET_SHMPTR(global_acq_tbl,ACQ_TABLE,ACQ_TABLE_SHMKEY,DEBUGGETSHM);

        log_debug("Global acquisition table = 0x%x cpio_fd = %d \n",global_acq_tbl,global_cpio_fd);
        acq_tbl = &global_acq_tbl[global_cpio_fd];

        log_debug("Obtained acquisition table = 0x%x \n",acq_tbl);
        
         buff_hdr = (BUFF_HDR *) malloc(sizeof(BUFF_HDR));
         if(init_buff_hdr(buff_hdr) < 0){
            log_error_q("Unable to initialize buffer header \n");
            free(acq_tbl); // NULL safe
            free(p_prod_retrans_table); // NULL safe

            status = -1;
            return NULL;
         }

         acq_tbl->pid = getpid();
         acq_tbl->link_id = global_cpio_fd;
         acq_tbl->link_addr = global_cpio_addr;
        log_info_q("Initialized acq_tbl  = 0x%x & buff_hdr = 0x%x pid = %d \n",acq_tbl,buff_hdr,acq_tbl->pid);
        log_info_q("Global link id = %d  Global link addr = %ld \n",acq_tbl->link_id,acq_tbl->link_addr);
        log_info_q("acq_tbl->read_distrib_enable = 0x%x \n",acq_tbl->read_distrib_enable);
    }
           /*** For Retranmission Purpose  ***/
#endif
        
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
        static const char*  FOS_TRAILER = "\015\015\012\003"; // CR CR LF ETX
        int                 cnt;
        sbn_struct          saved_sbn_struct;
        pdh_struct          saved_pdh_struct;
        psh_struct          saved_psh_struct;
        pdb_struct          saved_pdb_struct;
        int                 saved_nfrags;
        int                 frags_left;
        int                 n_scanlines;

        /* Look for first byte == 255  and a valid SBN checksum */
        if ((status = fifo_getBytes(fifo, buf, 1)) != 0) {
            if (3 == status)
                status = 0;
            break;
        }
        if ((b1 = (unsigned char)buf[0]) != 255) {
            if (logResync) {
                log_info_q("Trying to resync %u", b1);
                logResync = 0;
            }
            continue;
        }
        logResync = 1;

        if (fifo_getBytes(fifo, buf + 1, 15) != 0) {
            log_debug("couldn't read 16 bytes for sbn");
            continue;
        }

        while ((status = readsbn((char*)buf, sbn)) != 0) {
            log_debug("Not SBN start");

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

                if (fifo_getBytes(fifo, buf + 16 - IOFF, IOFF) != 0) {
                    log_debug("Couldn't read bytes for SBN, resync");
                    break;
                }
            }
        }

        if (status != 0) {
            log_debug("SBN status continue");
            continue;
        }

        IOFF = 0;

        if (fifo_getBytes(fifo, buf + 16, 16) != 0) {
            log_debug("error reading Product Definition Header");
            continue;
        }

#ifdef RETRANS_SUPPORT
                /* Update acq table stats - Begin */
                if(retrans_xmit_enable == OPTION_ENABLE){
                        buff_hdr->read_channel_type = sbn->datastream;
                }
                /* Update acq table stats - End */
#endif

#define          MAX_SEQNO 0xFFFFFFFFu

        log_debug("***********************************************");

        log_info_q("SBN seqnumber %ld", sbn->seqno);
        log_info_q("SBN datastream %d command %d", sbn->datastream, sbn->command);
        log_debug("SBN version %d length offset %d", sbn->version, sbn->len);

        if (((sbn->command != SBN_CMD_DATA) && (sbn->command != SBN_CMD_TIME)) ||
                (sbn->version != 1)) {
            log_error_q("Unknown sbn command/version %d PUNT", sbn->command);
            continue;
        }

        GOES = 0;
        NWSTG = 0;

        switch (sbn->datastream) {
            case SBN_CHAN_GOES:         /* GINI GOES */
            case SBN_CHAN_NMC4:         /* GINI GOES (deprecated) */
            case SBN_CHAN_NOAAPORT_OPT: /* OCONUS */
                GOES = 1;
                break;

            case SBN_CHAN_NMC1:         /* NWSTG1 - not used */
            case SBN_CHAN_NMC:          /* NWSTG */
            case SBN_CHAN_NMC2:         /* NWSTG2 */
            case SBN_CHAN_NMC3:         /* POLARSAT */
            case SBN_CHAN_NWWS:         /* NOAA Weather Wire Service (NWWS) */
            case SBN_CHAN_ADD:          /* Reserved for NWS internal use - Data Delivery */
            case SBN_CHAN_ENC:          /* Reserved for NWS internal use - Encrypted */
            case SBN_CHAN_EXP:          /* Reserved for NWS internal use - Experimental */
            case SBN_CHAN_GRW:          /* GOES-R West */
            case SBN_CHAN_GRE:          /* GOES-R East */
                NWSTG = 1;
                break;

            default:
                log_error_q("Unknown NOAAport channel %d PUNT", sbn->datastream);
                continue;
        }

        /* End of SBN version low 4 bits */

        // Parse the product definition header into `pdh`
        if (readpdh((char*)buf + IOFF + sbn->len, pdh) == -1) {
            log_error_q("problem with pdh, PUNT");
            continue;
        }
        // Read excess of product-definition header if it exists
        if (pdh->len > 16) {
            if (fifo_getBytes(fifo, buf + sbn->len + 16, pdh->len - 16) != 0)
                continue;
        }

        log_debug("Product definition header version %d pdhlen %d",
                pdh->version, pdh->len);

        if (pdh->version != 1) {
            log_error_q("Error: PDH transfer type %u, PUNT", pdh->transtype);
            continue;
        }

        log_debug("PDH transfer type %u", pdh->transtype);

        if ((pdh->transtype & 8) > 0)
            log_error_q("Product transfer flag error %u", pdh->transtype);
        if ((pdh->transtype & 32) > 0)
            log_error_q("Product transfer flag error %u", pdh->transtype);

        if ((pdh->transtype & 16) > 0) {
            PROD_COMPRESSED = 1;
            log_debug("Product transfer flag compressed %u", pdh->transtype);
        }
        else {
            PROD_COMPRESSED = 0;
        }

        log_debug("header length %ld [pshlen = %d]", pdh->len + pdh->pshlen, pdh->pshlen);
        log_debug("blocks per record %ld records per block %ld\n", pdh->blocks_per_record,
                pdh->records_per_block);
        log_debug("product seqnumber %ld block number %d data block size %d", pdh->seqno,
                pdh->dbno, pdh->dbsize);

        /*
         * Stop here if no psh.
         *
         * This will be true for synchronization frames (NbsFH::command == 5) -- SRE 2022-03-30
         */
        if ((pdh->pshlen == 0) && (pdh->transtype == 0)) {
            // IOFF = IOFF + sbn->len + pdh->len; // `IOFF` value isn't used
            continue;
        }

        if (!firstFrameSeen) {
            firstFrameSeen = true;
        }
        else {
            const uint32_t delta = sbn->seqno - last_sbn_seqno;

            if (delta == 0 || MAX_SEQNO/2 < delta) {
                log_warning_q("Retrograde packet number: previous=%lu, latest=%lu, "
                        "difference=%" PRIu32, last_sbn_seqno, sbn->seqno, delta);
            }
            else {
                const uint32_t nmissed = delta - 1;
                if (nmissed) {
                    if ((pdh->seqno == prevProdSeqNum && pdh->dbno == prevBlockNum + 1)
                            || (pdh->seqno == prevProdSeqNum + 1 && pdh->dbno == 0)) {
                        log_debug("%" PRIu32 " non-data frame(s) missed", nmissed);
                    }
                    else {
                        log_add("Gap in packet sequence: %lu to %lu [skipped %" PRIu32 "]",
                                 last_sbn_seqno, sbn->seqno, nmissed);
                        log_add("prevProdSeqNum=%u, pdh->seqno=%ld, prevBlockNum=%u, pdh->dbno=%d",
                                prevProdSeqNum, pdh->seqno, prevBlockNum, pdh->dbno);
                        log_flush_warning();
                        (void)pthread_mutex_lock(&productMaker->mutex);
                        productMaker->nmissed += nmissed;
                        (void)pthread_mutex_unlock(&productMaker->mutex);
                    }
                }

                (void)pthread_mutex_lock(&productMaker->mutex);
                productMaker->nframes++;
                (void)pthread_mutex_unlock(&productMaker->mutex);
            }
        }
        last_sbn_seqno = sbn->seqno;
        prevProdSeqNum = pdh->seqno;
        prevBlockNum = pdh->dbno;

#ifdef RETRANS_SUPPORT
                /** Update acquisition table statistics  **/
                if(retrans_xmit_enable == OPTION_ENABLE){
                                acq_tbl->read_tot_buff_read++;
                }
#endif
        if (pdh->pshlen != 0) {
            if (fifo_getBytes(fifo, buf + sbn->len + pdh->len, pdh->pshlen) != 0) {
                        log_error_q("problem reading psh");
                continue;
            }
            log_debug("read psh %d", pdh->pshlen);

            /* Timing block */
            if (sbn->command == SBN_CMD_TIME) {
                log_debug("Timing block received %ld %ld\0", psh->olen,
                        pdh->len);
                /*
                 * Don't step on our psh of a product struct of prod in
                 * progress.
                 */
                continue;
            }

            // Parse the product-specific header
            if (readpsh((char*)buf + IOFF + sbn->len + pdh->len, psh) == -1) {
                log_error_q("problem with readpsh");
                continue;
            }
            if (psh->olen != pdh->pshlen) {
                log_error_q("ERROR in calculation of psh len %ld %ld", psh->olen,
                    pdh->len);
                continue;
            }
            log_debug("len %ld", psh->olen);
            log_debug("product header flag %d, version %d", psh->hflag, psh->version);
            log_debug("prodspecific data length %ld", psh->psdl);
            log_debug("bytes per record %ld", psh->bytes_per_record);
            log_debug("Fragments = %ld category %d ptype %d code %d",
                    psh->frags, psh->pcat, psh->ptype, psh->pcode);
            if (psh->frags < 0)
                log_error_q("check psh->frags %d", psh->frags);
            if (psh->origrunid != 0)
                log_error_q("original runid %d", psh->origrunid);
            log_debug("next header offset %ld", psh->nhoff);
            log_debug("original seq number %ld", psh->seqno);
            log_debug("receive time %ld", psh->rectime);
            log_debug("transmit time %ld", psh->transtime);
            log_debug("run ID %ld", psh->runid);
            log_debug("original run id %ld", psh->origrunid);

#ifdef RETRANS_SUPPORT
                                /* Update acq table stats - Begin */
                        if(retrans_xmit_enable == OPTION_ENABLE){
                        
                                buff_hdr->buff_data_length = pdh->dbsize;
                                if(pdh->dbno == 0) {
                                                /* Assume first block */
                                        acq_tbl->proc_base_prod_type_last = psh->ptype;
                                        acq_tbl->proc_base_prod_cat_last = psh->pcat;
                                        acq_tbl->proc_base_prod_code_last = psh->pcode;
                                        acq_tbl->proc_prod_NCF_rcv_time = (time_t)psh->rectime;
                                        acq_tbl->proc_prod_NCF_xmit_time = (time_t)psh->transtime;
                                        if(psh->hflag & XFR_PROD_RETRANSMIT){
                                           acq_tbl->proc_orig_prod_seqno_last = psh->seqno;
                                           acq_tbl->proc_orig_prod_run_id = psh->origrunid;
                        log_debug("ORIG SEQ# = %ld CURR SEQ#: %ld \n",acq_tbl->proc_orig_prod_seqno_last,pdh->seqno);
                                                }else{
                                                   acq_tbl->proc_orig_prod_seqno_last = 0;
                                                   acq_tbl->proc_orig_prod_run_id = 0;
                                                }
                                        acq_tbl->proc_prod_run_id = psh->runid;
                                        buff_hdr->buff_datahdr_length = psh->psdl;
                                        time(&acq_tbl->proc_prod_start_time);
                                        acq_tbl->proc_tot_prods_handled++;
                                        genRetransReq = 0;
                                }else{
                                                buff_hdr->buff_datahdr_length = 0;
                                 }
                                buff_hdr->proc_prod_seqno= pdh->seqno;
                                buff_hdr->proc_blkno = pdh->dbno;
                                buff_hdr->proc_sub_code = 0;
                                buff_hdr->proc_prod_flag = pdh->transtype;
                                        
                                acq_tbl->proc_base_channel_type_last = buff_hdr->read_channel_type;
                                buff_hdr->proc_prod_type = acq_tbl->proc_base_prod_type_last;
                                buff_hdr->proc_prod_code = acq_tbl->proc_base_prod_code_last;
                                buff_hdr->proc_prod_cat = acq_tbl->proc_base_prod_cat_last;
                                        
                                acq_tbl->proc_prod_bytes_read = buff_hdr->buff_data_length;
                                                
                                /* Check prod_seqno for lost products */
                                if((buff_hdr->proc_prod_seqno - acq_tbl->proc_base_prod_seqno_last) != 1){
                                        do_prod_lost(buff_hdr,acq_tbl);
                                }
                                retrans_val = prod_retrans_ck(acq_tbl, buff_hdr, &orig_arrive_time);
                                log_buff[0] = '\0';             
                                if((retrans_val == PROD_DUPLICATE_DISCARD) ||
                                        ((retrans_val == PROD_DUPLICATE_MATCH) &&
                                        (acq_tbl->proc_retransmit_ctl_flag & ENABLE_RETRANS_DUP_MATCH_DISCARD)) ||
                                        ((retrans_val == PROD_DUPLICATE_NOMATCH) &&
                                        (acq_tbl->proc_retransmit_ctl_flag & ENABLE_RETRANS_DUP_NOMATCH_DISCARD))){
                                                /* Log product details and discard the product */
                                                strcpy(log_buff,"DISCARD");
                                                if(acq_tbl->proc_orig_prod_seqno_last != 0){
                                                        strcat(log_buff, "/RETRANS");
                                                }
                                                                
                                                log_prod_end(log_buff, acq_tbl->proc_orig_prod_seqno_last,
                                                                        buff_hdr->proc_prod_seqno,buff_hdr->proc_blkno,
                                                                        buff_hdr->proc_prod_code, acq_tbl->proc_prod_bytes_read,orig_arrive_time);
                                                save_prod = 0;
                                                acq_tbl->proc_base_prod_seqno_last = buff_hdr->proc_prod_seqno;
                                                /* Current prod discarded and continue with next */
                }
                else if (retrans_val == PROD_DUPLICATE_NOMATCH) {
                                                        strcpy(log_buff,"SAVE RETRANS");
                        log_prod_end(log_buff, acq_tbl->proc_orig_prod_seqno_last, buff_hdr->proc_prod_seqno,
                                buff_hdr->proc_blkno, buff_hdr->proc_prod_code, acq_tbl->proc_prod_bytes_read,
                                acq_tbl->proc_prod_start_time);
                                          }
                                   }
#endif

            if (prod.head != NULL) {
                log_error_q("OOPS, start of new product [%ld ] with unfinished "
                    "product %ld", pdh->seqno, prod.seqno);

                if(GOES == 1 && fillScan) {
                    /** Assume next product started before the prev. product is compelete **/
                    /** then the remaining number of frags should be filled with  blank   **/
                    /** scanlines in the GOES imagery **/

                    if((pdh->seqno != prod.seqno) &&
                           ((prod.nfrag != pfrag->fragnum + 1))){
                        frags_left = prod.nfrag - pfrag->fragnum - 1;
                        /** frags_left is the number of scanlines to be filled    **/
                        /** in the imagery rather than discarding the entire prod **/

                        GOES_BLNK_FRM_LEN = saved_pdb_struct.recsize;
                        n_scanlines = saved_pdh_struct.records_per_block;

                        log_notice_q("Fragments filled %d scanlines [%d] size (%d each) prod seq %ld ",
                                frags_left, n_scanlines, GOES_BLNK_FRM_LEN, prod.seqno);
                        log_debug("prev prod seqno %ld [%ld %ld]", prod.seqno, prod.nfrag, pfrag->fragnum);
                        log_debug("Balance frames left %d ", frags_left);
                        if (unCompress) {
                            /** Use uncompressed blank frames for scanlines **/
                            for(int cnt = 0; cnt < frags_left; cnt++){
                                memcpy(memheap + heapcount, GOES_BLANK_FRAME, (GOES_BLNK_FRM_LEN * n_scanlines));
                                MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount), (GOES_BLNK_FRM_LEN * n_scanlines));
                                heapcount += (GOES_BLNK_FRM_LEN * n_scanlines);
                            }
                        }
                        else {
                            /** Use compressed blank frames for scanlines **/
                            /** Compress the frame and add to the memheap **/
                            log_notice_q("Generating compressed blank scan lines of size [%d x %d x %d]"
                                     "[%ld] for prod seq %ld ", frags_left, n_scanlines, GOES_BLNK_FRM_LEN,
                                     (frags_left * n_scanlines * GOES_BLNK_FRM_LEN), prod.seqno);
    /**** FOR NOW   *****/
                            memset(uncomprBuf, 0, (GOES_BLNK_FRM_LEN * n_scanlines));
                            memset(comprBuf, 0, MAXBYTES_DATA);
                            uncomprLen = 0;
                            comprLen = 0;

                            deflateData((char*)uncomprBuf, (GOES_BLNK_FRM_LEN * n_scanlines),
                                   (char*)comprBuf, &comprLen, ANY_BLK );
                            inflateData((char*)comprBuf, comprLen,
                                    (char*)uncomprBuf, &uncomprLen, ANY_BLK );
                            deflateData((char*)uncomprBuf, uncomprLen,
                                    (char*)comprDataBuf, &comprDataLen, ANY_BLK );

                            for(int cnt = 0; cnt < (frags_left - 1); cnt++){
                                memcpy(memheap + heapcount, comprBuf, comprLen);
                                MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount), comprLen);
                                heapcount += comprLen;
                            }

                            /*** Last frame should be a filler with -1,0,-1,0 to make edex happy - Sathya - 10/06/2015 ***/
                            for(int ii=0; ii < (GOES_BLNK_FRM_LEN * n_scanlines); ii +=2)
                                uncomprBuf[ii] =  -1;
                            for(int ii=1; ii < (GOES_BLNK_FRM_LEN * n_scanlines); ii +=2)
                                uncomprBuf[ii] =  0;

                            deflateData((char*)uncomprBuf,
                                    (GOES_BLNK_FRM_LEN * n_scanlines),
                                    (char*)comprBuf, &comprLen, ANY_BLK);
                            inflateData((char*)comprBuf, comprLen,
                                    (char*)uncomprBuf, &uncomprLen, ANY_BLK );
                            deflateData((char*)uncomprBuf, uncomprLen,
                                    (char*)comprDataBuf, &comprDataLen, ANY_BLK);
        /**** FOR NOW   *****/
                            for(int cnt = 0; cnt < 1; cnt++){
                                memcpy(memheap + heapcount, comprBuf, comprLen);
                                MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount), comprLen);
                                heapcount += comprLen;
                            }
                        } /** end if-else unCompress **/

                        process_prod(prod, PROD_NAME, memheap, heapcount,
                              md5ctxp, productMaker->ldmProdQueue,
                              &saved_psh_struct, &saved_sbn_struct);

                        /** Increase the prod cnt as the product is inserted into ldm pq **/
                        (void)pthread_mutex_lock(&productMaker->mutex);
                        productMaker->nprods++;
                        (void)pthread_mutex_unlock(&productMaker->mutex);
                    }
                } /* end if GOES == 1 && fillScan */



#ifdef RETRANS_SUPPORT
                                /* Request retrans when prod is partially received but before completion */
                                /* if there is frame error and continue with different prod then, we need */
                                /* to abort the old prod and clear retrans table. */
                                if(retrans_xmit_enable == OPTION_ENABLE /*&& (pdh->dbno != 0)*/){
                                 acq_tbl->proc_acqtab_prodseq_errs++;
                                 if(proc_orig_prod_seqno_last_save != acq_tbl->proc_orig_prod_seqno_last){
                                 /* Clear retrans table for the orig prod if the previous prod is retrans */
                                 /* of original prod  */
                                   prod_retrans_abort_entry(acq_tbl, proc_orig_prod_seqno_last_save, RETRANS_RQST_CAUSE_RCV_ERR);
                                 }
                                 prod_retrans_abort_entry(acq_tbl, prod.seqno, RETRANS_RQST_CAUSE_RCV_ERR);
                                 /* Update Statistics */
                                 acq_tbl->proc_tot_prods_lost_errs++;
                                  /* For now, generate retrans request only for non-imagery products */
                                 if(!((buff_hdr->proc_prod_cat == PROD_CAT_IMAGE) && 
                                           (PROD_TYPE_NESDIS_HDR_TRUE(buff_hdr->proc_prod_type)))){
                                       generate_retrans_rqst(acq_tbl,prod.seqno , prod.seqno, RETRANS_RQST_CAUSE_RCV_ERR);
                                 }
                                   acq_tbl->proc_base_prod_seqno_last = buff_hdr->proc_prod_seqno;
                                }
#endif
                if(unCompress) {
                      log_info_q("resetting inflate due to prod error....");
                      inflateData((char*)buf + dataoff , datalen , (char*)uncomprBuf, &uncomprLen, END_BLK );
                }

                ds_free();

                prod.head = NULL;
                prod.tail = NULL;

                if (PNGINIT != 0) {
                    pngout_end();
                    PNGINIT = 0;
                }

                log_error_q("Product definition header version %d pdhlen %d", pdh->version,
                        pdh->len);
                log_error_q("PDH transfer type %u", pdh->transtype);

                if ((pdh->transtype & 8) > 0)
                    log_error_q("Product transfer flag error %u", pdh->transtype);
                if ((pdh->transtype & 32) > 0)
                    log_error_q("Product transfer flag error %u", pdh->transtype);

                log_error_q("header length %ld [pshlen = %d]",
                    pdh->len + pdh->pshlen, pdh->pshlen);
                log_error_q("blocks per record %ld records per block %ld",
                    pdh->blocks_per_record, pdh->records_per_block);
                log_error_q("product seqnumber %ld block number %d data block "
                    "size %d", pdh->seqno, pdh->dbno, pdh->dbsize);
                log_error_q("product header flag %d", psh->hflag);
                log_error_q("prodspecific data length %ld", psh->psdl);
                log_error_q("bytes per record %ld", psh->bytes_per_record);
                log_error_q("Fragments = %ld category %d", psh->frags, psh->pcat);

                if (psh->frags < 0)
                    log_error_q("check psh->frags %d", psh->frags);
                if (psh->origrunid != 0)
                    log_error_q("original runid %d", psh->origrunid);

                log_error_q("next header offset %ld", psh->nhoff);
                log_error_q("original seq number %ld", psh->seqno);
                log_error_q("receive time %ld", psh->rectime);
                log_error_q("transmit time %ld", psh->transtime);
                log_error_q("run ID %ld", psh->runid);
                log_error_q("original run id %ld", psh->origrunid);
            }

            prod.seqno = pdh->seqno;
            prod.nfrag = psh->frags;

            ds_init(prod.nfrag);

            /* NWSTG CCB = dataoff, WMO = dataoff + 24 */

            if (fifo_getBytes(fifo, buf + sbn->len + pdh->len + 
                        pdh->pshlen, pdh->dbsize) != 0) {
                log_error_q("problem reading datablock");
                continue;
            }
            if (sbn->datastream == SBN_CHAN_NOAAPORT_OPT) {
                if (psh->pcat != PROD_CAT_IMAGE) {
                    GOES = 0;
                    NWSTG = 1;
                }
            }

            heapcount = 0;

            MD5Init(md5ctxp);

            if (GOES == 1) {
                if (readpdb ((char*)buf + IOFF + sbn->len + pdh->len + pdh->pshlen, psh, pdb,
                        PROD_COMPRESSED, pdh->dbsize) == -1) {
                    log_error_q ("Error reading pdb, punt");
                    continue;
                }

                (void)memcpy(PROD_NAME, psh->pname, sizeof(PROD_NAME));

                log_debug("Read GOES %d %d %d [%d] %d", sbn->len, pdh->len,
                        pdh->pshlen, sbn->len + pdh->len + pdh->pshlen,
                        pdb->len);

                /* Data starts at first block after pdb */
                ccb->len = 0;
                heapsize = prodalloc(psh->frags, 5152, &memheap);
            }
            if (NWSTG == 1) {
                memset(psh->pname, 0, sizeof(psh->pname));

                if (readccb((char*)buf + IOFF + sbn->len + pdh->len + pdh->pshlen,
                        ccb, psh, pdh->dbsize) == -1)
                    log_info("Error reading ccb, using default name");

                log_debug("look at ccb start %d %d", ccb->b1, ccb->len);
                log_info_q("%s", psh->pname);

                memcpy(PROD_NAME, psh->pname, sizeof(PROD_NAME));

                heapsize = prodalloc(psh->frags, 4000 + 15, &memheap);

                /*
                 * The 11 characters in the FOS header:
                 *     SOH CR CR LF <iii> SPACE CR CR LF
                 * where <iii>  is the 3-digit sequence number, are *not* used
                 * in the computation of the MD5 signature. -- SRE 2020-07-09
                 */
                sprintf(memheap, "\001\015\015\012%03d\040\015\015\012",
                    ((int) pdh->seqno) % 1000);

                heapcount += 11;

                if (psh->metaoff > 0)
                    psh->metaoff = psh->metaoff + 11;
            }
        } // Have product-specific header (pdh->pshlen != 0)
        else {
            /* If a continuation record...don't let psh->pcat get missed */
            if ((sbn->datastream == SBN_CHAN_NOAAPORT_OPT) && (psh->pcat != PROD_CAT_IMAGE)) {
                GOES = 0;
                NWSTG = 1;
            }

            ccb->len = 0;

            log_debug("continuation record");

#ifdef RETRANS_SUPPORT
                        if(retrans_xmit_enable == OPTION_ENABLE){
                                         buff_hdr->buff_data_length = pdh->dbsize;
                                         buff_hdr->buff_datahdr_length = 0;
                                         buff_hdr->proc_prod_seqno= pdh->seqno;
                                         buff_hdr->proc_blkno = pdh->dbno;
                                         buff_hdr->proc_sub_code = 0;
                                         buff_hdr->proc_prod_flag = pdh->transtype;
                                         
                                         acq_tbl->proc_base_channel_type_last = buff_hdr->read_channel_type;
                                         buff_hdr->proc_prod_type = acq_tbl->proc_base_prod_type_last;
                                         buff_hdr->proc_prod_code = acq_tbl->proc_base_prod_code_last;
                                         buff_hdr->proc_prod_cat = acq_tbl->proc_base_prod_cat_last;
                                 
                                         acq_tbl->proc_prod_bytes_read += buff_hdr->buff_data_length;
                                         
                          }
#endif

            if ((pdh->transtype & 4) > 0) {
                psh->frags = 0;
            }
            if (fifo_getBytes(fifo, buf + sbn->len + pdh->len + 
                        pdh->pshlen, pdh->dbsize) != 0) {
                log_error_q("problem reading datablock (cont)");
                continue;
            }
            if (prod.head == NULL) {
                log_info_q("found data block before header, "
                    "skipping sequence %ld frag #%d", pdh->seqno, pdh->dbno);
                continue;
            }
        } // Don't have product-specific header (pdh->pshlen == 0)

        /* Get the data */
        dataoff = IOFF + sbn->len + pdh->len + pdh->pshlen + ccb->len;
        datalen = pdh->dbsize - ccb->len;

        log_debug("look at datalen %d", datalen);

        pfrag = ds_alloc();
        pfrag->seqno = pdh->seqno;
        pfrag->fragnum = pdh->dbno;
        pfrag->recsiz = datalen;
        pfrag->offset = heapcount;
        pfrag->next = NULL;

        if (GOES == 1) {
            if (pfrag->fragnum > 0) {
                if (prod.tail && ((pfrag->fragnum != prod.tail->fragnum + 1) ||
                        (pfrag->seqno != prod.seqno))) {
                    log_error_q("Missing GOES fragment in sequence, "
                            "last %d/%d this %d/%d\0", prod.tail->fragnum,
                            prod.seqno, pfrag->fragnum, pfrag->seqno);

#ifdef RETRANS_SUPPORT
                                        if(retrans_xmit_enable == OPTION_ENABLE){
                                          acq_tbl->proc_acqtab_prodseq_errs++;
                                          if((pfrag->seqno != prod.seqno) ||
                                                  ((pfrag->seqno == prod.seqno) &&
                                                  (genRetransReq == 0))) {
                                              do_prod_mismatch(acq_tbl,buff_hdr);
                                              genRetransReq = 1;
                                          }
                                          acq_tbl->proc_base_prod_seqno_last =
                                                  buff_hdr->proc_prod_seqno;
                                        }
#endif
/**********************    NEW CODE    ********************************/
                    if(fillScan) {
                        if(pfrag->seqno != prod.seqno) { /** Ex. last 307/5690 this 5/5691 **/
                            frags_left = saved_nfrags - prod.tail->fragnum - 1;
                            log_notice_q("Total frames expected: %d balance left %d ",
                                    saved_nfrags, frags_left);

                            GOES_BLNK_FRM_LEN = saved_pdb_struct.recsize;
                            n_scanlines = saved_pdh_struct.records_per_block;

                            if(unCompress) {
                                 for(int cnt = 0; cnt < frags_left; cnt++){
                                    memcpy(memheap + heapcount, GOES_BLANK_FRAME,
                                            (GOES_BLNK_FRM_LEN * n_scanlines));
                                    MD5Update(md5ctxp,
                                            (unsigned char *) (memheap + heapcount),
                                            (GOES_BLNK_FRM_LEN * n_scanlines));
                                    heapcount += (size_t)GOES_BLNK_FRM_LEN * n_scanlines;
                                    log_debug("GOES uncompressed blank frames added "
                                            "[tot/this] [%d/%d] heapcount = %ld "
                                            "blank_frame_len = %d scanlines %d",
                                             frags_left, cnt, heapcount,
                                             GOES_BLNK_FRM_LEN, n_scanlines);
                                  }
                            }
                            else{
                                /** Use compressed blank frames for scanlines **/
                                /** Compress the frame and add to the memheap **/
    /**** FOR NOW   *****/

                                memset(uncomprBuf, 0, (GOES_BLNK_FRM_LEN * n_scanlines));
                                memset(comprBuf, 0, MAXBYTES_DATA);
                                uncomprLen = 0;
                                comprLen = 0;

                                deflateData((char*)uncomprBuf,
                                        (GOES_BLNK_FRM_LEN * n_scanlines),
                                        (char*)comprBuf, &comprLen, ANY_BLK );
                                inflateData((char*)comprBuf, comprLen,
                                        (char*)uncomprBuf, &uncomprLen,
                                        ANY_BLK );
                                deflateData((char*)uncomprBuf, uncomprLen,
                                        (char*)comprDataBuf, &comprDataLen,
                                        ANY_BLK );

                                for(int cnt = 0; cnt < (frags_left - 1); cnt++){
                                    memcpy(memheap + heapcount, comprBuf, comprLen);
                                    MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount), comprLen);
                                    heapcount += comprLen;
                                 }

                                /*** Last frame should be a filler with -1,0,-1,0 to make edex happy - Sathya - 10/06/2015 ***/
                                for(int ii=0; ii < (GOES_BLNK_FRM_LEN * n_scanlines); ii +=2)
                                    uncomprBuf[ii] =  -1;
                                for(int ii=1; ii < (GOES_BLNK_FRM_LEN * n_scanlines); ii +=2)
                                    uncomprBuf[ii] =  0;

                                deflateData((char*)uncomprBuf,
                                        (GOES_BLNK_FRM_LEN * n_scanlines),
                                        (char*)comprBuf, &comprLen, ANY_BLK );

                                inflateData((char*)comprBuf, comprLen,
                                        (char*)uncomprBuf, &uncomprLen,
                                        ANY_BLK);
                                deflateData((char*)uncomprBuf, uncomprLen,
                                        (char*)comprBuf, &comprLen, ANY_BLK );

        /**** FOR NOW   *****/
                                for(int cnt = 0; cnt < 1; cnt++){
                                    memcpy(memheap + heapcount, comprBuf, comprLen);
                                    MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount), comprLen);
                                    heapcount += comprLen;
                                }
                            } /** end if-else unCompress **/

                            log_notice_q("%d scanlines filled into block %d prod seq %ld ",
                                    n_scanlines, frags_left, prod.seqno);
                            /** Insert the prod with missing frames into the ldm pq    **/
                            /** Also terminate current prod as there is no header info **/
                            process_prod(prod, PROD_NAME, memheap, heapcount,
                                    md5ctxp, productMaker->ldmProdQueue,
                                    &saved_psh_struct, &saved_sbn_struct);

                            /** Increase the prod cnt as the product is inserted into ldm pq **/
                            (void)pthread_mutex_lock(&productMaker->mutex);
                            productMaker->nprods++;
                            (void)pthread_mutex_unlock(&productMaker->mutex);

                            ds_free();
                            prod.head = NULL;
                            prod.tail = NULL;
                            continue;

                        } /** pfrag->seqno != prod.seqno **/

                        frags_left = pfrag->fragnum - prod.tail->fragnum - 1;
                        n_scanlines = pdh->records_per_block;
                        GOES_BLNK_FRM_LEN = pdb->recsize;

                        log_notice_q("Balance frames left %d scanlines per frame %d",
                                frags_left, n_scanlines);

                        if(unCompress) {
                            for(int cnt = 0; cnt < frags_left; cnt++){
                                 memcpy(memheap + heapcount, GOES_BLANK_FRAME,
                                         (GOES_BLNK_FRM_LEN * n_scanlines));
                                 MD5Update(md5ctxp,
                                         (unsigned char *) (memheap + heapcount),
                                        (GOES_BLNK_FRM_LEN * n_scanlines));
                                 heapcount += (size_t)GOES_BLNK_FRM_LEN * n_scanlines;
                                 log_debug("GOES blank frames added [tot/this] "
                                         "[%d/%d] heapcount [%ld] blank_frame_len "
                                         "[%d] scanlines [%d]",
                                         frags_left, cnt, heapcount,
                                         GOES_BLNK_FRM_LEN, n_scanlines);
                            }
                        }
                        else{
                            /** Use compressed blank frames for scanlines **/
                            /** Compress the frame and add to the memheap **/
        /**** FOR NOW   *****/
                            memset(uncomprBuf, 0, (GOES_BLNK_FRM_LEN * n_scanlines));
                            memset(comprBuf, 0, MAXBYTES_DATA);
                            uncomprLen = 0;
                            comprLen = 0;

                            //deflateData(uncomprBuf, (GOES_BLNK_FRM_LEN * n_scanlines * frags_left),
                            deflateData((char*)uncomprBuf,
                                    (GOES_BLNK_FRM_LEN * n_scanlines),
                                    (char*)comprBuf, &comprLen, ANY_BLK );
        /**** FOR NOW   *****/

                            for(int cnt = 0; cnt < frags_left; cnt++){
                                memcpy(memheap + heapcount, comprBuf, comprLen);
                                MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount), comprLen);
                                heapcount += comprLen;
                                log_debug("GOES compressed blank frames added "
                                        "heapcount = %ld blank frame size = %ld ",
                                        heapcount, comprLen );
                            }
                        }

                        log_notice_q("Total %d scanlines filled for block %d into prod seq %ld ",
                                (n_scanlines * frags_left), frags_left, prod.seqno);
        /**********************    NEW CODE    ********************************/
                    }/** end if fillScan */
                    else {
                        ds_free();

                        prod.head = NULL;
                        prod.tail = NULL;

                        continue;
                    }
                }

                if ((PNGINIT != 1) && (!PROD_COMPRESSED)) {
                    log_error_q("failed pnginit %d %d %s", sbn->datastream,
                            psh->pcat, PROD_NAME);
                    continue;
                }
                if (pdh->records_per_block < 1) {
                    log_error_q("records_per_block %d blocks_per_record %d "
                            "nx %d ny %d", pdh->records_per_block,
                            pdh->blocks_per_record, pdb->nx, pdb->ny);
                    log_error_q("source %d sector %d channel %d", pdb->source,
                            pdb->sector, pdb->channel);
                    log_error_q("nrec %d recsize %d date %02d%02d%02d %02d%02d "
                            "%02d.%02d", pdb->nrec, pdb->recsize, pdb->year,
                            pdb->month, pdb->day, pdb->hour, pdb->minute,
                            pdb->second, pdb->sechunds);
                    log_error_q("pshname %s", psh->pname);
                }
                if (!PROD_COMPRESSED) {
                    for (nscan = 0; (nscan * pdb->nx) < pdh->dbsize; nscan++) {
                        log_debug("png write nscan %d", nscan);
                        if (nscan >= pdh->records_per_block) {
                            log_error_q("nscan exceeding records per block %d [%d "
                                "%d %d]", pdh->records_per_block, nscan,
                                pdb->nx, pdh->dbsize);
                        }
                        else {
                            pngwrite((char*)buf + dataoff + (nscan * pdb->nx));
                        }
                    }
                }
                else {
                   if(unCompress){
                        memset(uncomprBuf, 0, MAXBYTES_DATA);
                        uncomprLen = 0;
                        /** Uncompress the frame and add to the memheap **/
                        inflateData((char*)buf + dataoff, datalen,
                                (char*)uncomprBuf, &uncomprLen, ANY_BLK );
                        memcpy(memheap + heapcount, uncomprBuf, uncomprLen);
                        MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount),
                                                                        uncomprLen);
                        heapcount += uncomprLen;
                   }
                   else {
                        memcpy(memheap + heapcount, buf + dataoff, datalen);
                        MD5Update(md5ctxp,
                                (unsigned char*)(memheap + heapcount), datalen);
                        heapcount += datalen;
                   }
                }
            } /* end if fragnum > 0 */
            else {
                if (!PROD_COMPRESSED) {
                    png_set_memheap(memheap, md5ctxp);
                    png_header((char*)buf + dataoff, datalen);
                    /*
                     * Add 1 to number of scanlines, image ends with
                     * f0f0f0f0...
                     */
                    pngout_init(pdb->nx, pdb->ny + 1);

                    PNGINIT = 1;
                }
                else {
                    if(unCompress) {
                        /** Uncompress the frame and add to the memheap **/
                        inflateData(NULL, 0, NULL, &uncomprLen, BEGIN_BLK );
                        inflateData((char*)buf + dataoff + 21, datalen - 21,
                                (char*)uncomprBuf, &uncomprLen, ANY_BLK );
                        memcpy(memheap + heapcount, uncomprBuf, uncomprLen);
                        MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount),
                              uncomprLen);
                        heapcount += uncomprLen;
                    }
                    else{
                        memcpy(memheap + heapcount, buf + dataoff, datalen);
                        MD5Update(md5ctxp,
                                (unsigned char*)(memheap + heapcount), datalen);
                        heapcount += datalen;
                    }
                }
                log_info("records_per_block %d blocks_per_record %d nx %d ny %d",
                        pdh->records_per_block, pdh->blocks_per_record, pdb->nx,
                        pdb->ny);
                log_info("source %d sector %d channel %d", pdb->source,
                        pdb->sector, pdb->channel);
                log_info("nrec %d recsize %d date %02d%02d%02d %02d%02d "
                        "%02d.%02d", pdb->nrec, pdb->recsize, pdb->year, pdb->month,
                        pdb->day, pdb->hour, pdb->minute, pdb->second,
                        pdb->sechunds);
                log_info("pshname %s", psh->pname);
            }
            deflen = 0;
#ifdef RETRANS_SUPPORT
                        if(retrans_xmit_enable == OPTION_ENABLE){
                           if(buff_hdr->proc_blkno != 0){
                          /*acq_tbl->proc_prod_bytes_read += buff_hdr->buff_data_length;*/
                                  acq_tbl->proc_prod_bytes_read += datalen;
                   }
            }
#endif
        } // Product is GOES image (GOES == 1)
        else {
            /* If the product already has a FOS trailer, don't add
             * another....this will match what pqing(SDI) sees
             */
            if ((prod.nfrag != 0) && (prod.tail != NULL)) {
                if ((pfrag->fragnum != prod.tail->fragnum + 1) ||
                        (pfrag->seqno != prod.seqno)) {
                    log_error_q("Missing fragment in sequence, last %d/%d this "
                            "%d/%d\0", prod.tail->fragnum, prod.seqno,
                            pfrag->fragnum, pfrag->seqno);

#ifdef RETRANS_SUPPORT
                                      if(retrans_xmit_enable == OPTION_ENABLE){
                                         acq_tbl->proc_acqtab_prodseq_errs++;
                        log_debug("do_prod_mismatch() proc_base_prod_seqno_last = %d \n",
                                                                            acq_tbl->proc_base_prod_seqno_last);
                                            do_prod_mismatch(acq_tbl,buff_hdr);
                                            acq_tbl->proc_base_prod_seqno_last = buff_hdr->proc_prod_seqno;
                                      }
#endif
                    if(unCompress) {
                        log_info_q("resetting inflate due to prod error....");
                        inflateData((char*)buf + dataoff , datalen ,
                                (char*)uncomprBuf, &uncomprLen, END_BLK );
                    }

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

                        log_debug("removing FOS trailer from %s", PROD_NAME);
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
                log_error_q("Error in heapsize %d product size %d [%d %d], Punt!\0",
                        heapsize, (heapcount + datalen), heapcount, datalen);
                                
#ifdef RETRANS_SUPPORT
                                if(retrans_xmit_enable == OPTION_ENABLE){       
                              /* Update Statistics */
                                        acq_tbl->proc_tot_prods_lost_errs++;
                                        /*  Abort entry and request retransmission */
                                        prod_retrans_abort_entry(acq_tbl, prod.seqno, RETRANS_RQST_CAUSE_RCV_ERR);
                                        generate_retrans_rqst(acq_tbl, prod.seqno, prod.seqno, RETRANS_RQST_CAUSE_RCV_ERR);
                                        if(acq_tbl->proc_orig_prod_seqno_last != 0){
                                                strcpy(log_buff, "RETRANS");
                                        }
                                        log_prod_end(log_buff, acq_tbl->proc_orig_prod_seqno_last,
                                                                 buff_hdr->proc_prod_seqno,buff_hdr->proc_blkno,
                                                                 buff_hdr->proc_prod_code, acq_tbl->proc_prod_bytes_read,
                                                                 acq_tbl->proc_prod_start_time);

                                acq_tbl->proc_base_prod_seqno_last = buff_hdr->proc_prod_seqno;
                        }
#endif

                continue;
            }
/**********     NEW CODE - BEGIN        ***************/
            /**  If uncompress is requested via cmdline
             **  and frame is compressed then uncompress
             **  the frame and add to the heap
             **/
            log_debug(" unCompress = %d   PROD_COMPRESSED = %d seqno=%ld\n",
                    unCompress, PROD_COMPRESSED, prod.seqno);

             /***
                   Special case: For a given product, when first and
                   intermediate frames are compressed but not the last frame by
                   uplink then last frame does not need to be decompressed. But
                   inflateData routine should be notified to close the stream.
                   Otherwise it would cause memory leak

             if((unCompress) &&
                (curr_prod_seqno == prod.seqno) &&
                (prod.nfrag == (pfrag->fragnum + 1)) &&
                (saved_prod_compr_flag == 1 && PROD_COMPRESSED == 0)) {
                  if(ulogIsDebug())
                     udebug("Prev. frame %s Current frame %s", ((saved_prod_compr_flag > 0) ? "compressed":"uncompressed"),
                           ((PROD_COMPRESSED > 0) ? "compressed" : "uncompressed"));
                    firstBlk = 2;
                    lastBlk = 2;
                    inflateData(NULL , 0 , firstBlk, lastBlk, NULL, &uncomprLen );
             }
             ***/

            if(unCompress) {
                if(pdh->dbno == 0){
                    log_debug("First Blk, initializing inflate prod %ld",
                            prod.seqno);
                    inflateData(NULL, 0, NULL, &uncomprLen, BEGIN_BLK );
                }
            }

            if(unCompress && PROD_COMPRESSED) {
                if(pdh->dbno == 0){
                    /** Only need to parse the first block for WMO and NNNXXX 
                     ** and get the offset required to pass on to inflate.
                     ** For other blocks, simply pass the buffer to inflate. **/

                    wmo_offset = prod_get_WMO_offset((char*)buf + dataoff,
                            datalen, &wmolen);
                    nnnxxx_offset =  prod_get_WMO_nnnxxx_offset(
                            (char*)buf + dataoff, datalen, &nxlen);

                    log_debug(" Block# %d  wmo_offset [%d] wmolen [%zd] ",
                            pdh->dbno, wmo_offset, wmolen);
                    log_debug(" Block# %d  nnnxxx_offset [%d] nnxxlen [%d] ", pdh->dbno, nnnxxx_offset, nxlen);
                    log_debug("Seq#:%ld Block# %d ",prod.seqno, pdh->dbno );
                    if((nnnxxx_offset == -1 && nxlen == 0) && (wmolen > 0)) {
                        /** Product does not contain NNNXXX **/
                        inflateData((char*)buf + dataoff + wmolen,
                                datalen - wmolen, (char*)uncomprBuf,
                                &uncomprLen, ANY_BLK );
                    }
                    else{
                        /** Product has NNNXXX (AWIPS Prod ID) **/
                        if((nnnxxx_offset > 0 && nxlen > 0) && (wmolen > 0)){
                            inflateData((char*)buf + dataoff + wmolen + nxlen,
                                    datalen - wmolen - nxlen, (char*)uncomprBuf,
                                    &uncomprLen, ANY_BLK );
                        }
                    }
                }
                else{ /** Continuation block **/
                    log_debug(" Block# %d  contd block", pdh->dbno);
                    inflateData((char*)buf + dataoff , datalen ,
                            (char*)uncomprBuf, &uncomprLen, ANY_BLK );
                    log_debug("Seq#:%ld Block# %d  contd block ",prod.seqno, pdh->dbno);
                }
                memcpy(memheap + heapcount, uncomprBuf, uncomprLen);
                deflen = uncomprLen;
                log_debug(" Block# %d inflated uncomprLen [%ld]", pdh->dbno, uncomprLen);
            }
            else{
                /** executed by default (when unCompress is not enabled
                    or product is not compressed) **/
                memcpy(memheap + heapcount, buf + dataoff, datalen);
                deflen = datalen;
            } /** end if-else pdh->blkno  == 0 **/

            MD5Update(md5ctxp, (unsigned char *) (memheap + heapcount), deflen);

#ifdef RETRANS_SUPPORT
                        if(retrans_xmit_enable == OPTION_ENABLE){
                          if(buff_hdr->proc_blkno != 0){
                                acq_tbl->proc_prod_bytes_read += datalen;
                          }
                        }
#endif
        } // Product isn't GOES image (GOES == 0)

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
 
#ifdef RETRANS_SUPPORT
                 if(((prod.nfrag == 0) || (prod.nfrag >= (pfrag->fragnum +1))) && (save_prod == 0)){
            log_info_q("Do not save prod [seqno=%ld] as its retrans dup fragnum/total fragments =[%d of %d] save_prod=[%d] \n",
                           prod.seqno,pfrag->fragnum,prod.nfrag,save_prod);
                   ds_free ();
                   prod.head = NULL;
                   prod.tail = NULL;
                   PNGINIT = 0;
                }else{
#endif
        if ((prod.nfrag == 0) || (prod.nfrag == (pfrag->fragnum + 1))) {
            if(unCompress){
                log_debug("uncompress ==> %d Last Blk, call inflateEnd prod %ld", unCompress,  prod.seqno);
                inflateData(NULL, 0, NULL, &uncomprLen, END_BLK );
            }
            if (GOES == 1) {
                if (PNGINIT == 1) {
                    pngout_end();
                    heapcount = png_get_prodlen();
                }
                else {
                    log_debug("GOES product already compressed %d", heapcount);
                }
                if(fillScan || !unCompress) {
                    log_debug("Last Blk, call deflateEnd prod %ld", prod.seqno);
                    deflateData(NULL, 0, NULL, &uncomprLen, END_BLK );
                }
            }

            log_info_q("we should have a complete product %ld %ld/%ld %ld /heap "
                    "%ld", prod.seqno, pfrag->seqno, prod.nfrag, pfrag->fragnum,
                    (long) heapcount);
            if ((NWSTG == 1) && (heapcount > 4)) {
                cnt = 4;                /* number of bytes to add for TRAILER */

                /*
                 * Do a DDPLUS vs HDS check for NWSTG channel only
                 */
                if (sbn->datastream == SBN_CHAN_NMC) {
                    /* nwstg channel */
                    switch (psh->pcat) {
                        case PROD_CAT_TEXT:
                        case PROD_CAT_OTHER:
                            /* Do a quick check for non-ascii text products */
                            if (!prod_isascii(PROD_NAME, memheap, heapcount))
                            psh->pcat += 100;       /* call these HDS */
                            break;
                    }
                }

                if (cnt > 0) {
                	/*
                	 * The FOS trailer is used in the computation of the MD5
                	 * signature. It probably shouldn't be -- especially because
                	 * the FOS header isn't used. Too late to change it now.
                	 * -- SRE 2020-07-09
                	 */
                    memcpy(memheap + heapcount, FOS_TRAILER + 4 - cnt, cnt);
                    MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount),
                            cnt);
                    heapcount += cnt;
                }
            }

#ifdef RETRANS_SUPPORT
                        if((retrans_xmit_enable == OPTION_ENABLE) && (acq_tbl->read_distrib_enable & READ_CTL_DISCARD)){
                                                num_prod_discards++;
                                                /* Set discard_prod to 1; Otherwise already stored prod may be requested for retransmit */
                                                discard_prod=1;
                    log_info_q("No of products discarded = %ld prod.seqno=%ld \n ",num_prod_discards,prod.seqno);
                                                prod_retrans_abort_entry(acq_tbl, prod.seqno, RETRANS_RQST_CAUSE_RCV_ERR);
                                                acq_tbl->proc_base_prod_seqno_last = buff_hdr->proc_prod_seqno -1 ;
                                                ds_free ();
                                                prod.head = NULL;
                                                prod.tail = NULL;
                                                PNGINIT = 0;
                        }else{
                                /* Do not insert prod into queue if its a duplicate product */
                                if(save_prod != 0)
#endif
            process_prod(prod, PROD_NAME, memheap, heapcount, md5ctxp,
                    productMaker->ldmProdQueue, psh, sbn);
#ifdef RETRANS_SUPPORT
                                /* Update acq table with last processed seqno -Begin */
                                if(retrans_xmit_enable == OPTION_ENABLE){
                                        acq_tbl->proc_base_prod_seqno_last = buff_hdr->proc_prod_seqno;
                        log_info_q(" prod with seqno processed = %ld\n",acq_tbl->proc_base_prod_seqno_last);
                                }
                                /* Update acq table with last processed seqno -End */
#endif

            ds_free();

            prod.head = NULL;
            prod.tail = NULL;
            PNGINIT = 0;

            (void)pthread_mutex_lock(&productMaker->mutex);
            productMaker->nprods++;
            (void)pthread_mutex_unlock(&productMaker->mutex);
#ifdef RETRANS_SUPPORT
        }
#endif
        } // Multiple products in frame or last fragment
        else {
            log_debug("processing record %ld [%ld %ld]", prod.seqno, prod.nfrag, pfrag->fragnum);
            if ((pdh->transtype & 4) > 0) {
                log_error_q("Hmmm....should call completed product %ld [%ld %ld]",
                        prod.seqno, prod.nfrag, pfrag->fragnum);
            }
        }

#ifdef RETRANS_SUPPORT
           if(retrans_xmit_enable == OPTION_ENABLE){
                if (!(acq_tbl->read_distrib_enable & READ_CTL_DISCARD)) {
                  if(!discard_prod){
                    acq_tbl->proc_base_prod_seqno_last = buff_hdr->proc_prod_seqno;
                    discard_prod = 0;
              }
           }
            }
#endif
        /** Required to save only if decompression is requested via cmdline **/
        if(unCompress || fillScan) {
            saved_sbn_struct = *sbn;
            saved_psh_struct = *psh;
            saved_pdb_struct = *pdb; // clang_sacn(1) says stored value isn't read
            saved_pdh_struct = *pdh; // clang_sacn(1) says stored value isn't read
            saved_nfrags = prod.nfrag; // clang_sacn(1) says stored value isn't read
        }
                
#ifdef RETRANS_SUPPORT
        }

        save_prod = 1;
#endif
        IOFF += (sbn->len + pdh->len + pdh->pshlen + pdh->dbsize);

        log_debug("look IOFF %ld datalen %ld (deflate %ld)", IOFF, datalen, deflen);
#ifdef RETRANS_SUPPORT
                if(retrans_xmit_enable == OPTION_ENABLE){
                  total_prods_retrans_rcvd = acq_tbl->proc_tot_prods_retrans_rcvd;     /* prods retrans rcvd by proc */
                  total_prods_retrans_rcvd_lost = acq_tbl->proc_tot_prods_retrans_rcvd_lost; /* prods retrans rcvd lost */
                  total_prods_retrans_rcvd_notlost = acq_tbl->proc_tot_prods_retrans_rcvd_notlost; /* prods rcvd not lost */
                  total_prods_retrans_rqstd = acq_tbl->proc_tot_prods_retrans_rqstd;    /* prods retrans requested */
                  total_prods_handled = acq_tbl->proc_tot_prods_handled;    /* prods retrans requested */
                  total_prods_lost_err = acq_tbl->proc_tot_prods_lost_errs;    /* prods retrans requested */
                  total_frame_cnt = acq_tbl->read_tot_buff_read;
                  total_frame_err = acq_tbl->read_frame_tot_lost_errs;
                  proc_orig_prod_seqno_last_save = acq_tbl->proc_orig_prod_seqno_last;
        }
#endif
    }

    if (NULL != memheap)
        free(memheap);

    productMaker->status = status;

    return NULL;
}

/**
 * Returns statistics since the last time this function was called or \link
 * pmStart() \endlink was called.
 *
 * @param[in]  productMaker      Product maker
 * @param[out] frameCount        Number of frames
 * @param[out] missedFrameCount  Number of missed frames
 * @param[out] prodCount         Number of products inserted into the product-queue
 */
void pmGetStatistics(
    ProductMaker* const  productMaker,
    unsigned long* const frameCount,
    unsigned long* const missedFrameCount,
    unsigned long* const prodCount)
{
    (void)pthread_mutex_lock(&productMaker->mutex);

    *frameCount = productMaker->nframes;
    *missedFrameCount = productMaker->nmissed;
    *prodCount = productMaker->nprods;

    productMaker->nframes = 0;
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
 * @retval 1    Usage failure. \c log_add() called.
 * @retval 2    O/S failure. \c log_add() called.
 */
int pmStatus(
    ProductMaker* const productMaker)   /**< [in] Pointer to the product-maker
                                          */
{
    return productMaker->status;
}

static z_stream i_zstrm;

/**
 * Returns the status of a frame decommpression
 *
 * This function is thread-compatible but not thread-safe.
 *
 * @param[in]  inBuf    Pointer to the frame buffer
 * @param[in]  inLen    Length of the compressed data
 * @param[out] outBuf   Pointer to uncompressed frame data buffer
 * @param[out] outLen   Length of uncompressed frame
 * @param[in]  blk      Block position
 * @retval  0           The frame was successfully uncompressed
 * @retval -1           Failed to uncompress
 */
static int inflateData(
    const char* const inBuf,
    unsigned long     inLen,
    const    char*    outBuf,
    unsigned long*    outLen,
    unsigned int      blk)
{
  int zerr;
  char out[CHUNK_SZ];
  char in[CHUNK_SZ];
  int ret;
  int savedByteCntr=0;
  unsigned char *dstBuf;
  int totalBytesIn=0;
  int inflatedBytes=0;
  int decompByteCounter = 0;
  static int isStreamSet = NO;


  ret = Z_OK;

 /** special case - close the stream when there is product error **/
  if(blk == END_BLK && isStreamSet) {
    ret = inflateEnd(&i_zstrm);
    if(ret < 0) {
      log_error_q("Fail inflateEnd %d [%s] ", ret, decode_zlib_err(ret));
      return (ret);
    }
   isStreamSet = NO;
   log_debug("inflateEnd called ......ret=%d", ret);
   return 0;
  }



  if(blk == BEGIN_BLK && !isStreamSet){
     log_debug("Received first Blk");
        memset(&i_zstrm, '\0', sizeof(z_stream));
        i_zstrm.zalloc = Z_NULL;
        i_zstrm.zfree = Z_NULL;
        i_zstrm.opaque = Z_NULL;

        if ((zerr = inflateInit(&i_zstrm)) != Z_OK) {
       log_error_q("ERROR %d inflateInit (%s)", zerr, decode_zlib_err(zerr));
                return -1;
        }
        isStreamSet = YES;
        return 0;
  }

  dstBuf = (unsigned char *) outBuf;

    log_debug("inflating now.. inlen[%ld] ", inLen);
/**************** NEW CODE FROM JAVA *******************/

    while(totalBytesIn < inLen ) {
      int compChunkSize = ((inLen - totalBytesIn) > 5120) ? 5120 :
                                                  (inLen - totalBytesIn);
      memcpy(in, inBuf + totalBytesIn, compChunkSize);

      i_zstrm.avail_in = inLen - totalBytesIn;
      i_zstrm.next_in = (Bytef*)in ;

      i_zstrm.avail_out = CHUNK_SZ;
      i_zstrm.next_out = (Bytef*)out;

      while(ret != Z_STREAM_END) {
         ret  = inflate(&i_zstrm, Z_NO_FLUSH);
         if(ret < 0) {
          log_error_q(" Error %d inflate (%s)", ret, decode_zlib_err(ret));
          (void)inflateEnd(&i_zstrm);
          isStreamSet = NO;
           return ret;
         }
         inflatedBytes = CHUNK_SZ - i_zstrm.avail_out;

         if(inflatedBytes == 0) {
              log_notice_q("\n Unable to decompress data - truncated");
              break;
         }

         totalBytesIn += i_zstrm.total_in;
         decompByteCounter += inflatedBytes;

         if(totalBytesIn == inLen)
              (void)getIndex(out, 0, inflatedBytes);
         memcpy(dstBuf + savedByteCntr, out, inflatedBytes);
         savedByteCntr = decompByteCounter;
       }
       // Reset inflater for additional input
       ret = inflateReset(&i_zstrm);
       if(ret == Z_STREAM_ERROR){
        log_error_q(" Error %d inflateReset (%s)", ret, decode_zlib_err(ret));
          (void)inflateEnd(&i_zstrm);
          isStreamSet = NO;
           return ret;
       }
      }

/**************** NEW CODE FROM JAVA *******************/

  *outLen = decompByteCounter;
   return 0; 
}

static z_stream d_zstrm;

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
    unsigned int      blk)
{
    int ret;
    int savedByteCntr=0;
    unsigned char in[CHUNK_SZ];
    unsigned char out[CHUNK_SZ];
    int flush;
    int totalBytesComp=0;
    int compressedBytes=0;
    int compressedByteCounter = 0;
    int zerr;
    unsigned char *dstBuf;
    static int isStreamSet = NO;

  log_debug(" Block [%d] deflating now.. inlen[%ld] isStreamSet %d ", blk, inLen, isStreamSet );
  if(blk == BEGIN_BLK && !isStreamSet){

        memset(&d_zstrm, '\0', sizeof(z_stream));
        d_zstrm.zalloc = Z_NULL;
        d_zstrm.zfree = Z_NULL;
        d_zstrm.opaque = Z_NULL;

        if ((zerr = deflateInit(&d_zstrm, Z_BEST_COMPRESSION)) != Z_OK) {
                log_error_q("ERROR %d deflateInit (%s)", zerr, decode_zlib_err(zerr));
                return -1;
        }
      isStreamSet = YES;
      return 0;
  }

   if(blk == END_BLK && isStreamSet){
        if ((zerr = deflateEnd(&d_zstrm)) != Z_OK) {
                log_error_q("ERROR %d deflateEnd (%s)", zerr, decode_zlib_err(zerr));
                return -1;
        }
     isStreamSet = NO;
     log_debug(" Calling deflateEnd to close deflate stream zerr = %d", zerr);
   }

    dstBuf = (unsigned char *) outBuf;

  totalBytesComp = 0;
 /************** NEW CODE   - FROM JAVA ********************/

   while(totalBytesComp != inLen ) {
      int compChunkSize = ((inLen - totalBytesComp) > 5120) ? 5120 :
                                                  (inLen - totalBytesComp);
      memcpy(in, inBuf + totalBytesComp, compChunkSize);

      d_zstrm.avail_in = compChunkSize; /*srcLen - totalBytesComp;*/
      d_zstrm.next_in = in ;

      d_zstrm.avail_out = CHUNK_SZ;
      d_zstrm.next_out = out;

      totalBytesComp += compChunkSize;
      flush = (totalBytesComp >= inLen) ? Z_FINISH : Z_NO_FLUSH;

       do  {
         ret  = deflate(&d_zstrm, flush);
          if (ret < 0) {
              log_error_q("FAIL %d delate (%s)", ret, decode_zlib_err(ret));
              (void)deflateEnd(&d_zstrm);
               isStreamSet = NO;
               return (ret);
          }

         compressedBytes = CHUNK_SZ - d_zstrm.avail_out;
         if(compressedBytes == 0) {
              log_debug("\n Unable to compress data - truncated");
              break;
         }

          compressedByteCounter += compressedBytes;

          memcpy(dstBuf + savedByteCntr, out, compressedBytes);
          savedByteCntr = compressedByteCounter;
          log_debug(" aval_in [%ld] after deflate.. inlen[%ld] compBytes [%ld]", d_zstrm.avail_in, inLen, compressedBytes);
         if(d_zstrm.avail_in > 0){
          int ret1;
            memmove(in, d_zstrm.next_in, d_zstrm.avail_in);
            totalBytesComp += d_zstrm.avail_in;
            d_zstrm.avail_out = CHUNK_SZ;
            d_zstrm.next_out = out;
            ret1  = deflate(&d_zstrm, flush);
            if (ret1 < 0){  /* state not clobbered */
              log_error_q("FAIL %d delate (%s) ", ret, decode_zlib_err(ret1));
              deflateEnd(&d_zstrm);
              isStreamSet = NO;
              return (ret);
            }

            compressedBytes =  d_zstrm.avail_out;
            compressedByteCounter += compressedBytes;
            memcpy(dstBuf + savedByteCntr, out, compressedBytes);
            savedByteCntr = compressedByteCounter;
         }

       }while(ret == Z_FINISH);

       deflateReset(&d_zstrm);
      }
 
     
  *outLen = compressedByteCounter;
   return 0; 
}

/*******************************************************************************
FUNCTION NAME
        int prod_get_WMO_offset(char *buf, size_t buflen, size_t *p_wmolen)

FUNCTION DESCRIPTION
        Parse the wmo heading from buffer and load the appropriate prod
        info fields.  The following regular expressions will satisfy this
        parser.  Note this parser is not case sensative.

        The WMO format is supposed to be...

        TTAAii CCCC DDHHMM[ BBB]\r\r\n
        [NNNXXX\r\r\n]

        This parser is generous with the ii portion of the WMO and all spaces
        are optional.  The TTAAII, CCCC, and DDHHMM portions of the WMO are
        required followed by at least 1 <cr> or <lf> with no other unparsed
        intervening characters. The following quasi-grammar describe what
        is matched.

        WMO = "TTAAII CCCC DDHHMM [BBB] CRCRLF [NNNXXX CRCRLF]"

        TTAAII = "[A-Z]{4}[0-9]{0,1,2}" | "[A-Z]{4} [0-9]" | "[A-Z]{3}[0-9]{3} "
        CCCC = "[A-Z]{4}"
        DDHHMM = "[ 0-9][0-9]{3,5}"
        BBB = "[A-Z0-9]{0-3}"
        CRCRLF = "[\r\n]+"
        NNNXXX = "[A-Z0-9]{0,4-6}"

        Most of the WMO's that fail to be parsed seem to be missing the ii
        altogether or missing part or all of the timestamp (DDHHMM)

PARAMETERS
        Type                    Name            I/O             Description
        char *                  buf                     I               buffer to parse for WMO
        size_t                  buflen          I               length of data in buffer
RETURNS
         offset to WMO from buf[0]
        -1: otherwise
*******************************************************************************/



#define WMO_TTAAII_LEN          6
#define WMO_CCCC_LEN            4
#define WMO_DDHHMM_LEN          6
#define WMO_DDHH_LEN            4
#define WMO_BBB_LEN                     3

#define WMO_T1  0
#define WMO_T2  1
#define WMO_A1  2
#define WMO_A2  3
#define WMO_I1  4
#define WMO_I2  5

#define NNN_LEN                 3
#define XXX_LEN                 3
#define AWIPSID_LEN             WMO_CCCC_LEN + NNN_LEN + XXX_LEN
#define MAX_SECLINE_LEN         40


static int prod_get_WMO_offset(char *buf, size_t buflen, size_t *p_wmolen)
{
        char *p_wmo;
        int i_bbb;
        int spaces;
        int     ttaaii_found = 0;
        int     ddhhmm_found = 0;
        int     crcrlf_found = 0;
        int     bbb_found = 0;
        int wmo_offset = -1;

        *p_wmolen = 0;

        for (p_wmo = buf; p_wmo + WMO_I2 + 1 < buf + buflen; p_wmo++) {
                if (isalpha(p_wmo[WMO_T1]) && isalpha(p_wmo[WMO_T2])
                                && isalpha(p_wmo[WMO_A1]) && isalpha(p_wmo[WMO_A2])) {
                        /* 'TTAAII ' */
                        if (isdigit(p_wmo[WMO_I1]) && isdigit(p_wmo[WMO_I2])
                                        && (isspace(p_wmo[WMO_I2+1]) || isalpha(p_wmo[WMO_I2+1]))) {
                                ttaaii_found = 1;
                                wmo_offset = p_wmo - buf;
                                p_wmo += WMO_I2 + 1;
                                break;
                        /* 'TTAAI C' */
                        } else if (isdigit(p_wmo[WMO_I1]) && isspace(p_wmo[WMO_I2])
                                        && (isspace(p_wmo[WMO_I2+1]) || isalpha(p_wmo[WMO_I2+1]))) {
                                ttaaii_found = 1;
                                wmo_offset = p_wmo - buf;
                                p_wmo += WMO_I1 + 1;
                                break;
                        /* 'TTAA I ' */
                        } else if (isspace(p_wmo[WMO_I1]) && isdigit(p_wmo[WMO_I2])
                                        && (isspace(p_wmo[WMO_I2+1]) || isalpha(p_wmo[WMO_I2+1]))) {
                                ttaaii_found = 1;
                                wmo_offset = p_wmo - buf;
                                p_wmo += WMO_I2 + 1;
                                break;
                        /* 'TTAAIC' */
                        } else if (isdigit(p_wmo[WMO_I1]) && isalpha(p_wmo[WMO_I2])) {
                                ttaaii_found = 1;
                                wmo_offset = p_wmo - buf;
                                p_wmo += WMO_I1 + 1;
                                break;
                        }
                } else if (isalpha(p_wmo[WMO_T1]) && isalpha(p_wmo[WMO_T2])
                                && isalpha(p_wmo[WMO_A1]) && isdigit(p_wmo[WMO_A2])) {
                        /* 'TTA#II ' */
                        if (isdigit(p_wmo[WMO_I1]) && isdigit(p_wmo[WMO_I2])
                                        && (isspace(p_wmo[WMO_I2+1]) || isalpha(p_wmo[WMO_I2+1]))) {
                                ttaaii_found = 1;
                                wmo_offset = p_wmo - buf;
                                p_wmo += WMO_I2 + 1;
                                break;
                        }
                } else if (!strncmp(p_wmo, "\r\r\n", 3)) {
                        /* got to EOH with no TTAAII found, check TTAA case below */
                        break;
                }
        }

        if (!ttaaii_found) {
                /* look for TTAA CCCC DDHHMM */
                for (p_wmo = buf; p_wmo + 9 < buf + buflen; p_wmo++) {
                        if (isalpha(p_wmo[WMO_T1]) && isalpha(p_wmo[WMO_T2])
                                        && isalpha(p_wmo[WMO_A1]) && isalpha(p_wmo[WMO_A2])
                                        && isspace(p_wmo[WMO_A2+1]) && isalpha(p_wmo[WMO_A2+2])
                                        && isalpha(p_wmo[WMO_A2+3]) && isalpha(p_wmo[WMO_A2+4])
                                        && isalpha(p_wmo[WMO_A2+5]) && isspace(p_wmo[WMO_A2+6])) {
                                wmo_offset = p_wmo - buf;
                                p_wmo += WMO_A2 + 1;
                                break;
                        } else if (!strncmp(p_wmo, "\r\r\n", 3)) {
                                /* got to EOH with no TTAA found, give up */
                                return -1;
                        }
                }
        }

        /* skip spaces if present */
        while (isspace(*p_wmo) && p_wmo < buf + buflen) {
                p_wmo++;
        }

        if (p_wmo + WMO_CCCC_LEN > buf + buflen) {
                return -1;
        } else if (isalpha(*p_wmo) && isalnum(*(p_wmo+1))
                        && isalpha(*(p_wmo+2)) && isalnum(*(p_wmo+3))) {
                p_wmo += WMO_CCCC_LEN;
        } else {
                return -1;
        }

        /* skip spaces if present */
        spaces = 0;
        while (isspace(*p_wmo) && p_wmo < buf + buflen) {
                p_wmo++;
                spaces++;
        }

        /* case1: check for 6 digit date-time group */
        if (p_wmo + 6 <= buf + buflen) {
                if (isdigit(*p_wmo) && isdigit(*(p_wmo+1))
                                && isdigit(*(p_wmo+2)) && isdigit(*(p_wmo+3))
                                && isdigit(*(p_wmo+4)) && isdigit(*(p_wmo+5))) {
                        ddhhmm_found = 1;
                        p_wmo += 6;
                }
        }

        /* case2: check for 4 digit date-time group */
        if (!ddhhmm_found && p_wmo + 5 <= buf + buflen) {
                if (isdigit(*p_wmo) && isdigit(*(p_wmo+1))
                                && isdigit(*(p_wmo+2)) && isdigit(*(p_wmo+3))
                                && isspace(*(p_wmo+4))) {
                        ddhhmm_found = 1;
                        p_wmo += 4;
                }
        }

        /* case3: check for leading 0 in date-time group being a space */
        if (!ddhhmm_found && p_wmo + 5 <= buf + buflen) {
                if (spaces > 1 && isdigit(*p_wmo) && isdigit(*(p_wmo+1))
                                && isdigit(*(p_wmo+2)) && isdigit(*(p_wmo+3))
                                && isdigit(*(p_wmo+4))) {
                        p_wmo += 5;
                } else {
                        return -1;
                }
        }

        /* skip potential trailing 'Z' on dddhhmm */
        if (*p_wmo == 'Z') {
                p_wmo++;
        }
        /* Everything past this point is gravy, we'll return the current
           length if we don't get the expected [bbb] crcrlf
         */

        /* check if we have a <cr> and/or <lf>, parse bbb if present */
        while (p_wmo < buf + buflen) {
                if ((*p_wmo == '\r') || (*p_wmo == '\n')) {
                        crcrlf_found++;
                        p_wmo++;
                        if (crcrlf_found == 3) {
                                /* assume this is our complete cr-cr-lf */
                                break;
                        }
                } else if (crcrlf_found) {
                        /* pre-mature end of crcrlf */
                        p_wmo--;
                        break;
                } else if (isalpha(*p_wmo)) {
                        if (bbb_found) {
                                /* already have a bbb, give up here */
                                return wmo_offset;
                        }
                        for (i_bbb = 1; p_wmo + i_bbb < buf + buflen && i_bbb < WMO_BBB_LEN; i_bbb++) {
                                if (!isalpha(p_wmo[i_bbb])) {
                                        break; /* out of bbb parse loop */
                                }
                        }
                        if (p_wmo + i_bbb < buf + buflen && isspace(p_wmo[i_bbb])) {
                                bbb_found = 1;
                                p_wmo += i_bbb;
                        } else {
                                /* bbb is too long or maybe not a bbb at all, give up */
                                return wmo_offset;
                        }
                } else if (isspace(*p_wmo)) {
                        p_wmo++;
                } else {
                        /* give up */
                        return wmo_offset;
                }
        }

        /* update length to include bbb and crcrlf */
        *p_wmolen = p_wmo - buf - wmo_offset;

        return wmo_offset;
} /* end prod_get_WMO_offset() */

/*************************************************************************

FUNCTION NAME
        prod_get_WMO_nnnxxx_offset - ACQ product library routine

FUNCTION DESCRIPTION
        Get the offset and lenght of the NNNXXX following the WMO header The
        NNNXXX may follow the cr-cr-lf WMO termination.  Some checks are
        made to ensure that what follows the WMO header is indeed an NNNXXX.

PARAMETERS
        Type    Name                    I/O     Description
        char *  wmo_buff                I   pointer to start of wmo header in the data
        int     max_search              I       max len for WMO search in buff
        int *   p_len                   O       len for the nnnxxx including crcrlf

RETURNS
        0 for success -1 for failure

*******************************************************************************/

static int prod_get_WMO_nnnxxx_offset (
        char *wmo_buff,     /* pointer to start of wmo header in the data buffer */
        int max_search,         /* max len for WMO search in buff */
        int *p_len)                     /* len for the nnnxxx */
{
        char *  p_nnnxxx;
        int             offset;
        int             eow_flag;
        int             eoh_flag;
        int             fill_flag;

        *p_len = 0;
        eow_flag = 0;
        for (p_nnnxxx = wmo_buff; p_nnnxxx <= wmo_buff+max_search; p_nnnxxx++) {
                if (*p_nnnxxx == '\n' || *p_nnnxxx == '\r') {
                        eow_flag = 1;
                        if (!strncmp(p_nnnxxx, "\r\r\n", 3)) {
                                p_nnnxxx += 3;
                                break;
                        }
                }
                else if (eow_flag) {
                        break;
                }
        }

        if (!eow_flag) {
                return -1;
        }

        offset = p_nnnxxx - wmo_buff;

        fill_flag = 0;
        eoh_flag = 0;
        for (*p_len = 0; p_nnnxxx <= wmo_buff+max_search; p_nnnxxx++, (*p_len)++) {
                /* maximum length check */
                if (*p_len > NNN_LEN + XXX_LEN && !eoh_flag) {
                        return -1;
                }
                /* NNNXXX may contain fill characters */
                if (*p_nnnxxx == ' ') {
                        fill_flag = 1;
                }
                /* loose check for crcrlf terminator */
                else if (*p_nnnxxx == '\n' || *p_nnnxxx == '\r') {
                        eoh_flag = 1;
                }
                /* we at least got a cr or a lf so assume the header is OK */
                else if (eoh_flag) {
                        return offset;
                }
                /* Found an embedded space -- assume this is not an NNNXXX */
                else if (fill_flag) {
                        return -1;
                }
                /* NNNXXX must be all upper-case alpha-numeric */
                else if (!(isalpha(*p_nnnxxx) && isupper(*p_nnnxxx))
                                && !isdigit(*p_nnnxxx)) {
                        return -1;
                }

                /* minimum length check, NNNXXX may contain a fill character or 2 (LEK) */
                if ((fill_flag || eoh_flag) && *p_len < NNN_LEN + XXX_LEN - 2) {
                        return -1;
                }

                /* check for official terminator, if we found it we are done */
                if (eoh_flag && !strncmp(p_nnnxxx, "\r\r\n", 3)) {
                        (*p_len)+=3;
                        return offset;
                }
        }
        return -1;
}

static char *decode_zlib_err(int err)
{
        static struct {
                int             code;
                char *  desc;
        } errtab[] = {
                {       Z_OK,                           "OK"                    },
                {       Z_STREAM_END,           "STREAM_END"    },
                {       Z_NEED_DICT,            "NEED_DICT"             },
                {       Z_ERRNO,                        "ERRNO"                 },
                {       Z_STREAM_ERROR,         "STREAM_ERROR"  },
                {       Z_DATA_ERROR,           "DATA_ERROR"    },
                {       Z_MEM_ERROR,            "MEM_ERROR"             },
                {       Z_BUF_ERROR,            "BUF_ERROR"             },
                {       Z_VERSION_ERROR,        "VERSION_ERROR" },
                {       0,                                      ""                              }
        };

        int i;

        for (i = 0; *errtab[i].desc ; i++) {
                if (err == errtab[i].code) {
                        break;
                }
        }

        if (errtab[i].code == Z_ERRNO) {
                return strerror(errno);
        } else {
                return errtab[i].desc;
        }
}

static int getIndex(char *arr, int pos, int sz)
{
  int index = -1;
  int ii;

  for(ii = pos; ii < sz; ii++) {
    if(arr[ii] == -1) {
      index = ii;
      break;
    }
  }

  if(index != -1 && (index + 3 <= sz -1)) {
    if(!(arr[index] == -1 && arr[index + 1] == 0 &&
          arr[index + 2] == -1 && arr[index + 3] == 0)) {
            index = getIndex(arr, index+1, sz);
    }
  }else {
         index = -1;
  }

  return index;

}

