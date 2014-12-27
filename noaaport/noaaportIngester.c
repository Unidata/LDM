/**
 *   Copyright © 2014, University Corporation for Atmospheric Research.
 *   See COPYRIGHT file for copying and redistribution conditions.
 *
 *   @file noaaportIngester.c
 *
 *   This file contains the code for the \c noaaportIngester(1) program. This
 *   program reads NOAAPORT data from a file or multicast packet stream,
 *   creates LDM data-products, and writes the data-products into an LDM
 *   product-queue.
 */
#include <config.h>

#include "ldm.h"
#include "log.h"
#include "fifo.h"
#include "fileReader.h"
#include "getFacilityName.h"
#include "globals.h"
#include "ldmProductQueue.h"
#include "multicastReader.h"
#include "productMaker.h"
#include "reader.h"

#include <errno.h>
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

static const int         USAGE_ERROR = 1;
static const int         SYSTEM_FAILURE = 2;
static const int         SCHED_POLICY = SCHED_FIFO;
static Fifo*             fifo;
static bool              reportStatistics;
static pthread_mutex_t   mutex;
static pthread_cond_t    cond = PTHREAD_COND_INITIALIZER;

/**
 * Decodes the command-line.
 *
 * @param[in]  argc           Number of arguments.
 * @param[in]  argv           Arguments.
 * @param[out] npages         Size of input buffer in memory-pages.
 * @param[out] prodQueuePath  Pathname of product-queue.
 * @param[out] mcastSpec      Specification of multicast group.
 * @param[out] interface      Specification of interface on which to listen.
 * @retval     0              Success.
 * @retval     1              Error. `log_start()` called.
 */
