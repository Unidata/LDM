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
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/*********** For Retransmission ****************/
#ifdef RETRANS_SUPPORT
#include "retrans.h" 
#endif
/*********** For Retransmission ****************/

static const int         USAGE_ERROR = 1;
static const int         SYSTEM_FAILURE = 2;
static const int         SCHED_POLICY = SCHED_FIFO;
static size_t            npages = 5000;
static const char*       prodQueuePath = NULL;
static const char*       mcastSpec = NULL;
static const char*       interface = NULL;
static Fifo*             fifo;
static Reader*           reader;
static ProductMaker*     productMaker;
static const char* const COPYRIGHT_NOTICE = \
    "Copyright (C) 2014 University Corporation for Atmospheric Research";
static struct timeval    startTime;      /**< Start of execution */
static struct timeval    reportTime;     /**< Time of last report */
static int               reportStatistics;
static pthread_mutex_t   mutex;
static pthread_cond_t    cond = PTHREAD_COND_INITIALIZER;

/**
 * Decodes the command-line.
 *
 * @param[in] argc  Number of arguments.
 * @param[in] argv  Arguments.
 * @retval    0     Success.
 * @retval    1     Error. `log_start()` called.
 */
static int
decodeCommandLine(
        int    argc,
        char** argv)
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
                    npages = n;
                }
                break;
            }
            case 'I':
                interface = optarg;
                break;
            case 'l':
                if (openulog(getulogident(), ulog_get_options(),
                        getulogfacility(), optarg) == -1)
                    status = 1;
                break;
            case 'm':
                mcastSpec = optarg;
                break;
            case 'n':
                (void)setulogmask(getulogmask() | LOG_MASK(LOG_NOTICE));
                break;
            case 'q':
                prodQueuePath = optarg;
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
                            logFacilities[i], optarg) == -1)
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
 */
static void usage(
    const char* const   progName)       /**< [in] Program name */
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
        progName, PACKAGE_VERSION, COPYRIGHT_NOTICE, progName, (unsigned
        long)npages, lpqGetQueuePath(), getFacilityName(getulogfacility()));

    (void)setulogmask(logmask);
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
                fifo_close(fifo);
            break;
        case SIGUSR1:
            if (NULL != reader) {
                (void)pthread_mutex_lock(&mutex);
                reportStatistics = 1;
                (void)pthread_cond_signal(&cond);
                (void)pthread_mutex_unlock(&mutex);
            }
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

/*
 * Registers the signal_handler
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
    (void)sigaction(SIGUSR1, &sigact, NULL);
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

    if (0 != status) {
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
 */
static void reportStats(void)
{
    struct timeval          now;
    double                  interval;
    double                  rate;
    char                    buf[80];
    int                     logmask;
    unsigned long           byteCount;
    unsigned long           packetCount, missedPacketCount, prodCount;
    static unsigned long    totalPacketCount;
    static unsigned long    totalMissedPacketCount;
    static unsigned long    totalProdCount;
    static unsigned long    totalByteCount;

    (void)gettimeofday(&now, NULL);
    readerGetStatistics(reader, &byteCount);
    pmGetStatistics(productMaker, &packetCount, &missedPacketCount, &prodCount);

    totalByteCount += byteCount;
    totalPacketCount += packetCount;
    totalMissedPacketCount += missedPacketCount;
    totalProdCount += prodCount;

    logmask = setulogmask(LOG_UPTO(LOG_NOTICE));

    log_start("----------------------------------------");
    log_add("Ingestion Statistics:");
    log_add("    Since Previous Report (or Start):");
    interval = duration(&now, &reportTime);
    encodeDuration(buf, sizeof(buf), interval);
    log_add("        Duration          %s", buf);
    log_add("        Raw Data:");
    log_add("            Octets        %lu", CHAR_BIT*byteCount/8);
    log_add("            Mean Rate:");
    rate = (byteCount/interval)*(CHAR_BIT/8.0);
    log_add("                Octets    %g/s", rate);
    log_add("                Bits      %g/s", 8*rate);
    log_add("        Received packets:");
    log_add("            Number        %lu", packetCount);
    log_add("            Mean Rate     %g/s", packetCount/interval);
    log_add("        Missed packets:");
    log_add("            Number        %lu", missedPacketCount);
    log_add("            %%             %g",
            100.0 * missedPacketCount / (missedPacketCount + packetCount));
    log_add("        Products:");
    log_add("            Inserted      %lu", prodCount);
    log_add("            Mean Rate     %g/s", prodCount/interval);
    log_add("    Since Start:");
    interval = duration(&now, &startTime);
    encodeDuration(buf, sizeof(buf), interval);
    log_add("        Duration          %s", buf);
    log_add("        Raw Data:");
    log_add("            Octets        %lu", CHAR_BIT*totalByteCount/8);
    log_add("            Mean Rate:");
    rate = (totalByteCount/interval)*(CHAR_BIT/8.0);
    log_add("                Octets    %g/s", rate);
    log_add("                Bits      %g/s", 8*rate);
    log_add("        Received packets:");
    log_add("            Number        %lu", totalPacketCount);
    log_add("            Mean Rate     %g/s", totalPacketCount/interval);
    log_add("        Missed packets:");
    log_add("            Number        %lu", totalMissedPacketCount);
    log_add("            %%             %g", 100.0 * totalMissedPacketCount /
            (totalMissedPacketCount + totalPacketCount));
    log_add("        Products:");
    log_add("            Inserted      %lu", totalProdCount);
    log_add("            Mean Rate     %g/s", totalProdCount/interval);

#ifdef RETRANS_SUPPORT
   if(retrans_xmit_enable == OPTION_ENABLE){
    log_add("       Retransmissions:");
    log_add("           Requested     %lu", total_prods_retrans_rqstd);
    log_add("           Received      %lu", total_prods_retrans_rcvd);
    log_add("           Duplicates    %lu", total_prods_retrans_rcvd_notlost);
    log_add("           No duplicates %lu", total_prods_retrans_rcvd_lost);
    }
#endif 
    log_add("----------------------------------------");

    log_log(LOG_NOTICE);
    (void)setulogmask(logmask);

    reportTime = now;
}

