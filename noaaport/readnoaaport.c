/*
 *   Copyright 2014, University Corporation for Atmospheric Research.
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
/**
 *   @file readnoaaport.c
 *
 *   This file contains the code for the \c readnoaaport(1) program. This
 *   program reads NOAAPORT data from a shared-memory FIFO or a file, creates
 *   LDM data-products, and writes the data-products into an LDM product-queue.
 */
#include "config.h"

#include "nport.h"
#include "shmfifo.h"

#include "ldm.h"
#include "globals.h"
#include "md5.h"
#include "setenv.h"
#include "log.h"
#include "dvbs.h"
#include "ldmProductQueue.h"

#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

/*
 * Function prototypes
 */
size_t prodalloc(long int nfrags, long int dbsize, char **heap);
void ds_init(int nfrags);
void ds_free();
datastore *ds_alloc();
void pngout_end();
void process_prod(prodstore prod, char *PROD_NAME,
		   char *memheap, size_t heapsize, MD5_CTX * md5try,
		   LdmProductQueue* lpq, psh_struct * psh, sbn_struct * sbn);
void pngwrite(char *memheap);
void png_set_memheap(char *memheap, MD5_CTX * md5ctxp);
void png_header(char *memheap, int length);
void pngout_init(int width, int height);
int png_get_prodlen();
int prod_isascii(char *pname, char *prod, size_t psize);

static LdmProductQueue*         ldmProdQueue = NULL;
static unsigned long            idle = 0;
static fd_set                   readfds;
static fd_set                   exceptfds;
static int                      DONE = 0;
static const char*              FOS_TRAILER = "\015\015\012\003"; // CR CR LF ETX = ^M ^M ^J ^C
static struct shmhandle*        shm = NULL;
/*
 * Output statistics if requested
 */
static volatile sig_atomic_t    logstats = 0;
static unsigned long            nmissed = 0;


static void dump_stats(void)
{
    log_notice_q("----------------------------------------\0");
    log_notice_q("Ingestion Statistics:\0");
    log_notice_q("   Number of missed packets %lu\0", nmissed);
    log_notice_q("----------------------------------------\0");
}

static void cleanup(void)
{
    log_notice_q("Exiting...\0");
    dump_stats();
    (void)lpqClose(ldmProdQueue);

    if (shm != NULL) {
       shmfifo_detach(shm);
       shmfifo_free(shm);

       shm = NULL;
    }
}

/*
 * Called upon receipt of signals
 */
static void signal_handler(
    int sig)
{
    switch (sig) {
      case SIGINT:
        exit(0);
      case SIGTERM:
        exit(0);
      case SIGPIPE:
        return;
      case SIGUSR1:
        log_refresh();
        logstats = 1;
        return;
      case SIGUSR2:
        log_roll_level();
        return;
    }
}

/*
 * Register the signal_handler
 */