static int
decodeCommandLine(
        int                   argc,
        char** const restrict argv,
        size_t* const         npages,
        char** const restrict prodQueuePath,
        char** const restrict mcastSpec,
        char** const restrict interface)
{
    int                 status = 0;
    extern int          optind;
    extern int          opterr;
    int                 ch;

    opterr = 0;                         /* no error messages from getopt(3) */

    while (0 == status &&
            (ch = getopt(argc, argv, "b:I:l:m:nq:r:s:t:u:vx")) != -1) {
        switch (ch) {
            extern char*    optarg;
            extern int      optopt;

            case 'b': {
                unsigned long   n;

                if (sscanf(optarg, "%12lu", &n) != 1) {
                    LOG_SERROR1("Couldn't decode FIFO size in pages: \"%s\"",
                            optarg);
                    status = 1;
                }
                else {
                    *npages = n;
                }
                break;
            }
            case 'I':
                *interface = optarg;
                break;
            case 'l':
                if (openulog(getulogident(), ulog_get_options(),
                        getulogfacility(), optarg) == -1)
                    status = 1;
                break;
            case 'm':
                *mcastSpec = optarg;
                break;
            case 'n':
                (void)setulogmask(getulogmask() | LOG_MASK(LOG_NOTICE));
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
            case 's': {
#ifdef RETRANS_SUPPORT
                strcpy(sbn_channel_name, optarg);
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
                strcpy(transfer_type, optarg);
                if(!strcmp(transfer_type,"MHS") || !strcmp(transfer_type,"mhs")){
                     /** Using MHS for communication with NCF  **/
                }else{
                     LOG_START0("No other mechanism other than MHS is currently supported\n");
                     status  = 1;
                }
#endif
                break;
            case 'u': {
                int         i = atoi(optarg);

                if (0 > i || 7 < i) {
                    LOG_START1("Invalid logging facility number: %d", i);
                    status = 1;
                }
                else {
                    static int  logFacilities[] = {LOG_LOCAL0, LOG_LOCAL1,
                        LOG_LOCAL2, LOG_LOCAL3, LOG_LOCAL4, LOG_LOCAL5,
                        LOG_LOCAL6, LOG_LOCAL7};

                    if (openulog(getulogident(), ulog_get_options(),
                            logFacilities[i], getulogpath()) == -1)
                        status = 1;
                }

                break;
            }
            case 'v':
                (void)setulogmask(getulogmask() | LOG_MASK(LOG_INFO));
                break;
            case 'x':
                (void)setulogmask(getulogmask() | LOG_MASK(LOG_DEBUG));
                break;
            default:
                optopt = ch;
                /*FALLTHROUGH*/
                /* no break */
            case '?': {
                LOG_START1("Unknown option: \"%c\"", optopt);
                status = 1;
                break;
            }
        }                               /* option character switch */
    }                                   /* getopt() loop */

    if (0 == status) {
        if (optind < argc) {
            LOG_START1("Extraneous command-line argument: \"%s\"", argv[optind]);
            status = 1;
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
    int logmask = setulogmask(LOG_UPTO(LOG_NOTICE));

    unotice(
"%s version %s\n"
"%s\n"
"\n"
"Usage: %s [-n|v|x] [-l log] [-u n] [-m addr] [-q queue] [-b npages] [-I iface]\n"
"          [-r <1|0>] [-t] [-s channel-name]                                   \n"
"where:\n"
"   -b npages   Allocate \"npages\" pages of memory for the internal buffer.\n"
"               Default is %lu pages. \"getconf PAGESIZE\" reveals page-size.\n"
"   -I iface    Listen for multicast packets on interface \"iface\".\n"
"               Default is to listen on all available interfaces.\n"
"   -l log      Log to file \"log\".  Default is to use the system logging\n"
"               daemon if the current process is a daemon (i.e., doesn't\n"
"               have a controlling terminal); otherwise, the standard error\n"
"               stream is used.\n"
"   -m addr     Read data from IPv4 dotted-quad multicast address \"addr\".\n"
"               Default is to read from the standard input stream.\n"
"   -n          Log through level NOTE. Report each data-product.\n"
"   -q queue    Use \"queue\" as LDM product-queue. Default is \"%s\".\n"
"   -u n        Use logging facility local\"n\". Default is to use the\n"
"               default LDM logging facility, %s.\n"
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
"SIGUSR1 causes statistics to be unconditionally logged at level NOTE.\n"
"SIGUSR2 rotates the logging level.\n",
        progName, PACKAGE_VERSION, copyright, progName, (unsigned
        long)npages, lpqGetQueuePath(), getFacilityName(getulogfacility()));

    (void)setulogmask(logmask);
}

/**
 * Tries to lock the current process in physical memory.
 */
static inline void
tryLockingProcessInMemory(void)
{
    if (lockProcessInMemory()) {
        LOG_ADD0("Couldn't lock process in physical memory");
        log_log(LOG_WARNING);
    }
}

/**
 * Handles SIGUSR1 by reporting statistics.
 *
 * @param[in] sig  The signal. Should be SIGUSR1.
 * @pre            {The input reader has been created.}
 */
static void
sigusr1_handler(
        const int sig)
{
    if (SIGUSR1 == sig) {
        (void)pthread_mutex_lock(&mutex);
        reportStatistics = true;
        (void)pthread_cond_signal(&cond);
        (void)pthread_mutex_unlock(&mutex);
    }
}

/*
 * Handles a signal.
 */
static void signal_handler(
        const int       sig)    /**< [in] Signal to be handled */
{
#ifdef SVR3SIGNALS
    /*
     * Some systems reset handler to SIG_DFL upon entry to handler.
     * In that case, we reregister our handler.
     */
    (void)signal(sig, signal_handler);
#endif

    switch (sig) {
        case SIGTERM:
            done = 1;
            if (fifo)
                fifo_close(fifo); // will cause input-reader to terminate
            break;
        case SIGUSR2: {
            unsigned logMask = getulogmask();

            if ((logMask & LOG_MASK(LOG_WARNING)) == 0) {
                (void)setulogmask(LOG_UPTO(LOG_WARNING));
            }
            else if ((logMask & LOG_MASK(LOG_NOTICE)) == 0) {
                (void)setulogmask(LOG_UPTO(LOG_NOTICE));
            }
            else if ((logMask & LOG_MASK(LOG_INFO)) == 0) {
                (void)setulogmask(LOG_UPTO(LOG_INFO));
            }
            else if ((logMask & LOG_MASK(LOG_DEBUG)) == 0) {
                (void)setulogmask(LOG_UPTO(LOG_DEBUG));
            }
            else {
                (void)setulogmask(LOG_UPTO(LOG_ERR));
            }
            break;
        }
    }

    return;
}

/**
 * Registers the SIGUSR1 handler.
 *
 * @param[in] ignore  Whether or not to ignore or handle SIGUSR1.
 */
static void
set_sigusr1Action(
        const bool ignore)
{
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = ignore ? SIG_IGN : sigusr1_handler;

#ifdef SA_RESTART   /* SVR4, 4.3+ BSD */
    /* Restart system calls for these */
    sigact.sa_flags |= SA_RESTART;
#endif

    (void)sigaction(SIGUSR1, &sigact, NULL);
}

/**
 * Registers the signal handler for most signals.
 */
static void set_sigactions(void)
{
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Ignore these */
    sigact.sa_handler = SIG_IGN;
    (void)sigaction(SIGHUP, &sigact, NULL);
    (void)sigaction(SIGALRM, &sigact, NULL);
    (void)sigaction(SIGCHLD, &sigact, NULL);
    (void)sigaction(SIGCONT, &sigact, NULL);

    /* Handle these */
    /*
     * SIGTERM must be handled in order to cleanly close the product-queue
     * (i.e., return the writer-counter of the product-queue to zero).
     */
    sigact.sa_handler = signal_handler;
    (void)sigaction(SIGTERM, &sigact, NULL);
#ifdef SA_RESTART   /* SVR4, 4.3+ BSD */
    /* Restart system calls for these */
    sigact.sa_flags |= SA_RESTART;
#endif
    (void)sigaction(SIGUSR2, &sigact, NULL);
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
 * Creates a product-maker and starts it on a new thread.
 *
 * @param[in]  attr            Pointer to thread-creation attributes
 * @param[in]  fifo            Pointer to FIFO from which to read data.
 * @param[in]  productQueue    Pointer to product-queue into which to put
 *                             created data-products.
 * @param[out] productMaker    Pointer to pointer to created product-maker.
 * @param[out] thread          Pointer to created thread.
 * @retval     0               Success.
 * @retval     USAGE_ERROR     Usage error. `log_start()` called.
 * @retval     SYSTEM_FAILURE  System failure. `log_start()` called.
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
        LOG_ADD0("Couldn't create new LDM product-maker");
    }
    else {
        status = pthread_create(thread, attr, pmStart, pm);

        if (status) {
            LOG_ERRNUM0(status, "Couldn't start product-maker thread");
            status = SYSTEM_FAILURE;
        }
        else {
            *productMaker = pm;
        }
    }

    return status;
}

/**
 * Returns the time interval between two timestamps.
 *
 * @return the time interval, in seconds, between the two timestamps.
 */
static double duration(
    const struct timeval*   later,      /**< [in] The later time */
    const struct timeval*   earlier)    /**< [in] The earlier time */
{
    return (later->tv_sec - earlier->tv_sec) +
        1e-6*(later->tv_usec - earlier->tv_usec);
}

/**
 * Returns the string representation of a time interval.
 *
 * @return The string representation of the given time interval.
 */
static void encodeDuration(
    char*       buf,        /**< [out] Buffer into which to encode the interval
                              */
    size_t      size,       /**< [in] Size of the buffer in bytes */
    double      duration)   /**< [in] The time interval in seconds */
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
    struct timeval          now;
    double                  interval;
    double                  rate;
    char                    buf[80];
    int                     logmask;
    unsigned long           byteCount;
    unsigned long           packetCount, missedPacketCount, prodCount;
    unsigned long           fullFifoCount;
    static unsigned long    totalPacketCount;
    static unsigned long    totalMissedPacketCount;
    static unsigned long    totalProdCount;
    static unsigned long    totalByteCount;
    static unsigned long    totalFullFifoCount;

    (void)gettimeofday(&now, NULL);
    readerGetStatistics(reader, &byteCount, &fullFifoCount);
    pmGetStatistics(productMaker, &packetCount, &missedPacketCount, &prodCount);

    totalByteCount += byteCount;
    totalPacketCount += packetCount;
    totalMissedPacketCount += missedPacketCount;
    totalProdCount += prodCount;
    totalFullFifoCount += fullFifoCount;

    logmask = setulogmask(LOG_UPTO(LOG_NOTICE));

    unotice("----------------------------------------");
    unotice("Ingestion Statistics:");
    unotice("    Since Previous Report (or Start):");
    interval = duration(&now, reportTime);
    encodeDuration(buf, sizeof(buf), interval);
    unotice("        Duration          %s", buf);
    unotice("        Raw Data:");
    unotice("            Octets        %lu", byteCount);
    unotice("            Mean Rate:");
    rate = (byteCount/interval);
    unotice("                Octets    %g/s", rate);
    unotice("                Bits      %g/s", 8*rate);
    unotice("        Received packets:");
    unotice("            Number        %lu", packetCount);
    unotice("            Mean Rate     %g/s", packetCount/interval);
    unotice("        Missed packets:");
    unotice("            Number        %lu", missedPacketCount);
    unotice("            %%             %g",
            100.0 * missedPacketCount / (missedPacketCount + packetCount));
    unotice("        Full FIFO:");
    unotice("            Number        %lu", fullFifoCount);
    unotice("            %%             %g",
            100.0 * fullFifoCount / packetCount);
    unotice("        Products:");
    unotice("            Inserted      %lu", prodCount);
    unotice("            Mean Rate     %g/s", prodCount/interval);
    unotice("    Since Start:");
    interval = duration(&now, startTime);
    encodeDuration(buf, sizeof(buf), interval);
    unotice("        Duration          %s", buf);
    unotice("        Raw Data:");
    unotice("            Octets        %lu", totalByteCount);
    unotice("            Mean Rate:");
    rate = (totalByteCount/interval);
    unotice("                Octets    %g/s", rate);
    unotice("                Bits      %g/s", 8*rate);
    unotice("        Received packets:");
    unotice("            Number        %lu", totalPacketCount);
    unotice("            Mean Rate     %g/s", totalPacketCount/interval);
    unotice("        Missed packets:");
    unotice("            Number        %lu", totalMissedPacketCount);
    unotice("            %%             %g", 100.0 * totalMissedPacketCount /
            (totalMissedPacketCount + totalPacketCount));
    unotice("        Full FIFO:");
    unotice("            Number        %lu", totalFullFifoCount);
    unotice("            %%             %g",
            100.0 * totalFullFifoCount / totalPacketCount);
    unotice("        Products:");
    unotice("            Inserted      %lu", totalProdCount);
    unotice("            Mean Rate     %g/s", totalProdCount/interval);

#ifdef RETRANS_SUPPORT
   if(retrans_xmit_enable == OPTION_ENABLE){
    unotice("       Retransmissions:");
    unotice("           Requested     %lu", total_prods_retrans_rqstd);
    unotice("           Received      %lu", total_prods_retrans_rcvd);
    unotice("           Duplicates    %lu", total_prods_retrans_rcvd_notlost);
    unotice("           No duplicates %lu", total_prods_retrans_rcvd_lost);
    }
#endif 
    unotice("----------------------------------------");

    (void)setulogmask(logmask);

    *reportTime = now;
}

/**
 * Reports statistics when signaled. May be called by `pthread_create()`.
 *
 * @param[in] arg   Pointer to the relevant `StatsStruct`. The caller must not
 *                  modify or free.
 * @retval    NULL  Always.
 */
static void* startReporter(void* arg)
{
    StatsStruct         ss = *(StatsStruct*)arg;

    (void)pthread_mutex_lock(&mutex);

    do {
        while (!reportStatistics)
            (void)pthread_cond_wait(&cond, &mutex);

        reportStats(ss.productMaker, &ss.startTime, &ss.reportTime,
                ss.reader);
        reportStatistics = false;
    } while (!done);

    (void)pthread_mutex_unlock(&mutex);

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
 * @param[in] attr            Pointer to uninitialized thread attributes object.
 *                            Caller should call `pthread_attr_destroy(*attr)`
 *                            when it's no longer needed.
 * @param[in] isMcastInput    Is input from multicast?
 * @param[in] policy          Scheduling policy. Ignored if input isn't from
 *                            multicast.
 * @param[in] priority        Thread priority. Ignored if input isn't from
 *                            multicast. Must be consonant with `policy`.
 * @retval    0               Success. `*attr` is initialized.
 * @retval    USAGE_ERROR     Usage error. `log_start()` called.
 * @retval    SYSTEM_FAILURE  System error. `log_start()` called.
 */
static int
initThreadAttr(
        pthread_attr_t* const attr,
        const int             policy,
        const bool            isMcastInput,
        const int             priority)
{
    int status = pthread_attr_init(attr);

    if (status) {
        LOG_ERRNUM0(status, "Couldn't initialize thread attributes object");
        status = (EBUSY == status) ? USAGE_ERROR : SYSTEM_FAILURE;
    }
    else if (isMcastInput) {
        #ifndef _POSIX_THREAD_PRIORITY_SCHEDULING
            uwarn("Can't adjust thread scheduling due to lack of support from "
                    "the environment");
        #else
            struct sched_param  param;

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
        const bool                 isMcastInput,
        const char* const restrict mcastSpec)
{
    #ifdef RETRANS_SUPPORT
        if (isMcastInput && retrans_xmit_enable == OPTION_ENABLE) {
            // Copy mcastAddress needed to obtain the cpio entries
            (void)strcpy(mcastAddr, mcastSpec);
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
 * Creates an input-reader and runs it in a new thread.
 *
 * @param[in]  attr            Pointer to thread-creation attributes
 * @param[out] thread          Pointer to created thread.
 * @param[in]  mcastSpec       Specification of multicast group or NULL if
 *                             input is from the standard input stream.
 * @param[in]  interface       Specification of interface on which to listen or
 *                             NULL to listen on all interfaces.
 * @param[out] reader          Reader of input. Caller should call
 *                             `readerFree(*reader)` when it's no longer needed.
 * @retval     0               Success. `*reader` is set.
 * @retval     USAGE_ERROR     Usage error. `log_start()` called.
 * @retval     SYSTEM_FAILURE  System failure. `log_start()` called.
 */
static int
spawnReader(
    const pthread_attr_t* const restrict attr,
    pthread_t* const restrict            thread,
    const char* const restrict           mcastSpec,
    const char* const restrict           interface,
    Reader** const restrict              reader)
{
    Reader* rdr;
    int     status = mcastSpec
            ? multicastReaderNew(mcastSpec, interface, fifo, &rdr)
            : fileReaderNew(NULL, fifo, &rdr);

    if (status) {
        LOG_ADD0("Couldn't create input-reader");
    }
    else {
        status = pthread_create(thread, attr, readerStart, rdr);

        if (status) {
            LOG_ERRNUM0(status, "Couldn't create input-reader thread");
            readerFree(rdr);
            status = SYSTEM_FAILURE;
        }
        else {
            *reader = rdr;
        }
    } // `rdr` is set

    return status;
}

/**
 * Creates and starts an input-reader on a separate thread.
 *
 * @param[in]  isMcastInput    Is the input from multicast?
 * @param[in]  policy          Scheduling policy for the reader thread. Ignored
 *                             if the input isn't from multicast.
 * @param[in]  priority        Scheduling priority for the reader thread.
 *                             Ignored if the input isn't from multicast. Must
 *                             be consonant with `policy`.
 * @param[in]  mcastSpec       Specification of multicast group or NULL if input
 *                             is from the standard input stream.
 * @param[in]  interface       Specification of interface on which to listen or
 *                             NULL to listen on all interfaces.
 * @param[out] reader          Reader of input. Caller should call
 *                             `readerFree(*reader)` when it's no longer needed.
 * @param[out] thread          Thread on which the input-reader is executing.
 * @retval     0               Success. `*reader` and `*thread` are set.
 * @retval     USAGE_ERROR     Usage error. `log_start()` called.
 * @retval     SYSTEM_FAILURE  System failure. `log_start()` called.
 */
static int
startReader(
        const bool                 isMcastInput,
        const int                  policy,
        const int                  priority,
        const char* const restrict mcastSpec,
        const char* const restrict interface,
        Reader** const restrict    reader,
        pthread_t* const restrict  thread)
{
    pthread_attr_t attr;
    int            status = initThreadAttr(&attr, isMcastInput, policy,
            priority);

    if (0 == status) {
        unblockTermSignals();
        status = spawnReader(&attr, thread, mcastSpec, interface, reader);
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
        LOG_ERRNUM0(status, "Couldn't join input-reader thread");
        status = SYSTEM_FAILURE;
    }
    else {
        status = done ? 0 : *(int*)voidPtr;

        if (status)
            LOG_ADD1("Input-reader thread returned %d", status);
    }

    return status;
}

/**
 * Runs the inner core of this program. The FIFO is closed on return and the
 * product-maker thread is joined. Final statistics are reported on success.
 *
 * @param[in]  productMaker    The maker of LDM data-products.
 * @param[in]  pmThread        The thread on which the product-maker is
 *                             executing.
 * @param[in]  isMcastInput    Is the input from multicast?
 * @param[in]  policy          Scheduling policy for the reader thread. Ignored
 *                             if the input isn't from multicast.
 * @param[in]  priority        Scheduling priority for the reader thread.
 *                             Ignored if the input isn't from multicast. Must
 *                             be consonant with `policy`.
 * @param[in]  mcastSpec       Specification of multicast group or NULL if input
 *                             is from the standard input stream.
 * @param[in]  interface       Specification of interface on which to listen or
 *                             NULL to listen on all interfaces.
 * @retval     0               Success.
 * @retval     USAGE_ERROR     Usage error. `log_start()` called.
 * @retval     SYSTEM_FAILURE  System failure. `log_start()` called.
 */
static int
runInner(
        ProductMaker* const restrict productMaker,
        pthread_t const              pmThread,
        const bool                   isMcastInput,
        const int                    policy,
        const int                    priority,
        const char* const restrict   mcastSpec,
        const char* const restrict   interface)
{
    Reader*     reader;
    pthread_t   readerThread;
    int         status = startReader(isMcastInput, policy, priority, mcastSpec,
            interface, &reader, &readerThread);
    pthread_t   reporterThread;
    bool        reporterRunning = false;

    if (status) {
        LOG_ADD0("Couldn't start input-reader");
    }
    else {
        pthread_mutexattr_t attr;
        status = pthread_mutexattr_init(&attr);
        if (status) {
            LOG_ERRNUM0(status, "Couldn't initialize mutex attributes");
        }
        else {
            // At most one lock per thread
            (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
            // Prevent priority inversion
            (void)pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
            (void)pthread_mutex_init(&mutex, &attr);
            (void)pthread_mutexattr_destroy(&attr);

            StatsStruct ss;
            ss_init(&ss, productMaker, reader);
            (void)pthread_create(&reporterThread, NULL, startReporter, &ss);
            reporterRunning = true;

            set_sigusr1Action(false);  // enable stats reporting; requires reader
            status = waitOnReader(readerThread);
        } // `attr` initialized
    } // input-reader started

    // Ensures `pmThread` termination; idempotent => can't hurt
    fifo_close(fifo);
    (void)pthread_join(pmThread, NULL);

    /*
     * Final statistics are reported only after the product-maker has
     * terminated to prevent a race condition in logging and consequent
     * variability in the output -- which can affect testing.
     */
    if (reporterRunning) {
        done = 1;  // causes reporting thread to terminate
        raise(SIGUSR1);  // reports statistics; requires reader
        (void)pthread_join(reporterThread, NULL);
        readerFree(reader);
    }

    return status;
}

/**
 * Runs the outer core of this program.
 *
 * @param[in] fifo            Pointer to FIFO object. Will be closed upon
 *                            return.
 * @param[in] prodQueue       Pointer to LDM product-queue object.
 * @param[in] mcastSpec       Specification of multicast group or NULL if
 *                            input is from the standard input stream.
 * @param[in] interface       Specification of interface on which to listen or
 *                            NULL to listen on all interfaces.
 * @retval    0               Success.
 * @retval    USAGE_ERROR     Usage error. `log_start()` called.
 * @retval    SYSTEM_FAILURE  System failure. `log_start()` called.
 */
static int
runOuter(
        Fifo* const restrict            fifo,
        LdmProductQueue* const restrict prodQueue,
        const char* const restrict      mcastSpec,
        const char* const restrict      interface)
{
    const bool      isMcastInput = NULL != mcastSpec;
    pthread_attr_t  attr;
    /*
     * If the input is multicast UDP packets, then the product-maker thread
     * runs at a lower priority than the input thread to reduce the chance
     * of the input thread missing a packet.
     */
    int             maxPriority = sched_get_priority_max(SCHED_POLICY);
    int             status = initThreadAttr(&attr, isMcastInput, SCHED_POLICY,
            maxPriority-1);

    if (0 == status) {
        initRetransSupport(isMcastInput, mcastSpec);

        /*
         * Termination signals are blocked for all threads except the
         * input-reader thread, which might have the highest priority,
         */
        blockTermSignals();
        ProductMaker* productMaker;
        pthread_t     pmThread;
        status = spawnProductMaker(&attr, fifo, prodQueue, &productMaker,
                &pmThread);

        if (0 == status)
            status = runInner(productMaker, pmThread, isMcastInput,
                    SCHED_POLICY, maxPriority, mcastSpec, interface);

        destroyRetransSupport(isMcastInput);
        (void)pthread_attr_destroy(&attr);
    }   // `attr` initialized

    return status;
}

/**
 * Executes this program.
 *
 * @param[in] prodQueuePath   Pathname of product-queue.
 * @param[in] mcastSpec       Specification of multicast group or NULL if input
 *                            is from the standard input stream.
 * @param[in] interface       Specification of interface on which to listen or
 *                            NULL to listen on all interfaces.
 * @retval    0               Success.
 * @retval    USAGE_ERROR     Usage error. `log_start()` called.
 * @retval    SYSTEM_FAILURE  O/S failure. `log_start()` called.
 */
static int
execute(
        const size_t               npages,
        const char* const restrict prodQueuePath,
        const char* const restrict mcastSpec,
        const char* const restrict interface)
{
    int status = fifo_new(npages, &fifo);

    if (0 == status) {
        LdmProductQueue*    prodQueue;

        set_sigactions();   // to ensure product-queue is closed cleanly
        status = lpqGet(prodQueuePath, &prodQueue);

        if (3 == status) {
            status = USAGE_ERROR;
        }
        else if (0 == status) {
            status = runOuter(fifo, prodQueue, mcastSpec, interface);
            (void)lpqClose(prodQueue);
        }                       /* "prodQueue" open */

        fifo_free(fifo);
    }                           /* "fifo" created */

    return status;
}

/**
 * Reads a NOAAPORT data stream, creates LDM data-products from the stream, and
 * inserts the data-products into an LDM product-queue.  The NOAAPORT data
 * stream can take the form of multicast UDP packets from (for example) a
 * Novra S300 DVB-S2 receiver or the standard input stream.
 *
 * Usage:
 *     noaaportIngester [-l <em>log</em>] [-n|-v|-x] [-q <em>queue</em>] [-u <em>n</em>] [-m <em>mcastAddr</em>] [-I <em>iface</em>] [-b <em>npages</em>]\n
 *
 * Where:
 * <dl>
 *      <dt>-b <em>npages</em></dt>
 *      <dd>Allocate \e npages pages of memory for the internal buffer.</dd>
 *
 *      <dt>-I <em>iface</em></dt>
 *      <dd>Listen for multicast packets on interface \e iface.</dd>
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
    /*
     * Initialize logging. Done first in case something happens that needs to
     * be reported.
     */
    const char* const progname = basename(argv[0]);
    log_initLogging(progname, LOG_WARNING, LOG_LDM);

    set_sigusr1Action(true);  // ignore SIGUSR1 initially

    size_t            npages = 5000;
    char*             prodQueuePath = NULL;
    char*             mcastSpec = NULL;
    char*             interface = NULL;
    const char* const COPYRIGHT_NOTICE = "Copyright (C) 2014 University "
            "Corporation for Atmospheric Research";
    int status = decodeCommandLine(argc, argv, &npages, &prodQueuePath,
            &mcastSpec, &interface);

    if (status) {
        log_add("Couldn't decode command-line");
        log_log(LOG_ERR);
        usage(progname, npages, COPYRIGHT_NOTICE);
    }
    else {
        unotice("Starting Up %s", PACKAGE_VERSION);
        unotice("%s", COPYRIGHT_NOTICE);

        tryLockingProcessInMemory(); // because NOAAPORT is realtime
        status = execute(npages, prodQueuePath, mcastSpec, interface);

        if (status)
            log_log(LOG_ERR);
    }                               /* command line decoded */

    return status;
}
