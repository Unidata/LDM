/**
 * This file contains the code for the `noaaportIngester(1)` program. This
 * program reads NOAAPORT data from a file or multicast packet stream,
 * creates LDM data-products, and writes the data-products into an LDM
 * product-queue.
 *
 * Copyright © 2019, University Corporation for Atmospheric Research.
 * See COPYRIGHT file for copying and redistribution conditions.
 *
 * @file noaaportIngester.c
 */
#include <config.h>

#include "ldm.h"
#include "ldmfork.h"
#include "log.h"
#include "fifo.h"
#include "getFacilityName.h"
#include "globals.h"
#include "ldmProductQueue.h"
#include "noaaport_socket.h"
#include "productMaker.h"
#include "reader.h"

#include <zlib.h> /* Required for compress/uncompress */
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int lockProcessInMemory(void);

/*********** For Retransmission ****************/
#ifdef RETRANS_SUPPORT
#include "retrans.h"
#endif
/*********** For Retransmission ****************/

typedef struct {
    /** Maker of LDM data-products */
    ProductMaker*     productMaker;
    /** Reader of input */
    Reader*           reader;
    /** Time of start of execution */
    struct timeval    startTime;
    /** Time of last report */
    struct timeval    reportTime;
} StatsStruct;

static const int USAGE_ERROR = 1;
static const int SYSTEM_FAILURE = 2;
static const int SCHED_POLICY = SCHED_FIFO;
static Fifo*     fifo;
static pthread_t reporterThread = {0};
int              inflateFrame;
int              fillScanlines;

/**
 * Decodes the command-line.
 *
 * @param[in]  argc           Number of arguments.
 * @param[in]  argv           Arguments.
 * @param[out] npages         Size of input buffer in memory-pages.
 * @param[out] prodQueuePath  Pathname of product-queue.
 * @param[out] mcastSpec      Specification of multicast group.
 * @param[out] interface      Specification of interface on which to listen.
 * @param[out] rcvBufSize     Receiver buffer size in bytes
 * @retval     0              Success.
 * @retval     EINVAL         Error. `log_add()` called.
 */
static int
decodeCommandLine(
        int                    argc,
        char**  const restrict argv,
        size_t* const          npages,
        char**  const restrict prodQueuePath,
        char**  const restrict mcastSpec,
        char**  const restrict interface,
        int*    const restrict rcvBufSize)
{
    int                 status = 0;
    extern int          optind;
    extern int          opterr;
    int                 ch;

    opterr = 0;                         /* no error messages from getopt(3) */

    while (0 == status &&
            (ch = getopt(argc, argv, "b:cfI:l:m:nq:r:R:s:t:u:vx")) != -1) {
        switch (ch) {
            extern char*    optarg;
            extern int      optopt;

            case 'b': {
                unsigned long   n;
                int             nbytes;

                if (sscanf(optarg, "%12lu %n", &n, &nbytes) != 1 ||
                        optarg[nbytes] != 0) {
                    log_add_syserr("Couldn't decode FIFO size in pages: \"%s\"",
                            optarg);
                    log_flush_error();
                    status = EINVAL;
                }
                else {
                    *npages = n;
                }
                break;
            }
            case 'c':
                inflateFrame = TRUE;
                break;  
            case 'f':
                fillScanlines = TRUE;
                break;  
            case 'I':
                *interface = optarg;
                break;
            case 'l':
                if (log_set_destination(optarg)) {
                    log_syserr("Couldn't set logging destination to \"%s\"",
                            optarg);
                    status = EINVAL;
                }
                break;
            case 'm':
                *mcastSpec = optarg;
                break;
            case 'n':
                if (!log_is_enabled_notice)
                    (void)log_set_level(LOG_LEVEL_NOTICE);
                break;
            case 'q':
                *prodQueuePath = optarg;
                break;
            case 'r':
#ifdef RETRANS_SUPPORT
                retrans_xmit_enable = atoi(optarg);
                if(retrans_xmit_enable == 1)
                  retrans_xmit_enable = OPTION_ENABLE;
                else
                  retrans_xmit_enable = OPTION_DISABLE;
#endif
                break;
            case 'R':
                   if (sscanf(optarg, "%d", rcvBufSize) != 1 ||
                           *rcvBufSize <= 0) {
                       log_add("Invalid receive buffer size: \"%s\"", optarg);
                       status = EINVAL;
                   }
                   break;
            case 's': {
#ifdef RETRANS_SUPPORT
                strncpy(sbn_channel_name, optarg, 12);
                if(!strcmp(optarg,NAME_SBN_TYP_GOES)) {
                    sbn_type = SBN_TYP_GOES;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_NOAAPORT_OPT)) {
                    sbn_type = SBN_TYP_NOAAPORT_OPT;
                    break;
                }
                if(!strcmp(optarg,"NWSTG")) {
                    sbn_type = SBN_TYP_NMC;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_NMC)) {
                    sbn_type = SBN_TYP_NMC;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_NMC2)) {
                    sbn_type = SBN_TYP_NMC2;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_NMC3)) {
                    sbn_type = SBN_TYP_NMC3;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_NWWS)) {
                    sbn_type = SBN_TYP_NWWS;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_ADD)) {
                    sbn_type = SBN_TYP_ADD;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_ENC)) {
                    sbn_type = SBN_TYP_ENC;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_EXP)) {
                    sbn_type = SBN_TYP_EXP;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_GRW)) {
                    sbn_type = SBN_TYP_GRW;
                    break;
                }
                if(!strcmp(optarg,NAME_SBN_TYP_GRE)) {
                    sbn_type = SBN_TYP_GRE;
                    break;
                }
                printf("Operator input: UNKNOWN type must be\n");
                printf(" %s, %s, %s, %s, %s, %s, %s, %s, %s, %s  or %s \n",
                        NAME_SBN_TYP_NMC,
                        NAME_SBN_TYP_GOES,
                        NAME_SBN_TYP_NOAAPORT_OPT,
                        NAME_SBN_TYP_NMC2,
                        NAME_SBN_TYP_NMC3,
                        NAME_SBN_TYP_NWWS,
                        NAME_SBN_TYP_ADD,
                        NAME_SBN_TYP_ENC,
                        NAME_SBN_TYP_EXP,
                        NAME_SBN_TYP_GRW,
                        NAME_SBN_TYP_GRE);