/**
 * Reports statistics when signaled.
 */
static void* reportStatsWhenSignaled(void* arg)
{
    pthread_mutexattr_t attr;
    int                 oldType;

    (void)pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldType);
    (void)pthread_mutexattr_init(&attr);
    (void)pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    (void)pthread_mutex_init(&mutex, &attr);

    (void)pthread_mutex_lock(&mutex);

    for (;;) {
        while (!reportStatistics) {
            (void)pthread_cond_wait(&cond, &mutex);
        }

        reportStats();
        reportStatistics = 0;
    }

    return NULL;
}

/**
 * Starts the statistics-reporting thread. The thread is detached.
 */
static void
startReportingThread(void)
{
    pthread_t thread;

    (void)gettimeofday(&startTime, NULL);
    reportTime = startTime;
    (void)pthread_create(&thread, NULL, reportStatsWhenSignaled, NULL);
    (void)pthread_detach(thread);
}

/**
 * Initializes thread attributes.
 *
 * @param[in] attr            Pointer to uninitialized thread attributes object.
 *                            Caller should call `pthread_attr_destroy(*attr)`
 *                            when it's no longer needed.
 * @param[in] isMcastInput    Is input from multicast?
 * @param[in] priority        Thread priority. Ignored if input isn't from
 *                            multicast.
 * @retval    0               Success. `*attr` is initialized.
 * @retval    USAGE_ERROR     Usage error. `log_start()` called.
 * @retval    SYSTEM_FAILURE  System error. `log_start()` called.
 */
static int
initThreadAttr(
        pthread_attr_t* const attr,
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
            uwarn("Can't adjust thread priorities due to lack of "
                    "necessary support from environment");
        #else
            struct sched_param  param;

            param.sched_priority = priority;

            (void)pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
            (void)pthread_attr_setschedpolicy(attr, SCHED_POLICY);
            (void)pthread_attr_setschedparam(attr, &param);
            // System-wide scope because this process is crucial
            (void)pthread_attr_setscope(attr, PTHREAD_SCOPE_SYSTEM);
        #endif
    }

    return status;
}

/**
 * Sets the attributes of the current thread if appropriate.
 *
 * @param[in] isMcastInput    Is input from multicast? This function does
 *                            nothing if the input isn't from multicast.
 * @param[in] priority        Thread priority. Ignored if input isn't from
 *                            multicast.
 * @retval    0               Success.
 * @retval    SYSTEM_FAILURE  System failure. `log_start()` called.
 * @retval    USAGE_ERROR     Usage error. `log_start()` called.
 */