static void set_sigactions(void)
{
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Ignore these */
    sigact.sa_handler = SIG_IGN;
    (void)sigaction(SIGALRM, &sigact, NULL);
    (void)sigaction(SIGCHLD, &sigact, NULL);

    /* Handle these */
#ifdef SA_RESTART		/* SVR4, 4.3+ BSD */
    /* usually, restart system calls */
    sigact.sa_flags |= SA_RESTART;
#endif
    sigact.sa_handler = signal_handler;
    (void)sigaction(SIGTERM, &sigact, NULL);
    (void)sigaction(SIGUSR1, &sigact, NULL);
    (void)sigaction(SIGUSR2, &sigact, NULL);

    /* Don't restart after interrupt */
    sigact.sa_flags = 0;
#ifdef SA_INTERRUPT		/* SunOS 4.x */
    sigact.sa_flags |= SA_INTERRUPT;
#endif
    (void)sigaction(SIGINT, &sigact, NULL);
    (void)sigaction(SIGPIPE, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGALRM);
    (void)sigaddset(&sigset, SIGCHLD);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigaddset(&sigset, SIGPIPE);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

static void usage(
    const char* const   av0)		/*  id string */
{
    (void)fprintf(stderr,
"Usage: %s [options] feedname\t\nOptions:\n", av0);
    (void)fprintf(stderr,
"\t-v           Verbose, tell me about each product\n");
    (void)fprintf(stderr,
"\t-n           Log notice messages\n");
    (void)fprintf(stderr,
"\t-x           Log debug messages\n");
    (void)fprintf(stderr,
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
    (void)fprintf(stderr,
"\t-f type      Claim to be feedtype \"type\", "
"one of \"hds\", \"ddplus\", ...\n");
    (void)fprintf(stderr,
"\t-q queue     default \"%s\"\n", getDefaultQueuePath());
    (void)fprintf(stderr,
"\t-u number    default LOCAL0\n");
    exit(1);
}

/*
 * Reads data from the FIFO to a buffer.
 *
 * Arguments:
 *      buf             Pointer to the buffer into which to put the data.
 *      want            The amount of data to read in bytes.
 * Returns:
 *      0               Success.
 *      -1              Pre-condition failure. Error-message logged.
 *      -2              I/O error. Error-message logged.
 */
static int _shm_bufread(
    char* const buf,
    const int   want)
{
    int         status;                 /* return status */

    if (shm == NULL) {
        log_error_q("NULL shared-memory pointer");
        DONE = 1;
        status = -1;                    /* pre-condition failure */
    }
    else if (NULL == buf) {
        log_error_q("NULL buffer pointer");
        status = -1;                    /* pre-condition failure */
    }
    else if (0 > want) {
        log_error_q("Negative number of bytes to read: %d", want);
        status = -1;                    /* pre-condition failure */
    }
    else {
        int     got;                    /* count of "buf" bytes */

        log_debug("shm_bufread %d", want);

        status = 0;                     /* success */

        for (got = 0; got < want; ) {
            static char         msgbuf[10000];  /* I/O buffer */
            static char*        from;           /* next "msgbuf" byte */
            static int          left = 0;       /* remaining "msgbuf" bytes */
            int                 ncopy;          /* number of bytes to copy */

            if (left == 0) {
                if (shmfifo_get(shm, msgbuf, sizeof(msgbuf), &left) != 0) {
                    status = -2;        /* I/O error */

                    break;
                }

                from = msgbuf; 
            }

            ncopy = want - got;

            if (ncopy > left) {
                log_error_q("Can \"want\" exceed 1 packet?");
                ncopy = left;
            }

            (void)memcpy(buf + got, from, ncopy);

            left -= ncopy;
            from += ncopy ;
            got += ncopy;
        }                               /* while more data needed */
    }                                   /* pre-conditions satisfied */

    return status;
}

/*
 * Reads data from a file descriptor.
 *
 * Arguments:
 *      fd              The file descriptor from which to read data.
 *      buf             Pointer to the buffer into which to put the data.
 *      want            The amount of data to read in bytes.
 * Returns:
 *      0               Success.
 *      -2              I/O error. Error-message logged.
 *      -3              End-of-file.
 */
static int _fd_bufread(
    const int   fd,
    char* const buf,
    const int   bsiz)
{
    int                 width;
    int                 ready;
    struct timeval      timeo;
    size_t              bread = 0;
    static int          TV_SEC = 30;

    width = fd + 1;

    while (bread < bsiz) {
        timeo.tv_sec = TV_SEC;
        timeo.tv_usec = 0;

        FD_ZERO(&readfds);
        FD_ZERO(&exceptfds);
        FD_SET(fd, &readfds);
        FD_SET(fd, &exceptfds);

        ready = select(width, &readfds, 0, &exceptfds, &timeo);
        /* timeo may be modified, don't rely on value now */

        if (ready < 0) {
            if (errno != EINTR)
              log_syserr("select");
            else
              log_notice_q("select received interupt\0");

            errno = 0;
            continue;
        }


        if (ready == 0) {
            idle += TV_SEC;

            if (idle > 600) {
                if (log_is_enabled_info)
                    log_info_q("Idle for 600 seconds");
                idle = 0;
            }
            else {
                log_debug("Idle for %d seconds", idle);
            }

            continue;		/* was return(-1) */
        }

        if (FD_ISSET(fd, &readfds) || FD_ISSET(fd, &exceptfds)) {
            size_t      nread;

            idle = 0;
            nread = read(fd, buf + bread, (size_t) bsiz - bread);
            bread = bread + nread;
            
            if (nread == -1) {
                log_syserr("_fd_bufread(): read() failure");
                return -2;
            }
            if (nread == 0) {
                if (log_is_enabled_info)
                  log_info_q("End of Input");

                DONE = 1;

                return -3;
            }
            if (bread < (size_t) bsiz) {
                FD_CLR(fd, &readfds);
                FD_CLR(fd, &exceptfds);
            }
            else {
              return 0;
            }
        }
        else {
            log_error_q("select() returned %d but fd not set", ready);

            idle += TV_SEC;

            return -2;
        }

    }

    return 0;
}

/*
 * Reads data.
 *
 * Arguments:
 *      fd              The file descriptor from which to read data if "shm"
 *                      is NULL.
 *      buf             Pointer to the buffer into which to put the data.
 *      want            The amount of data to read in bytes.
 * Returns:
 *      0               Success.
 *      -1              Pre-condition failure. Error-message logged.
 *      -2              I/O error. Error-message logged.
 *      -3              End-of-file.
 */
static int bufread(
    const int   fd,
    char* const buf,
    const int   want)
{
    return (shm == NULL)
        ? _fd_bufread(fd, buf, want)
        : _shm_bufread(buf, want);
}

/**
 * Reads NOAAPORT data from a shared-memory FIFO or a file, creates LDM
 * data-products, and inserts the data-products into an LDM product-queue.
 *
 * Usage:
 *
 *     readnoaaport [-nvx] [-q <em>queue</em>] [-u <em>n</em>] [-m mcastAddr] [path]\n
 *
 * Where:
 * <dl>
 *      <dt>-l <em>log</em></dt>
 *      <dd>Log to \e log. if \e log is "-", then logging occurs to the 
 *      standard error stream; otherwise, \e log is the pathname of a file to
 *      which logging will occur. If not specified, then log messages will go
 *      to the system logging daemon. </dd>
 *
 *      <dt>-m <em>mcastAddr</em></dt>
 *      <dd>Use the shared-memory FIFO associated with the UDP
 *      multicast address \e mcastAddr.</dd>
 *
 *      <dt>-n</dt>
 *      <dd>Log messages of level NOTICE and higher priority.</dd>
 *
 *      <dt>-q <em>queue</em></dt>
 *      <dd>Use \e queue as the pathname of the LDM product-queue. The default
 *      is to use the default LDM pathname of the product-queue.</dd>
 *
 *      <dt>-u <em>n</em></dt>
 *      <dd>If logging is to the system logging daemon, then use facility 
 *      <b>local</b><em>n</em>. The default is to use the LDM facility.
 *
 *      <dt>-v</dt>
 *      <dd>Log messages of level INFO and higher priority. Each data-product
 *      will generate a log message.</dd>
 *
 *      <dt>-x</dt>
 *      <dd>Log messages of level DEBUG and higher priority.</dd>
 *
 *      <dt><em>path</em></dt>
 *      <dd>Pathname of the file from which to read data. The default is to use
 *      a shared-memory FIFO.</dd>
 * </dl>
 *
 * @retval 0 if successful.
 * @retval 1 if an error occurred. At least one error-message is logged.
 */
int main(
     const int          argc,
     char* const        argv[])
{
    int                 fd;
    char*               prodmmap;
    char*               memheap = NULL;
    size_t              heapsize;
    size_t              heapcount;
    unsigned char       b1;
    int                 cnt, dataoff, datalen, deflen;
    int                 nscan;
    long                IOFF;
    int                 NWSTG, GOES, PNGINIT = 0, PROD_COMPRESSED;
    long                last_sbn_seqno = (-1);
    char                PROD_NAME[1024];
    int                 status;
    prodstore           prod;
    sbn_struct*         sbn;
    pdh_struct*         pdh;
    psh_struct*         psh;
    ccb_struct*         ccb;
    pdb_struct*         pdb;
    datastore*          pfrag;
    extern int          optind;
    extern int          opterr;
    extern char*        optarg;
    int                 ch;
    MD5_CTX*            md5ctxp = NULL;
    /*unsigned char *compr;
    long                comprLen = 10000 * sizeof (int);*/
    /*
     * The following is *not* the DVB PID: it's the least significant byte of
     * the IPv4 multicast address (e.g., the "3" in "224.0.1.3").
     */
    int                 pid_channel = -1;

    /*compr = (unsigned char *) calloc (comprLen, 1);*/

    /* Initialize the logger. */
    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }
    (void)log_set_level(LOG_LEVEL_ERROR);

    const char*         pqfname = getQueuePath();

    opterr = 1;
    while ((ch = getopt(argc, argv, "nvxl:q:u:m:")) != EOF) {
        switch (ch) {
        case 'v':
            if (!log_is_enabled_info)
                (void)log_set_level(LOG_LEVEL_INFO);
            break;
        case 'x':
            (void)log_set_level(LOG_LEVEL_DEBUG);
            break;
        case 'n':
            if (!log_is_enabled_notice)
                (void)log_set_level(LOG_LEVEL_NOTICE);
            break;
        case 'l':
            if (optarg[0] == '-' && optarg[1] != 0) {
                log_error_q("logfile \"%s\" ??\n", optarg);
                usage(argv[0]);
            }
            /* else */
            if (log_set_destination(optarg)) {
                log_syserr("Couldn't set logging destination to \"%s\"",
                        optarg);
                exit(1);
            }
            break;
        case 'q':
            pqfname = optarg;
            break;
        case 'u': {
            int         i = atoi(optarg);

            if (0 <= i && 7 >= i) {
                static int  logFacilities[] = {LOG_LOCAL0, LOG_LOCAL1,
                    LOG_LOCAL2, LOG_LOCAL3, LOG_LOCAL4, LOG_LOCAL5, LOG_LOCAL6,
                    LOG_LOCAL7};
                (void)log_set_facility(logFacilities[i]);
            }

            break;
        }
        case 'm': {
            int nbytes;
            if (sscanf(optarg, "%*d.%*d.%*d.%d %n", &pid_channel, &nbytes) != 1
                    || optarg[nbytes] != 0 || (pid_channel < 1) ||
                    (pid_channel > MAX_DVBS_PID)) {
                pid_channel = -1;
            }
            else {  
                shm = shmfifo_new();
                cnt = 0;

                while (((status = shmfifo_shm_from_key(shm, 
                          s_port[pid_channel - 1])) == -3) && (cnt < 30)) {
                    log_info_q("Trying to get shared-memory FIFO");
                    cnt++;
                    sleep(1);
                }

                if (0 != status) {
                    log_error_q("Couldn't get shared-memory FIFO. "
                            "Check associated dvbs_multicast(1) process.");
                    shmfifo_free(shm);
                    shm = NULL;
                }
                else {
                    log_info_q("Got shared-memory FIFO");
                }
            }
            break;
        }
        case '?':
            usage(argv[0]);
            break;
        }
    }


    if (argc - optind < 0)
        usage(argv[0]);

    setQueuePath(pqfname);

    log_notice_q("Starting Up %s", PACKAGE_VERSION);

    fd = ((argc - optind) == 0)
        ? fileno(stdin)
        : open(argv[optind], O_RDONLY, 0);

    if ((!shm) && (fd == -1)) {
        log_error_q("could not open input file");
        exit(0);
    }

    /*
     * Set up signal handlers
     */
    set_sigactions();

    /*
     * Register atexit routine
     */
    if (atexit(cleanup) != 0) {
        log_syserr("atexit");
        exit(-1);
    }

    sbn = (sbn_struct*)malloc(sizeof(sbn_struct));
    pdh = (pdh_struct*)malloc(sizeof(pdh_struct));
    psh = (psh_struct*)malloc(sizeof(psh_struct));
    ccb = (ccb_struct*)malloc(sizeof(ccb_struct));
    pdb = (pdb_struct*)malloc(sizeof(pdb_struct));
    prodmmap = (char*)malloc(10000);

    if (prodmmap == NULL) {
        log_error_q("could not allocate read buffer");
        exit(-1);
    }

    md5ctxp = new_MD5_CTX();
    prod.head = NULL;
    prod.tail = NULL;

    if (lpqGet(pqfname, &ldmProdQueue) != 0) {
        log_add("Couldn't open LDM product-queue \"%s\"", pqfname);
        exit(1);
    }

    while (DONE == 0) {
        /* See if any stats need to be logged */
        if (logstats) {
            logstats = 0;
            dump_stats();
        }

        /* Look for first byte == 255  and a valid SBN checksum */
        if ((status = bufread(fd, prodmmap, 1)) != 0) {
            if (-3 == status)
                break;
            abort();
        }
        if ((b1 = (unsigned char)prodmmap[0]) != 255) {
            if (log_is_enabled_info)
                log_info_q("trying to resync %u", b1);
            log_debug("bufread loop");
            continue;
        }

        if (bufread(fd, prodmmap + 1, 15) != 0) {
            log_debug("couldn't read 16 bytes for sbn");
            continue;
        }

        while ((status = readsbn(prodmmap, sbn)) != 0) {
            log_debug("Not SBN start");

            IOFF = 1;

            while ((IOFF < 16) && ((b1 = (unsigned char) prodmmap[IOFF]) !=
                        255))
                IOFF++;

            if (IOFF > 15) {
                break;
            }
            else {
                for (ch = IOFF; ch < 16; ch++)
                    prodmmap[ch - IOFF] = prodmmap[ch];

                if (bufread(fd, prodmmap + 16 - IOFF, IOFF) != 0) {
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

        if (bufread(fd, prodmmap + 16, 16) != 0) {
            log_debug("error reading Product Definition Header");
            continue;
        }

        log_debug("***********************************************");
        if (last_sbn_seqno != -1) {
            if (sbn->seqno != last_sbn_seqno + 1) {
                log_notice_q("Gap in SBN sequence number %ld to %ld [skipped %ld]",
                         last_sbn_seqno, sbn->seqno,
                         sbn->seqno - last_sbn_seqno - 1);
                if ( sbn->seqno > last_sbn_seqno )
                    nmissed = nmissed + 
                        (unsigned long)(sbn->seqno - last_sbn_seqno - 1);
            }
        }

        last_sbn_seqno = sbn->seqno;

        if (log_is_enabled_info)
            log_info_q("SBN seqnumber %ld", sbn->seqno);
        if (log_is_enabled_info)
            log_info_q("SBN datastream %d command %d", sbn->datastream,
                sbn->command);
        log_debug("SBN version %d length offset %d", sbn->version, sbn->len);
        if (((sbn->command != 3) && (sbn->command != 5)) || 
                (sbn->version != 1)) {
            log_error_q("Unknown sbn command/version %d PUNT", sbn->command);
            continue;
        }

	switch (sbn->datastream) {
		case 1:		/* GINI GOES */
		case 2:		/* GINI GOES (deprecated) */
		case 4:		/* OCONUS */
			NWSTG = 0;
			GOES = 1;
			break;
		case 3:		/* NWSTG1 - not used */
		case 5:		/* NWSTG */
		case 6:		/* NWSTG2 */
		case 7:		/* POLARSAT */
		case 8:		/* NOAA Weather Wire Service (NWWS) */
		case 9:		/* Reserved for NWS internal use - Data Delivery */
		case 10:	/* Reserved for NWS internal use - Encrypted */
		case 11:	/* Reserved for NWS internal use - Experimental */
		case 12:	/* GOES-R West */
		case 13:	/* GOES-R East */
			NWSTG = 1;
			GOES = 0;
			break;
		default:
			log_error_q("Unknown NOAAport channel %d PUNT", sbn->datastream);
			continue;
	}

        /* End of SBN version low 4 bits */

        if (readpdh(prodmmap + IOFF + sbn->len, pdh) == -1) {
            log_error_q("problem with pdh, PUNT");
            continue;
        }
        if (pdh->len > 16) {
            bufread(fd, prodmmap + sbn->len + 16, pdh->len - 16);
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

        log_debug("header length %ld [pshlen = %d]", pdh->len + pdh->pshlen,
                pdh->pshlen);
        log_debug("blocks per record %ld records per block %ld\n",
                pdh->blocks_per_record, pdh->records_per_block);
        log_debug("product seqnumber %ld block number %ld data block size "
                "%ld", pdh->seqno, pdh->dbno, pdh->dbsize);

        /* Stop here if no psh */
        if ((pdh->pshlen == 0) && (pdh->transtype == 0)) {
            //IOFF += sbn->len + pdh->len; // scan-build(1) says stored value isn't read
            continue;
        }

        if (pdh->pshlen != 0) {
            if (bufread(fd, prodmmap + sbn->len + pdh->len, pdh->pshlen) != 0) {
                log_error_q("problem reading psh");
                continue;
            }
            else {
                log_debug("read psh %d", pdh->pshlen);
            }

            /* Timing block */
            if (sbn->command == 5) {
                log_debug("Timing block recieved %ld %ld\0", psh->olen, pdh->len);
                /*
                 * Don't step on our psh of a product struct of prod in
                 * progress.
                 */
                continue;
            }

            if (readpsh(prodmmap + IOFF + sbn->len + pdh->len, psh) == -1) {
                log_error_q("problem with readpsh");
                continue;
            }
            if (psh->olen != pdh->pshlen) {
                log_error_q("ERROR in calculation of psh len %ld %ld", psh->olen,
                    pdh->len);
                continue;
            }
            log_debug("len %ld", psh->olen);
            log_debug("product header flag %d, version %d", psh->hflag,
                    psh->version);
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
            if (prod.head != NULL) {
                log_error_q("OOPS, start of new product [%ld ] with unfinished "
                    "product %ld", pdh->seqno, prod.seqno);

                ds_free();

                prod.head = NULL;
                prod.tail = NULL;

                if (PNGINIT != 0) {
                    pngout_end();
                    PNGINIT = 0;
                }

                log_error_q("Product definition header version %d pdhlen %d",
                        pdh->version, pdh->len);
                log_error_q("PDH transfer type %u", pdh->transtype);

                if ((pdh->transtype & 8) > 0)
                    log_error_q("Product transfer flag error %u", pdh->transtype);
                if ((pdh->transtype & 32) > 0)
                    log_error_q("Product transfer flag error %u", pdh->transtype);

                log_error_q("header length %ld [pshlen = %d]",
                    pdh->len + pdh->pshlen, pdh->pshlen);
                log_error_q("blocks per record %ld records per block %ld",
                    pdh->blocks_per_record, pdh->records_per_block);
                log_error_q("product seqnumber %ld block number %ld data block "
                    "size %ld", pdh->seqno, pdh->dbno, pdh->dbsize);
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

            if (bufread(fd, prodmmap + sbn->len + pdh->len + pdh->pshlen,
                    pdh->dbsize) != 0) {
                log_error_q("problem reading datablock");
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
                if (readpdb(prodmmap + IOFF + sbn->len + pdh->len + pdh->pshlen,
                        psh, pdb, PROD_COMPRESSED, pdh->dbsize) == -1) {
                    log_error_q("Error reading pdb, punt");
                    continue;
                }

                memcpy(PROD_NAME, psh->pname, sizeof(PROD_NAME));

                log_debug("Read GOES %d %d %d [%d] %d", sbn->len, pdh->len,
                        pdh->pshlen, sbn->len + pdh->len + pdh->pshlen,
                        pdb->len);

                /* Data starts at first block after pdb */
                ccb->len = 0;
                heapsize = prodalloc(psh->frags, 5152, &memheap);
            }
            if (NWSTG == 1) {
                memset(psh->pname, 0, sizeof(psh->pname));

                if (readccb(prodmmap + IOFF + sbn->len + pdh->len + pdh->pshlen,
                        ccb, psh, pdh->dbsize) == -1)
                    log_error_q("Error reading ccb, using default name");
                log_debug("look at ccb start %d %d", ccb->b1, ccb->len);

                /*
                   cnt = 0;
                   memset(psh->pname,0,sizeof(psh->pname));
                   while ((b1 = (unsigned char)prodmmap[
                           IOFF + sbn->len + pdh->len + pdh->pshlen + ccb->len +
                           cnt]) >= 32) {
                       psh->pname[cnt] = prodmmap[
                           IOFF + sbn->len + pdh->len + pdh->pshlen + ccb->len +
                           cnt];
                       cnt++;
                   } 
                   if(cnt > 0)
                 */
                if (log_is_enabled_info)
                    log_info_q("%s", psh->pname);

                memcpy(PROD_NAME, psh->pname, sizeof(PROD_NAME));

                heapsize = prodalloc(psh->frags, 4000 + 15, &memheap);
                /*
                 * We will only compute md5 checksum on the data, 11 FOS
                 * characters at start
                 */
                /*
                 * sprintf(memheap,"\001\015\015\012%04d\015\015\012",
                 * ((int)pdh->seqno)%10000);
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

            log_debug("continuation record");
            if ((pdh->transtype & 4) > 0) {
                psh->frags = 0;
            }
            if (bufread(fd, prodmmap + sbn->len + pdh->len + pdh->pshlen,
                    pdh->dbsize) != 0) {
                log_error_q("problem reading datablock (cont)");
                continue;
            }
            if (prod.head == NULL) {
                if (log_is_enabled_info)
                    log_info_q("found data block before header, "
                        "skipping sequence %d frag #%d", pdh->seqno, pdh->dbno);
                continue;
            }
        }

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

        /*memcpy(memheap+heapcount,prodmmap+dataoff,datalen);
        MD5Update(md5ctxp, (unsigned char *)(memheap+heapcount), datalen);
        test_deflate(compr,comprLen,(unsigned char *)(memheap+heapcount),
        datalen);*/

        if (GOES == 1) {
            if (pfrag->fragnum > 0) {
                if (prod.tail && ((pfrag->fragnum != prod.tail->fragnum + 1) ||
                        (pfrag->seqno != prod.seqno))) {
                    log_error_q("Missing GOES fragment in sequence, "
                        "last %d/%d this %d/%d\0", prod.tail->fragnum,
                        prod.seqno, pfrag->fragnum, pfrag->seqno);
                    ds_free();

                    prod.head = NULL;
                    prod.tail = NULL;

                    continue;
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
                          pngwrite(prodmmap + dataoff + (nscan * pdb->nx));
                        }
                    }
                }
                else {
                    memcpy(memheap + heapcount, prodmmap + dataoff, datalen);
                    MD5Update(md5ctxp, (unsigned char *) (memheap + heapcount),
                        datalen);
                    heapcount += datalen;
                }
            }
            else {
                if (!PROD_COMPRESSED) {
                    png_set_memheap(memheap, md5ctxp);
                    png_header(prodmmap + dataoff, datalen);
                    /*
                     * Add 1 to number of scanlines, image ends with 
                     * f0f0f0f0...
                     */
                    pngout_init(pdb->nx, pdb->ny + 1);

                    PNGINIT = 1;
                }
                else {
                    memcpy(memheap + heapcount, prodmmap + dataoff, datalen);
                    MD5Update(md5ctxp, (unsigned char*)(memheap + heapcount),
                        datalen);
                    heapcount += datalen;
                }
                log_notice_q("records_per_block %d blocks_per_record %d nx %d ny %d",
                    pdh->records_per_block, pdh->blocks_per_record, pdb->nx,
                    pdb->ny);
                log_notice_q("source %d sector %d channel %d", pdb->source,
                    pdb->sector, pdb->channel);
                log_notice_q("nrec %d recsize %d date %02d%02d%02d %02d%02d "
                    "%02d.%02d", pdb->nrec, pdb->recsize, pdb->year, pdb->month,
                    pdb->day, pdb->hour, pdb->minute, pdb->second,
                    pdb->sechunds);
                log_notice_q("pshname %s", psh->pname);
            }
            deflen = 0;
        }
        else {
            /*
             * test_deflate(memheap+heapcount,heapsize-heapcount,(unsigned char
             * *)(prodmmap+dataoff),datalen,&deflen);
             */
            /* If the product already has a FOS trailer, don't add
             * another....this will match what pqing(SDI) sees
             */
            if ((prod.nfrag != 0) && (prod.tail != NULL)) {
                if ((pfrag->fragnum != prod.tail->fragnum + 1) ||
                        (pfrag->seqno != prod.seqno)) {
                    log_error_q("Missing fragment in sequence, last %d/%d this "
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
                    memcpy(testme, prodmmap + (dataoff + datalen - 4), 4);

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
                continue;
            }

            memcpy(memheap + heapcount, prodmmap + dataoff, datalen);

            deflen = datalen;

            MD5Update(md5ctxp, (unsigned char *) (memheap + heapcount),
                deflen);
        }

        pfrag->recsiz = deflen;
        /*heapcount += datalen;*/
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
                    log_debug("GOES product already compressed %d", heapcount);
                }
            }
            if (log_is_enabled_info)
              log_info_q("we should have a complete product %ld %ld/%ld %ld /heap "
                  "%ld", prod.seqno, pfrag->seqno, prod.nfrag, pfrag->fragnum,
                 (long) heapcount);
            if ((NWSTG == 1) && (heapcount > 4)) {
                cnt = 4;		/* number of bytes to add for TRAILER */

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
                        psh->pcat += 100;	/* call these HDS */
                      /* else {
                         ** call these DDPLUS **
                         if (memheap[heapcount-1] == FOS_TRAILER[3]) **
                             ETX check **
                             cnt = 0; ** no need to add extra ETX pqing doesn't
                             see it **
                         }
                       */
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
                md5ctxp, ldmProdQueue, psh, sbn);
            ds_free();

            prod.head = NULL;
            prod.tail = NULL;
            PNGINIT = 0;
        }
        else {
            log_debug("processing record %ld [%ld %ld]", prod.seqno,
                    prod.nfrag, pfrag->fragnum);
            if ((pdh->transtype & 4) > 0) {
                log_error_q("Hmmm....should call completed product %ld [%ld %ld]",
                    prod.seqno, prod.nfrag, pfrag->fragnum);
            }
        }

        IOFF += (sbn->len + pdh->len + pdh->pshlen + pdh->dbsize);

        log_debug("look IOFF %ld datalen %ld (deflate %ld)", IOFF, datalen,
                deflen);
    }

    if (fd != -1)
       (void)close(fd);

    exit(0);
}