#endif
                break;
            }
            case 't':
#ifdef RETRANS_SUPPORT
                strncpy(transfer_type, optarg, 9);
                if(!strcmp(transfer_type,"MHS") || !strcmp(transfer_type,"mhs")){
                     /** Using MHS for communication with NCF  **/
                }else{
                     log_add("No other mechanism other than MHS is currently supported\n");
                     status  = EINVAL;
                }
#endif
                break;
            case 'u': {
                int         i = atoi(optarg);

                if (0 > i || 7 < i) {
                    log_add("Invalid system logging facility number: %d", i);
                    status = EINVAL;
                }
                else {
                    static int  logFacilities[] = {LOG_LOCAL0, LOG_LOCAL1,
                        LOG_LOCAL2, LOG_LOCAL3, LOG_LOCAL4, LOG_LOCAL5,
                        LOG_LOCAL6, LOG_LOCAL7};
                    // NB: Specifying syslog facility implies logging to syslog
                    if (log_set_facility(logFacilities[i]) ||
                            log_set_destination(""))
                        status = EINVAL;
                }

                break;
            }
            case 'v':
                if (!log_is_enabled_info)
                    (void)log_set_level(LOG_LEVEL_INFO);
                break;
            case 'x':
                (void)log_set_level(LOG_LEVEL_DEBUG);
                break;
            default:
                optopt = ch;
                /*FALLTHROUGH*/
                /* no break */
            case '?': {
                log_add("Unknown option: \"%c\"", optopt);
                status = EINVAL;
                break;
            }
        }                               /* option character switch */
    }                                   /* getopt() loop */

    if (0 == status) {
        if (optind < argc) {
            log_add("Extraneous command-line argument: \"%s\"", argv[optind]);
            status = EINVAL;
        }
    }

    return status;
}

/**
 * Unconditionally logs a usage message.
 *
 * @param[in] progName   Name of the program.
 * @param[in] npages     Default size of the input buffer in memory-pages.
 * @param[in] copyright  Copyright notice.
 */
static void usage(
    const char* const          progName,
    const size_t               npages,
    const char* const restrict copyright)
{
    int level = log_get_level();
    (void)log_set_level(LOG_LEVEL_NOTICE);

    log_notice_q(
"%s version %s\n"
"%s\n"
"\n"
"Usage: %s [-cf] [-n|v|x] [-l log] [-u n] [-m addr] [-q queue] [-b npages] [-I ip_addr]\n"
"          [-R bufSize]"
#ifdef RETRANS_SUPPORT
" [-r <1|0>] [-t] [-s channel-name]"
#endif
"\n"
"where:\n"
"   -b npages   Allocate \"npages\" pages of memory for the internal buffer.\n"
"               Default is %lu pages. \"getconf PAGESIZE\" reveals page-size.\n"
"   -c          Enable Frame Decompression [Default => Disabled ]. \n"
"   -f          Fill blank scanlines for missing Satellite Imagery  [Default => Disabled ]. \n"
"   -I ip_addr  Listen for multicast packets on interface \"ip_addr\".\n"
"               Default is system's default multicast interface.\n"
"   -l dest     Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"               (standard error), or file `dest`. Default is \"%s\"\n"
"   -m addr     Read data from IPv4 dotted-quad multicast address \"addr\".\n"
"               Default is to read from the standard input stream.\n"
"   -n          Log through level NOTE. Report each data-product.\n"
"   -q queue    Use \"queue\" as LDM product-queue. Default is \"%s\".\n"
"   -R bufSize  Receiver buffer size in bytes. Default is system dependent.\n"
"   -u n        Use logging facility local\"n\". Default is to use the\n"
"               default LDM logging facility, %s. Implies \"-l ''\".\n"
"   -v          Log through level INFO.\n"
"   -x          Log through level DEBUG. Too much information.\n"
#ifdef RETRANS_SUPPORT
"   -r <1|0>    Enable(1)/Disable(0) Retransmission [ Default: 0 => Disabled ] \n"
"   -t          Transfer mechanism [Default = MHS]. \n"
"   -s          Channel Name [Default = NMC]. \n"
#endif
"\n"
"If neither \"-n\", \"-v\", nor \"-x\" is specified, then only levels ERROR\n"
"and WARN are logged.\n"
"\n"
"SIGUSR1 unconditionally logs statistics at level NOTE and refreshes logging.\n"
"SIGUSR2 rotates the logging level.\n",
        progName, PACKAGE_VERSION, copyright, progName, (unsigned long)npages,
        log_get_default_destination(), lpqGetQueuePath(),
        getFacilityName(log_get_facility()));

    (void)log_set_level(level);
}

/**
 * Tries to lock the current process in physical memory.
 */