static int
setCurrentThreadAttr(
        const bool isMcastInput,
        const int  priority)
{
    int status;

    if (!isMcastInput) {
        status = 0;
    }
    else {
        pthread_attr_t attr;

        status = initThreadAttr(&attr, isMcastInput, priority);

        if (0 == status) {
            int                schedPolicy;
            struct sched_param schedParam;

            (void)pthread_attr_getschedpolicy(&attr, &schedPolicy);
            (void)pthread_attr_getschedparam(&attr, &schedParam);

            status = pthread_setschedparam(pthread_self(), schedPolicy,
                    &schedParam);

            if (status) {
                LOG_SERROR0("Couldn't set attributes of current thread");
                status = (ENOMEM == status) ? SYSTEM_FAILURE : USAGE_ERROR;
            }

            (void)pthread_attr_destroy(&attr);
        } // `attr` initialized
    } // is multicast input

    return status;
}

/**
 * Initialize support for retransmission requests. Does nothing if
 * retransmission support isn't enabled at compile-time or if the input isn't
 * from multicast UDP packets.
 *
 * @param[in] isMcastInput  Is the input from multicast UDP packets?
 */
static void
initRetransSupport(
        const bool isMcastInput)
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
 * Reads input and puts the data into `fifo`. If `mcastSpec` is non-NULL, then
 * input is read from that multicast UDP address on `interface`; otherwise,
 * input is read from the standard input stream. Sets `reader`. Doesn't free
 * `reader`.
 *
 * @retval    0               Success. EOF encountered or `done` is set.
 * @retval    SYSTEM_FAILURE  System failure. `log_log()` called.
 * @retval    USAGE_ERROR     Usage error. `log_log()` called.
 */
static int
readInput(void)
{
    int     status = mcastSpec
            ? multicastReaderNew(mcastSpec, interface, fifo, &reader)
            : fileReaderNew(NULL, fifo, &reader);

    if (status) {
        LOG_ADD0("Couldn't create input-reader");
    }
    else {
        status = *(int*)(readerStart(reader));

        if (done)
            status = 0;
    }

    return status;
}

/**
 * Runs this program.
 *
 * @param[in] fifo            Pointer to FIFO object.
 * @param[in] prodQueue       Pointer to LDM product-queue object.
 * @retval    0               Success.
 * @retval    USAGE_ERROR     Usage error. `log_start()` called.
 * @retval    SYSTEM_FAILURE  System failure. `log_start()` called.
 */
static int
run(
        Fifo* const restrict            fifo,
        LdmProductQueue* const restrict prodQueue)
{
    int             status;
    const bool      isMcastInput = NULL != mcastSpec;
    pthread_attr_t  attr;
    int             maxPriority = sched_get_priority_max(SCHED_POLICY);

    /*
     * If the input is multicast UDP packets, then the product-maker thread
     * runs at a lower priority than the input thread to reduce the chance
     * of the input thread missing a packet.
     */
    status = initThreadAttr(&attr, isMcastInput, maxPriority-1);

    if (0 == status) {
        initRetransSupport(isMcastInput); // initialize retransmission support

        /*
         * Termination signals are blocked for all other threads so that they
         * are only received on this, possibly higher-priority, input thread.
         */
        blockTermSignals();
        startReportingThread();           // detached thread
        pthread_t productMakerThread;
        status = spawnProductMaker(&attr, fifo, prodQueue, &productMaker,
                &productMakerThread);
        unblockTermSignals();

        if (0 == status) {
            status = setCurrentThreadAttr(isMcastInput, maxPriority);

            if (0 == status) {
                status = readInput();

                if (status)
                    LOG_ADD0("Error reading input");
            }

            fifo_close(fifo); // idempotent => can't hurt
            (void)pthread_join(productMakerThread, NULL);

            /*
             * Final statistics are reported only after the product-maker has
             * terminated to prevent a race condition in logging and consequent
             * variable output -- which can affect testing.
             */
            if (reader)
                reportStats(); // requires `reader`
            readerFree(reader);
        }       // `productMakerThread` running

        destroyRetransSupport(isMcastInput);
        (void)pthread_attr_destroy(&attr);
    }   // `attr` initialized

    return status;
}

/**
 * Executes this program.
 *
 * @retval    0               Success.
 * @retval    USAGE_ERROR     Usage error. `log_start()` called.
 * @retval    SYSTEM_FAILURE  O/S failure. `log_start()` called.
 */
static int
execute(void)
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
            status = run(fifo, prodQueue);
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

    int status = decodeCommandLine(argc, argv);

    if (status) {
        log_add("Couldn't decode command-line");
        log_log(LOG_ERR);
        usage(progname);
    }
    else {
        unotice("Starting Up %s", PACKAGE_VERSION);
        unotice("%s", COPYRIGHT_NOTICE);

        status = execute();
    }                               /* command line decoded */

    return status;
}