static inline void
tryLockingProcessInMemory(void)
{
    if (lockProcessInMemory()) {
        log_warning_q("Couldn't lock process in physical memory");
    }
}

/**
 * Handles a signal.
 *
 * @param[in] sig  The signal to be handled.
 */
static void signal_handler(
        const int       sig)
{
    switch (sig) {
        case SIGTERM:
            done = 1;
            /*
             * The ProductMaker will continue to call `fifo_getBytes()` on the
             * product-maker thread until the FIFO has no more data.
             */
            fifo_close(fifo); // Async-signal-safe
            break;
        case SIGUSR1:
            if (reporterThread != pthread_self())
                (void)pthread_kill(reporterThread, SIGUSR1);
            break;
        case SIGUSR2:
            (void)log_roll_level();
            break;
    }

    return;
}

/**
 * Enables or disables the handling of SIGUSR1.
 *
 * @param[in] enable  Whether or not to enable SIGUSR1 handling.
 */
static void
enableSigUsr1(
        const bool enable)
{
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = enable ? signal_handler : SIG_IGN;

    /* Restart system calls */
    sigact.sa_flags |= SA_RESTART;

    (void)sigaction(SIGUSR1, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

/**
 * Registers the signal handler for most signals.
 */
static void set_sigactions(void)
{
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Ignore the following */
    sigact.sa_handler = SIG_IGN;
    (void)sigaction(SIGALRM, &sigact, NULL);
    (void)sigaction(SIGCHLD, &sigact, NULL);
    (void)sigaction(SIGCONT, &sigact, NULL);

    /* Handle the following */
    sigact.sa_handler = signal_handler;

    /*
     * Don't restart the following.
     *
     * SIGTERM must be handled in order to cleanly close the product-queue
     * (i.e., return the writer-counter of the product-queue to zero).
     */
    (void)sigaction(SIGTERM, &sigact, NULL);

    /* Restart the following */
    sigact.sa_flags |= SA_RESTART;
    (void)sigaction(SIGUSR1, &sigact, NULL);
    (void)sigaction(SIGUSR2, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGALRM);
    (void)sigaddset(&sigset, SIGCHLD);
    (void)sigaddset(&sigset, SIGCONT);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

/**
 * Blocks termination signals (SIGINT, SIGTERM) for the current thread. This
 * function is idempotent.
 */
static void
blockTermSignals(void)
{
    static sigset_t sigSet;

    (void)sigemptyset(&sigSet);
    (void)sigaddset(&sigSet, SIGINT);
    (void)sigaddset(&sigSet, SIGTERM);

    (void)pthread_sigmask(SIG_BLOCK, &sigSet, NULL);
}

/**
 * Unblocks termination signals (SIGINT, SIGTERM) for the current thread. This
 * function is idempotent.
 */
static void
unblockTermSignals(void)
{
    static sigset_t sigSet;

    (void)sigemptyset(&sigSet);
    (void)sigaddset(&sigSet, SIGINT);
    (void)sigaddset(&sigSet, SIGTERM);

    (void)pthread_sigmask(SIG_UNBLOCK, &sigSet, NULL);
}

/**
 * Returns a file descriptor to the input. Sets `isMcastInput`.
 *
 * @param[in] mcastSpec      Specification of multicast group or NULL to read
 *                           from the standard input stream.
 * @param[in] interface      IPv4 address of interface on which to listen for
 *                           multicast NOAAPort packets or NULL to listen on all
 *                           interfaces. Ignored iff `mcastSpec == NULL`.
 * @param[in] rcvBufSize     Receiver buffer size in bytes iff > 0
 * @param[out] isMcastInput  Is input from NOAAPort multicast?
 * @retval    -1             Failure. `log_add()` called.
 * @return                   Input file descriptor. `*isMcastInput` is set.
 * @threadsafety             Safe
 */
static int
getInputFd(
        const char* const restrict mcastSpec,
        const char* const restrict interface,
        bool* const restrict       isMcastInput,
        int                        rcvBufSize)
{
    int fd; // Input file descriptor
    bool mcastInput = mcastSpec != NULL;


    if (!mcastInput) {
        fd = fileno(stdin);

        if (fd == -1)
            log_add_syserr(
                    "Couldn't get file-descriptor of standard input stream");
    }
    else if (nportSock_init(&fd, mcastSpec, interface, rcvBufSize)) {
        log_add("Couldn't open NOAAPort socket");
        fd = -1;
    }

    if (fd != -1)
        *isMcastInput = mcastInput;

    return fd;
}

/**
 * Try to create a thread with non-default scheduling. If that fails then create
 * a default thread.
 *
 * This function exists solely to accommodate "security" software like RHEL's
 * Insights Client, which, by default, will prevent this program from changing
 * its scheduling -- even if the process is owned by root.
 *
 * @param[out] thread   Thread. Set only on success.
 * @param[in]  name     Name of the thread
 * @param[in]  attr     Thread attributes with non-default scheduling
 * @param[in]  func     Thread start-function
 * @param[in]  arg      Optional argument to start-function
 * @retval     `true`   Success. `*thread` is set. `log_warning()` called if the
 *                      thread couldn't be created with the desired scheduling.
 * @retval     `false`  Failure. `log_add()` called.
 */
static bool
createThread(
		pthread_t*            thread,
		const char*           name,
		const pthread_attr_t* attr,
		void*               (*func)(void*),
		void*                 arg)
{
	int status = pthread_create(thread, attr, func, arg);
	if (status) {
		if (status == EPERM) {
			status = pthread_create(thread, NULL, func, arg); // Default attributes
			if (status) {
				log_add_errno(status, "Couldn't create %s thread", name);
			}
			else {
				log_add_errno(EPERM, "Couldn't create %s thread with desired "
						"scheduling", name);
				log_flush_warning();
				status = 0; // Ignore this failure
			}
		}
		else {
			log_add_errno(status, "Couldn't create %s thread", name);
		}
	}

	return status == 0;
}

/**
 * Creates a product-maker and starts it on a new thread.
 *
 * @param[in]  attr            Pointer to thread-creation attributes
 * @param[in]  fifo            Pointer to FIFO from which to read data.
 * @param[in]  productQueue    Pointer to product-queue into which to put
 *                             created data-products.
 * @param[out] productMaker    Pointer to pointer to created product-maker.
 * @param[out] thread          Pointer to created thread.
 * @retval     0               Success.
 * @retval     USAGE_ERROR     Usage error. `log_add()` called.
 * @retval     SYSTEM_FAILURE  System failure. `log_add()` called.
 */
static int spawnProductMaker(
    const pthread_attr_t* const restrict attr,
    Fifo* const restrict                 fifo,
    LdmProductQueue* const restrict      productQueue,
    ProductMaker** const restrict        productMaker,
    pthread_t* const restrict            thread)
{
    ProductMaker*   pm;
    int             status = pmNew(fifo, productQueue, &pm);

    if (status) {
        log_add("Couldn't create new LDM product-maker");
    }
    else {
        if (!createThread(thread, "product-maker", attr, pmStart, pm)) {
			pmFree(pm);
			status = SYSTEM_FAILURE;
		}
		else {
            *productMaker = pm;
		}
    } // `pm` allocated

    return status;
}

/**
 * Returns the time interval between two times.
 *
 * @param[in] later    The later time
 * @param[in] earlier  The earlier time.
 * @return             The time interval, in seconds, between the two times.
 */
static double duration(
    const struct timeval*   later,
    const struct timeval*   earlier)
{
    return (later->tv_sec - earlier->tv_sec) +
        1e-6*(later->tv_usec - earlier->tv_usec);
}

/**
 * Returns the string representation of a time interval.
 *
 * @param[out] buf       Buffer into which to encode the interval.
 * @param[in]  size      Size of the buffer in bytes.
 * @param[in]  duration  The time interval in seconds.
 * @return               The string representation of the given time interval.
 */
static void encodeDuration(
    char*       buf,
    size_t      size,
    double      duration)
{
    unsigned    value;
    int         nchar;
    int         tPrinted = 0;

    buf[size-1] = 0;

    (void)strncpy(buf, "P", size);
    buf++;
    size--;

    value = duration / 86400;

    if (value > 0) {
        nchar = snprintf(buf, size, "%uD", value);
        buf += nchar;
        size -= nchar;
        duration -= 86400 * value;
        duration = duration < 0 ? 0 : duration;
    }

    value = duration / 3600;

    if (value > 0) {
        nchar = snprintf(buf, size, "T%uH", value);
        tPrinted = 1;
        buf += nchar;
        size -= nchar;
        duration -= 3600 * value;
        duration = duration < 0 ? 0 : duration;
    }

    value = duration / 60;

    if (value > 0) {
        if (!tPrinted) {
            (void)strncpy(buf, "T", size);
            buf++;
            size--;
            tPrinted = 1;
        }

        nchar = snprintf(buf, size, "%uM", value);
        buf += nchar;
        size -= nchar;
        duration -= 60 * value;
        duration = duration < 0 ? 0 : duration;
    }

    if (duration > 0) {
        if (!tPrinted) {
            (void)strncpy(buf, "T", size);
            buf++;
            size--;
        }

        (void)snprintf(buf, size, "%fS", duration);
    }
}

/**
 * Reports statistics.
 *
 * @param[in] productMaker  Maker of LDM data-products.
 * @param[in] startTime     Time when this process started.
 * @param[in] reportTime    Time of last report.
 * @param[in] reader        Reader of data-input.
 */
static void reportStats(
        ProductMaker* const restrict   productMaker,
        struct timeval* const restrict startTime,
        struct timeval* const restrict reportTime,
        Reader* const restrict         reader)
{
    static unsigned long    totalFrameCount;
    static unsigned long    totalMissedFrameCount;
    static unsigned long    totalProdCount;
    static unsigned long    totalByteCount;
    static unsigned long    totalFullFifoCount;

    struct timeval          now;
    unsigned long           byteCount;
    unsigned long           frameCount, missedFrameCount, prodCount;
    unsigned long           fullFifoCount;
    (void)gettimeofday(&now, NULL);
    readerGetStatistics(reader, &byteCount, &fullFifoCount);
    pmGetStatistics(productMaker, &frameCount, &missedFrameCount, &prodCount);

    totalByteCount += byteCount;
    totalFrameCount += frameCount;
    totalMissedFrameCount += missedFrameCount;
    totalProdCount += prodCount;
    totalFullFifoCount += fullFifoCount;

    int     logLevel = log_get_level();
    (void)log_set_level(LOG_LEVEL_NOTICE);

    double  reportDuration = duration(&now, reportTime);
    double  startDuration = duration(&now, startTime);
    char    reportDurationBuf[80];
    char    startDurationBuf[80];
    encodeDuration(reportDurationBuf, sizeof(reportDurationBuf), reportDuration);
    encodeDuration(startDurationBuf, sizeof(startDurationBuf), startDuration);
    double  reportRate = (byteCount/reportDuration);
    double  startRate = (totalByteCount/startDuration);
    char    msg[4096];
    char*   buf = msg;
    size_t  size = sizeof(msg);
    int     nbytes = snprintf(buf, size,
"\n"
"----------------------------------------\n"
"Ingestion Statistics:\n"
"    Since Previous Report (or Start):\n"
"        Duration          %s\n"
"        Raw Data:\n"
"            Octets        %lu\n"
"            Mean Rate:\n"
"                Octets    %g/s\n"
"                Bits      %g/s\n"
"        Received frames:\n"
"            Number        %lu\n"
"            Mean Rate     %g/s\n"
"        Missed frames:\n"
"            Number        %lu\n"
"            %%             %g\n"
"        Full FIFO:\n"
"            Number        %lu\n"
"            %%             %g\n"
"        Products:\n"
"            Inserted      %lu\n"
"            Mean Rate     %g/s\n"
"    Since Start:\n"
"        Duration          %s\n"
"        Raw Data:\n"
"            Octets        %lu\n"
"            Mean Rate:\n"
"                Octets    %g/s\n"
"                Bits      %g/s\n"
"        Received frames:\n"
"            Number        %lu\n"
"            Mean Rate     %g/s\n"
"        Missed frames:\n"
"            Number        %lu\n"
"            %%             %g\n"
"        Full FIFO:\n"
"            Number        %lu\n"
"            %%             %g\n"
"        Products:\n"
"            Inserted      %lu\n"
"            Mean Rate     %g/s\n",
            reportDurationBuf,
            byteCount,
            reportRate,
            8*reportRate,
            frameCount,
            frameCount/reportDuration,
            missedFrameCount,
            100.0 * missedFrameCount / (missedFrameCount + frameCount),
            fullFifoCount,
            100.0 * fullFifoCount / frameCount,
            prodCount,
            prodCount/reportDuration,
            startDurationBuf,
            totalByteCount,
            startRate,
            8*startRate,
            totalFrameCount,
            totalFrameCount/startDuration,
            totalMissedFrameCount,
            100.0 * totalMissedFrameCount /
                (totalMissedFrameCount + totalFrameCount),
            totalFullFifoCount,
            100.0 * totalFullFifoCount / totalFrameCount,
            totalProdCount,
            totalProdCount/startDuration);
    if (nbytes >= size)
        nbytes = size;
    buf += nbytes;
    size -= nbytes;
#ifdef RETRANS_SUPPORT
    if (retrans_xmit_enable == OPTION_ENABLE) {
        nbytes = snprintf(buf, size,
"        Retransmissions:\n"
"            Requested     %lu\n"
"            Received      %lu\n"
"            Duplicates    %lu\n"
"            No duplicates %lu\n",
                total_prods_retrans_rqstd,
                total_prods_retrans_rcvd,
                total_prods_retrans_rcvd_notlost,
                total_prods_retrans_rcvd_lost);
        if (nbytes >= size)
            nbytes = size;
        buf += nbytes;
        size -= nbytes;
    }
#endif 
    (void)snprintf(buf, size,
"----------------------------------------");
    msg[sizeof(msg)-1] = 0;
    log_notice_q("%s", msg); // Danger! `msg` contains '%' characters

    (void)log_set_level(logLevel);

    *reportTime = now;
}

/**
 * Reports statistics when the current thread receives a `SIGUSR1`. May be
 * called by `pthread_create()`.
 *
 * @param[in] arg   Pointer to the relevant `StatsStruct`. The caller must not
 *                  modify or free.
 * @retval    NULL  Always.
 */
static void* startReporter(void* arg)
{
    StatsStruct ss = *(StatsStruct*)arg;

    sigset_t    usr1Mask;
    (void)sigemptyset(&usr1Mask);
    (void)sigaddset(&usr1Mask, SIGUSR1);

    sigset_t prevMask;
    (void)pthread_sigmask(SIG_BLOCK, &usr1Mask, &prevMask);

    while (!done) {
        int sig;

        (void)sigwait(&usr1Mask, &sig);
        reportStats(ss.productMaker, &ss.startTime, &ss.reportTime, ss.reader);
        log_refresh();
    }

    (void)pthread_sigmask(SIG_SETMASK, &prevMask, NULL);

    return NULL;
}

/**
 * Initializes a statistics-reporting structure. The `startTime` and
 * `reportTime` fields are set to the current time.
 *
 * @param[in] ss            The structure.
 * @param[in] productMaker  The maker of LDM data-products.
 * @param[in] reader        The reader of input.
 */
static void
ss_init(
        StatsStruct* const restrict  ss,
        ProductMaker* const restrict productMaker,
        Reader* const restrict       reader)
{
    ss->productMaker = productMaker;
    ss->reader = reader;
    (void)gettimeofday(&ss->startTime, NULL);
    ss->reportTime = ss->startTime;
}

/**
 * Initializes thread attributes. The scheduling inheritance mode will be
 * explicit and the scheduling contention scope will be system-wide.
 *
 * @param     isMcastInput    Input is from NOAAPort multicast?
 * @param[in] attr            Pointer to uninitialized thread attributes object.
 *                            Caller should call `pthread_attr_destroy(*attr)`
 *                            when it's no longer needed.
 * @param[in] policy          Scheduling policy. Ignored if input isn't from
 *                            multicast.
 * @param[in] priority        Thread priority. Ignored if input isn't from
 *                            multicast. Must be consonant with `policy`.
 * @retval    0               Success. `*attr` is initialized.
 * @retval    USAGE_ERROR     Usage error. `log_add()` called.
 * @retval    SYSTEM_FAILURE  System error. `log_add()` called.
 */
static int
initThreadAttr(
        const bool            isMcastInput,
        pthread_attr_t* const attr,
        const int             policy,
        const int             priority)
{
    int status = pthread_attr_init(attr);

    if (status) {
        log_errno(status, "Couldn't initialize thread attributes object");
        status = (EBUSY == status) ? USAGE_ERROR : SYSTEM_FAILURE;
    }
    else if (isMcastInput) {
        #ifndef _POSIX_THREAD_PRIORITY_SCHEDULING
            log_warning_q("Can't adjust thread scheduling due to lack of support from "
                    "the environment");
        #else
            struct sched_param  param = {}; // 2020-09-03

            param.sched_priority = priority;

            (void)pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
            (void)pthread_attr_setschedpolicy(attr, policy);
            (void)pthread_attr_setschedparam(attr, &param);
            /*
             * The scheduling contention scope is system-wide because data
             * arrival can't be controlled.
             */
            (void)pthread_attr_setscope(attr, PTHREAD_SCOPE_SYSTEM);
        #endif
    }

    return status;
}

/**
 * Initialize support for retransmission requests. Does nothing if
 * retransmission support isn't enabled at compile-time or if the input isn't
 * from multicast UDP packets.
 *
 * @param[in] isMcastInput  Is the input from multicast UDP packets?
 * @param[in] mcastSpec     Specification of multicast group. Ignored if
 *                          `!isMcastInput`.
 */
static void
initRetransSupport(
        const bool        isMcastInput,
        const char* const mcastSpec)
{
    #ifdef RETRANS_SUPPORT
        if (isMcastInput && retrans_xmit_enable == OPTION_ENABLE) {
            // Copy mcastAddress needed to obtain the cpio entries
            (void)strncpy(mcastAddr, mcastSpec, 15);
        }
    #endif
}

/**
 * Destroys support for retransmission requests. Does nothing if retransmission
 * support isn't enabled at compile-time or if the input isn't from multicast
 * UDP packets.
 *
 * @param[in] isMcastInput  Is the input from multicast UDP packets?
 */
static void
destroyRetransSupport(
        const bool isMcastInput)
{
    #ifdef RETRANS_SUPPORT
        if (isMcastInput && retrans_xmit_enable == OPTION_ENABLE) {
            // Release buffer allocated for retransmission
            freeRetransMem();
        }
    #endif
}

/**
 * Creates and starts an input-reader on a separate thread.
 *
 * @param[in]  isMcastInput    Input is from NOAAPort multicast?
 * @param[in]  policy          Scheduling policy for the reader thread. Ignored
 *                             if the input isn't from multicast.
 * @param[in]  priority        Scheduling priority for the reader thread.
 *                             Ignored if the input isn't from multicast. Must
 *                             be consonant with `policy`.
 * @param[out] reader          Reader of input. Caller should call
 *                             `readerFree(*reader)` when it's no longer needed.
 * @param[out] thread          Thread on which the input-reader is executing.
 * @retval     0               Success. `*reader` and `*thread` are set.
 * @retval     USAGE_ERROR     Usage error. `log_add()` called.
 * @retval     SYSTEM_FAILURE  System failure. `log_add()` called.
 */
static int
startReader(
        const bool                 isMcastInput,
        const int                  policy,
        const int                  priority,
        Reader** const restrict    reader,
        pthread_t* const restrict  thread)
{
    log_assert(reader);
    log_assert(thread);

    pthread_attr_t attr;
    int            status = initThreadAttr(isMcastInput, &attr, policy,
            priority);

    if (0 == status) {
        unblockTermSignals();
            /*
             * The maximum IPv4 UDP payload is 65507 bytes. The maximum observed
             * UDP payload, however, should be 5232 bytes, which is the maximum
             * amount of data in a NESDIS frame (5152 bytes) plus the overhead
             * of the 3 SBN protocol headers: frame level header (16 bytes) +
             * product definition header (16 bytes) + AWIPS product specific
             * header (48 bytes). The maximum size of an ethernet jumbo frame is
             * around 9000 bytes. Consequently, the maximum amount to read in a
             * single call is conservatively set to 10000 bytes. 2014-12-30.
             *
             * Reverted to 65507 bytes because the number of frames missed by
             * Chico increased greatly relative to Lenny after the maximum read
             * size was changed from 65507 to 10000 bytes. Could it be that
             * NOAAPORT is using large UDP packets and depending on IP
             * fragmentation? That seems inconsistent, however, with
             * dvbs_multicast(1) use of 10000 bytes in its call to recvfrom(2).
             * 2015-01-3.
             *
             * 2020-09-05: NOAAPort frames are 4000 bytes => IP reassembly.
             */
            Reader* rdr = readerNew(fifo,
                    isMcastInput ? 65507 : sysconf(_SC_PAGESIZE));

            if (rdr == NULL) {
                log_add("Couldn't create input-reader");
                status = SYSTEM_FAILURE;
            }
            else {
                if (!createThread(thread, "input-reader", &attr, readerStart,
                		rdr)) {
                    readerFree(rdr);
                	status = SYSTEM_FAILURE;
				}
				else {
                    *reader = rdr;
				}
            } // `rdr` is set
        blockTermSignals();

        (void)pthread_attr_destroy(&attr);
    } // `attr` initialized

    return status;
}

/**
 * Waits for an input-reader to terminate.
 *
 * @param[in]  thread          Thread on which the reader is executing.
 * @retval     0               Success.
 * @retval     USAGE_ERROR     Usage error.
 * @retval     SYSTEM_FAILURE  System failure. `log_add()` called.
 */
static inline int
waitOnReader(
        pthread_t const thread)
{
    void* voidPtr;
    int   status = pthread_join(thread, &voidPtr);

    if (status) {
        log_errno(status, "Couldn't join input-reader thread");
        status = SYSTEM_FAILURE;
    }
    else {
        status = done ? 0 : *(int*)voidPtr;

        if (status)
            log_add("Input-reader thread returned %d", status);
    }

    return status;
}

/**
 * Runs the inner core of this program. The FIFO is closed on return and the
 * product-maker thread is joined. Final statistics are reported on success.
 *
 * @param      isMcastInput    Input is from NOAAPort multicast?
 * @param[in]  productMaker    The maker of LDM data-products.
 * @param[in]  pmThread        The thread on which the product-maker is
 *                             executing.
 * @param[in]  policy          Scheduling policy for the reader thread. Ignored
 *                             if the input isn't from multicast.
 * @param[in]  priority        Scheduling priority for the reader thread.
 *                             Ignored if the input isn't from multicast. Must
 *                             be consonant with `policy`.
 * @retval     0               Success.
 * @retval     USAGE_ERROR     Usage error. `log_add()` called.
 * @retval     SYSTEM_FAILURE  System failure. `log_add()` called.
 */
static int
execute3(
        const bool                   isMcastInput,
        ProductMaker* const restrict productMaker,
        pthread_t const              pmThread,
        const int                    policy,
        const int                    priority)
{
    Reader*     reader;
    pthread_t   readerThread = {0};
    int         status = startReader(isMcastInput, policy, priority,  &reader,
            &readerThread);
    bool        reporterRunning = false;

    if (status) {
        log_add("Couldn't start input-reader");
    }
    else {
        StatsStruct ss;

        ss_init(&ss, productMaker, reader);
        (void)pthread_create(&reporterThread, NULL, startReporter, &ss);
        reporterRunning = true;

        enableSigUsr1(true);  // enable stats reporting; requires reader object
        status = waitOnReader(readerThread);
    } // input-reader started

    (void)pthread_join(pmThread, NULL);

    /*
     * Final statistics are reported only after the product-maker has
     * terminated to prevent a race condition in logging and consequent
     * variability in the output -- which can affect testing.
     */
    if (reporterRunning) {
        // Reports statistics; requires reader object
        done = 1;  // Causes reporting thread to terminate
        (void)pthread_kill(reporterThread, SIGUSR1);
        (void)pthread_join(reporterThread, NULL);

        readerFree(reader); // reporterRunning => reader != NULL
    }

    return status;
}

/**
 * Runs the outer core of this program.
 *
 * @param[in] isMcastInput    Input is from NOAAPort multicast?
 * @param[in] mcastSpec       Specification of multicast group or NULL if
 *                            input is from the standard input stream.
 * @param[in] prodQueue       Pointer to LDM product-queue object.
 * @retval    0               Success.
 * @retval    USAGE_ERROR     Usage error. `log_add()` called.
 * @retval    SYSTEM_FAILURE  System failure. `log_add()` called.
 */
static int
execute2(
        const bool                 isMcastInput,
        const char* const restrict mcastSpec,
        LdmProductQueue* const     prodQueue)
{
    pthread_attr_t  attr;
    /*
     * If the input is multicast UDP packets, then the product-maker thread
     * runs at a lower priority than the input thread to reduce the chance
     * of the input thread missing a packet.
     */
    int             maxPriority = sched_get_priority_max(SCHED_POLICY);
    int             status = initThreadAttr(isMcastInput, &attr, SCHED_POLICY,
            maxPriority-1);

    if (0 == status) {
        initRetransSupport(isMcastInput, mcastSpec);

        /*
         * Termination signals are blocked for all threads except the
         * input-reader thread, which might have the highest priority,
         */
        blockTermSignals();
        ProductMaker* productMaker = NULL;
        pthread_t     pmThread;
        status = spawnProductMaker(&attr, fifo, prodQueue, &productMaker,
                &pmThread);

        if (0 == status)
            status = execute3(isMcastInput, productMaker, pmThread,
                    SCHED_POLICY, maxPriority);

        pmFree(productMaker);
        destroyRetransSupport(isMcastInput);
        (void)pthread_attr_destroy(&attr);
    }   // `attr` initialized

    return status;
}

/**
 * Executes this program.
 *
 * @param[in] mcastSpec       Specification of multicast group or NULL to read
 *                            from the standard input stream.
 * @param[in] interface       IPv4 address of interface on which to listen for
 *                            multicast NOAAPort packets or NULL to listen on
 *                            all interfaces. Ignored iff `mcastSpec == NULL`.
 * @param[in] npages          Size of the queue in pages.
 * @param[in] prodQueuePath   Pathname of product-queue.
 * @param[in] rcvBufSize      Receiver buffer size in bytes iff > 0
 * @retval    1               Couldn't open input. `log_add()` called.
 * @retval    2               Couldn't create FIFO. `log_add()` called.
 * @retval    3               Couldn't open product-queue. `log_add()` called.
 */
static int
execute(const char* const restrict mcastSpec,
        const char* const restrict interface,
        const size_t               npages,
        const char* const restrict prodQueuePath,
		int                        rcvBufSize)
{
    log_assert(npages > 0);

    int  status;
    bool isMcastInput;
    int  fd = getInputFd(mcastSpec, interface, &isMcastInput, rcvBufSize);

    if (fd == -1) {
        log_add("Couldn't open input");
        status = 1;
    }
    else {
        fifo = fifo_new(fd, npages);

        if (fifo == NULL) {
            log_add("Couldn't create FIFO");
            status = 2;
        }
        else {
            set_sigactions();   // Ensures product-queue is closed cleanly

            LdmProductQueue* prodQueue = NULL;

            status = lpqGet(prodQueuePath, &prodQueue);

            if (status) {
                log_add("Couldn't open product-queue");
                status = 3;
            }
            else {
                status = execute2(isMcastInput, mcastSpec, prodQueue);

                (void)lpqClose(prodQueue);
            } // `prodQueue` open */

            fifo_free(fifo);
            fifo = NULL;
        } // `fifo` created

        (void)close(fd);
    } // `fd` open

    return status;
}

/**
 * Reads a NOAAPORT data stream, creates LDM data-products from the stream, and
 * inserts the data-products into an LDM product-queue.  The NOAAPORT data
 * stream can take the form of multicast UDP packets from (for example) a
 * Novra S300 DVB-S2 receiver or the standard input stream.
 *
 * Usage:
 *     noaaportIngester [-l <em>log</em>] [-n|-v|-x] [-q <em>queue</em>] [-u <em>n</em>] [-m <em>mcastAddr</em>] [-I <em>ip_addr</em>] [-b <em>npages</em>]\n
 *
 * Where:
 * <dl>
 *      <dt>-b <em>npages</em></dt>
 *      <dd>Allocate \e npages pages of memory for the internal buffer.</dd>
 *
 *      <dt>-I <em>ip_addr</em></dt>
 *      <dd>Listen for multicast packets on interface \e ip_addr. Default is
 *      the system's default multicast interface.</dd>
 *
 *      <dt>-l <em>log</em></dt>
 *      <dd>Log to file \e log. The default is to use the system logging daemon
 *      if the current process is a daemon; otherwise, the standard error
 *      stream is used.</dd>
 *
 *      <dt>-m <em>mcastAddr</em></dt>
 *      <dd>Use the multicast address \e mcastAddr. The default is to
 *      read from the standard input stream.</dd>
 *
 *      <dt>-n</dt>
 *      <dd>Log messages of level NOTE and higher priority. Each data-product
 *      will generate a log message.</dd>
 *
 *      <dt>-q <em>queue</em></dt>
 *      <dd>Use \e queue as the pathname of the LDM product-queue. The default
 *      is to use the default LDM pathname of the product-queue.</dd>
 *
 *      <dt>-R <em>bufSize</em></dt>
 *      <dd>Set the receive buffer size for the NOAAPort UDP packets to
 *      <em>bufSize</em> bytes. The default is system dependent.</dd>
 *
 *      <dt>-u <em>n</em></dt>
 *      <dd>If logging is to the system logging daemon, then use facility 
 *      <b>local</b><em>n</em>. The default is to use the LDM facility.</dd>
 *
 *      <dt>-v</dt>
 *      <dd>Log messages of level INFO and higher priority.</dd>
 *
 *      <dt>-x</dt>
 *      <dd>Log messages of level DEBUG and higher priority.</dd>
 * </dl>
 *
 * If neither -n, -v, nor -x is specified, then logging will be restricted to
 * levels ERROR and WARN only.
 *
 * @retval 0 if successful.
 * @retval 1 if an error occurred. At least one error-message will be logged.
 */
int main(
    const int argc,           /**< [in] Number of arguments */
    char*     argv[])         /**< [in] Arguments */
{
    int status;

    /*
     * Initialize logging. Done first in case something happens that needs to
     * be reported.
     */
    const char* const progname = basename(argv[0]);
    if (log_init(progname)) {
        log_syserr("Couldn't initialize logging module");
        status = -1;
    }
    else {
        (void)log_set_level(LOG_LEVEL_WARNING);

        /*
         * Open the standard error stream on `/dev/null` if it's closed because
         * some of the NOAAPort library functions mistakenly write to it.
         */
        if (open_on_dev_null_if_closed(STDERR_FILENO, O_WRONLY)) {
            log_add("Couldn't open standard error stream on \"/dev/null\"");
            log_flush_error();
            status = -1;
        }
        else {
            enableSigUsr1(false);  // ignore SIGUSR1 initially

            size_t            npages = 5000;
            char*             prodQueuePath = NULL;
            // Read from standard input stream
            char*             mcastSpec = NULL;
            char*             interface = NULL; // Listen on all interfaces
            int               rcvBufSize= 0;
            const char* const COPYRIGHT_NOTICE = "Copyright (C) 2021 "
                    "University Corporation for Atmospheric Research";
            status = decodeCommandLine(argc, argv, &npages, &prodQueuePath,
                    &mcastSpec, &interface, &rcvBufSize);

            if (status) {
                log_add("Couldn't decode command-line");
                log_flush_fatal();
                usage(progname, npages, COPYRIGHT_NOTICE);
            }
            else {
                log_notice("Starting up %s", PACKAGE_VERSION);
                log_notice("%s", COPYRIGHT_NOTICE);

                tryLockingProcessInMemory(); // because NOAAPORT is realtime

                status = execute(mcastSpec, interface, npages, prodQueuePath,
                		         rcvBufSize);

                if (status) {
                    log_add("Couldn't ingest NOAAPort data");
                    log_flush_error();
                }
            }                               /* command line decoded */
        } // Closed `stderr` opened on `/dev/null`

        log_fini();
    }

    return status ? 1 : 0;
}
