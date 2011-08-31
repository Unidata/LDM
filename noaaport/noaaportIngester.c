/*
 *   Copyright © 2011, University Corporation for Atmospheric Research.
 *   See COPYRIGHT file for copying and redistribution conditions.
 */
/**
 *   @file noaaportIngester.c
 *
 *   This file contains the code for the \c noaaportIngester(1) program. This
 *   program reads NOAAPORT data from a file or multicast packet stream,
 *   creates LDM data-products, and writes the data-products into an LDM
 *   product-queue.
 */
#include <config.h>

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <ldm.h>
#include <ulog.h>

#include "noaaportLog.h"
#include "fifo.h"
#include "fileReader.h"
#include "ldmProductQueue.h"
#include "multicastReader.h"
#include "productMaker.h"
#include "reader.h"

static pthread_t        readerThread;
static Reader*          reader;
static ProductMaker*    productMaker;
static pthread_t        productMakerThread;
static const size_t     DEFAULT_NPAGES = 5000;
static unsigned         logFacility = LOG_LDM;  /* default LDM facility */
static const char*      COPYRIGHT_NOTICE = \
    "Copyright (C) 2011 University Corporation for Atmospheric Research";
static struct timeval   startTime;      /**< Start of execution */
static struct timeval   reportTime;     /**< Time of last report */
static int              reportStatistics;
static pthread_mutex_t  mutex;
static pthread_cond_t   cond = PTHREAD_COND_INITIALIZER;

/**
 * Unconditionally logs a usage message.
 */
static void usage(
    const char* const   progName)       /**< [in] Program name */
{
    int logmask = setulogmask(LOG_UPTO(LOG_NOTICE));

    nplNotice(
"%s version %s\n"
"%s\n"
"\n"
"Usage: %s [-n|v|x] [-l log] [-u n] [-m addr] [-q queue] [-b npages] [-I iface]\n"
"where:\n"
"   -b npages   Allocate \"npages\" pages of memory for the internal buffer.\n"
"               Default is %lu pages.\n"
"   -I iface    Listen for multicast packets on interface \"iface\".\n"
"               Default is to listen on all available interfaces.\n"
"   -l log      Log to file \"log\".  Default is to use the system logging\n"
"               daemon if the current process is a daemon; otherwise, the\n"
"               standard error stream is used.\n"
"   -m addr     Read data from IPv4 dotted-quad multicast address \"addr\".\n"
"               Default is to read from the standard input stream.\n"
"   -n          Log through level NOTE. Report each data-product.\n"
"   -q queue    Use \"queue\" as LDM product-queue. Default is \"%s\".\n"
"   -u n        Use logging facility local\"n\". Default is to use the\n"
"               default LDM logging facility, %s.\n"
"   -v          Log through level INFO.\n"
"   -x          Log through level DEBUG. Too much information.\n"
"\n"
"If neither \"-n\", \"-v\", nor \"-x\" is specified, then only levels ERROR\n"
"and WARN are logged.\n"
"\n"
"SIGUSR1 causes statistics to be unconditionally logged at level NOTE.\n"
"SIGUSR2 rotates the logging level.\n",
        progName, PACKAGE_VERSION, COPYRIGHT_NOTICE, progName, (unsigned
        long)DEFAULT_NPAGES, lpqGetQueuePath(), getFacilityName(LOG_LDM));

    (void)setulogmask(logmask);
}

/**
 * Initializes logging.
 *
 * @retval 0    Success
 * @retval 1    Usage error.
 */
static int initLogging(
    const char* const   progName,       /**< [in] Name of the program */
    const unsigned      logOptions,     /**< [in] Logging options */
    const unsigned      logFacility,    /**< [in] Logging facility */
    const char* const   logPath)        /**< [in] Pathname of the log file,
                                          *  "-", or NULL */
{
    int status;

    if (openulog(progName, logOptions, logFacility, logPath) == -1) {
        NPL_SERROR0("Couldn't initialize logging");
        nplLog(LOG_ERR);
        usage(progName);
        status = 1;
    }
    else {
        status = 0;
    }

    return status;
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
        case SIGINT:
            exit(1);
        case SIGTERM:
            (void)pthread_cancel(readerThread);
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
#ifdef SA_RESTART   /* SVR4, 4.3+ BSD */
    /* Usually, restart system calls */
    sigact.sa_flags |= SA_RESTART;
#endif
    sigact.sa_handler = signal_handler;
    (void)sigaction(SIGTERM, &sigact, NULL);
    (void)sigaction(SIGUSR1, &sigact, NULL);
    (void)sigaction(SIGUSR2, &sigact, NULL);

    /* Don't restart after interrupt */
    sigact.sa_flags = 0;
#ifdef SA_INTERRUPT /* SunOS 4.x */
    sigact.sa_flags |= SA_INTERRUPT;
#endif
    (void)sigaction(SIGINT, &sigact, NULL);
}

/**
 * Creates a product-maker and starts it in a new thread.
 *
 * @retval 0    Success.
 * @retval 1    Usage failure. \c nplStart() called.
 * @retval 2    O/S failure. \c nplStart() called.
 */
static int spawnProductMaker(
    const pthread_attr_t* const attr,           /**< [in] Thread-creation
                                                  *  attributes */
    Fifo* const                 fifo,           /**< [in] Pointer to FIFO from
                                                  *  which to get data */
    LdmProductQueue* const      productQueue,   /**< [in] LDM product-queue into
                                                  *  which to put data-products
                                                  *  */
    ProductMaker** const        productMaker,   /**< [out] Pointer to pointer to
                                                  *  returned product-maker */
    pthread_t* const            thread)         /**< [out] Pointer to pointer
                                                  *  to created thread */
{
    ProductMaker*   pm;
    int             status = pmNew(fifo, productQueue, &pm);

    if (0 != status) {
        NPL_ADD0("Couldn't create new LDM product-maker");
        status = 1;
    }
    else {
        pthread_t   thrd;

        if (0 != (status = pthread_create(&thrd, attr, pmStart, pm))) {
            NPL_ERRNUM0(status, "Couldn't start product-maker thread");
            status = 1;
        }
        else {
            *productMaker = pm;
            *thread = thrd;
        }
    }

    return status;
}

/**
 * Creates a file data-reader and starts it in a new thread.
 *
 * @retval 0    Success
 * @retval 1    Usage failure. \c nplStart() called.
 * @retval 2    O/S failure. \c nplStart() called.
 */
static int spawnFileReader(
    const pthread_attr_t* const attr,       /**< [in] Thread-creation
                                              *  attributes */
    const char* const           pathname,   /**< [in] Pathname of input file or
                                              *  NULL to read standard input
                                              *  stream */
    Fifo* const                 fifo,       /**< [in] Pointer to FIFO into
                                              *  which to put data */
    Reader** const              reader,     /**< [out] Pointer to pointer to
                                              *  address of reader */
    pthread_t* const            thread)     /**< [out] Pointer to pointer to
                                              *  created thread */
{
    Reader*             fileReader;
    int                 status = fileReaderNew(NULL, fifo, &fileReader);

    if (0 != status) {
        NPL_ADD0("Couldn't create file-reader");
    }
    else {
        pthread_t   thrd;

        if ((status = pthread_create(&thrd, attr, readerStart, fileReader)) !=
                0) {
            NPL_ERRNUM0(status, "Couldn't start file-reader thread");
            status = 1;
        }
        else {
            *reader = fileReader;
            *thread = thrd;
        }
    }

    return status;
}

/**
 * Creates a multicast data-reader and starts it in a new thread.
 *
 * @retval 0    Success
 * @retval 1    Usage failure. \c nplStart() called.
 * @retval 2    O/S failure. \c nplStart() called.
 */
static int spawnMulticastReader(
    const pthread_attr_t* const attr,       /**< [in] Thread-creation
                                              *  attributes */
    const char* const           mcastAddr,  /**< [in] Dotted-quad
                                              * representation of the multicast
                                              * group */
    const char* const           interface,  /**< [in] IPv4 address of interface
                                              *  on which to listen for
                                              *  multicast UDP packets in IPv4
                                              *  dotted-quad format or NULL to
                                              *  listen on all available
                                              *  interfaces */
    Fifo* const                 fifo,       /**< [in] Pointer to FIFO into
                                              *  which to put data */
    Reader** const              reader,     /**< [out] Pointer to pointer to
                                              *  address of reader */
    pthread_t* const            thread)     /**< [out] Pointer to pointer to
                                              * created thread */
{
    Reader*     multicastReader;
    int         status = multicastReaderNew(mcastAddr, interface, fifo,
                    &multicastReader);

    if (0 != status) {
        NPL_ADD0("Couldn't create multicast-reader");
    }
    else {
        pthread_t   thrd;

        if (0 != (status = pthread_create(&thrd, attr, readerStart,
                        multicastReader))) {
            NPL_ERRNUM0(status, "Couldn't start multicast-reader thread");
            status = 1;
        }
        else {
            *reader = multicastReader;
            *thread = thrd;
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
            tPrinted = 1;
        }

        nchar = snprintf(buf, size, "%fS", duration);
        buf += nchar;
        size -= nchar;
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

    nplStart("----------------------------------------");
    nplAdd("Ingestion Statistics:");
    nplAdd("    Since Previous Report (or Start):");
    interval = duration(&now, &reportTime);
    encodeDuration(buf, sizeof(buf), interval);
    nplAdd("        Duration          %s", buf);
    nplAdd("        Raw Data:");
    nplAdd("            Octets        %lu", CHAR_BIT*byteCount/8);
    nplAdd("            Mean Rate:");
    rate = (byteCount/interval)*(CHAR_BIT/8.0);
    nplAdd("                Octets    %g/s", rate);
    nplAdd("                Bits      %g/s", 8*rate);
    nplAdd("        Received packets:");
    nplAdd("            Number        %lu", packetCount);
    nplAdd("            Mean Rate     %g/s", packetCount/interval);
    nplAdd("        Missed packets:");
    nplAdd("            Number        %lu", missedPacketCount);
    nplAdd("            %%             %g",
            100.0 * missedPacketCount / (missedPacketCount + packetCount));
    nplAdd("        Products:");
    nplAdd("            Inserted      %lu", prodCount);
    nplAdd("            Mean Rate     %g/s", prodCount/interval);
    nplAdd("    Since Start:");
    interval = duration(&now, &startTime);
    encodeDuration(buf, sizeof(buf), interval);
    nplAdd("        Duration          %s", buf);
    nplAdd("        Raw Data:");
    nplAdd("            Octets        %lu", CHAR_BIT*totalByteCount/8);
    nplAdd("            Mean Rate:");
    rate = (totalByteCount/interval)*(CHAR_BIT/8.0);
    nplAdd("                Octets    %g/s", rate);
    nplAdd("                Bits      %g/s", 8*rate);
    nplAdd("        Received packets:");
    nplAdd("            Number        %lu", totalPacketCount);
    nplAdd("            Mean Rate     %g/s", totalPacketCount/interval);
    nplAdd("        Missed packets:");
    nplAdd("            Number        %lu", totalMissedPacketCount);
    nplAdd("            %%             %g", 100.0 * totalMissedPacketCount /
            (totalMissedPacketCount + totalPacketCount));
    nplAdd("        Products:");
    nplAdd("            Inserted      %lu", totalProdCount);
    nplAdd("            Mean Rate     %g/s", totalProdCount/interval);
    nplAdd("----------------------------------------");

    nplLog(LOG_NOTICE);
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
    const int           argc,           /**< [in] Number of arguments */
    char* const         argv[])         /**< [in] Arguments */
{
    int                 status = 0;     /* default success */
    extern int          optind;
    extern int          opterr;
    int                 ch;
    const char* const   progName = ubasename(argv[0]);
    const char*         interface = NULL;
    int                 logmask = LOG_UPTO(LOG_WARNING);
    const unsigned      logOptions = LOG_CONS | LOG_PID;
    const char*         mcastSpec = NULL;
    const char*         prodQueuePath = NULL;
    size_t              npages = DEFAULT_NPAGES;
    Fifo*               fifo;
    int                 ttyFd = open("/dev/tty", O_RDONLY);
    int                 processPriority = 0;
    const char*         logPath = (-1 == ttyFd)
        ? NULL                          /* log to system logging daemon */
        : "-";                          /* log to standard error stream */

    (void)close(ttyFd);
    (void)setulogmask(logmask);

    status = initLogging(progName, logOptions, logFacility, logPath);
    opterr = 0;                         /* no error messages from getopt(3) */

    while (0 == status && (ch = getopt(argc, argv, "b:I:l:m:np:q:u:vx")) != -1)
    {
        switch (ch) {
            extern char*    optarg;
            extern int      optopt;

            case 'b': {
                unsigned long   n;

                if (sscanf(optarg, "%lu", &n) != 1) {
                    NPL_SERROR1("Couldn't decode FIFO size in pages: \"%s\"",
                            optarg);
                    status = 1;
                }
                else {
                    npages = n;
                }
            }
            case 'I':
                interface = optarg;
                break;
            case 'l':
                logPath = optarg;
                status = initLogging(progName, logOptions, logFacility,
                        logPath);
                break;
            case 'm':
                mcastSpec = optarg;
                break;
            case 'n':
                logmask |= LOG_MASK(LOG_NOTICE);
                (void)setulogmask(logmask);
                break;
            case 'p': {
                char* cp;

                errno = 0;
                processPriority = (int)strtol(optarg, &cp, 0);

                if (0 != errno) {
                    NPL_SERROR1("Couldn't decode priority \"%s\"", optarg);
                    nplLog(LOG_ERR);
                }
                else {
                    if (processPriority < -20)
                        processPriority = -20;
                    else if (processPriority > 20)
                        processPriority = 20;
                }

                break;
            }
            case 'q':
                prodQueuePath = optarg;
                break;
            case 'u': {
                int         i = atoi(optarg);

                if (0 > i || 7 < i) {
                    NPL_START1("Invalid logging facility number: %d", i);
                    status = 1;
                }
                else {
                    static int  logFacilities[] = {LOG_LOCAL0, LOG_LOCAL1,
                        LOG_LOCAL2, LOG_LOCAL3, LOG_LOCAL4, LOG_LOCAL5,
                        LOG_LOCAL6, LOG_LOCAL7};

                    logFacility = logFacilities[i];

                    status = initLogging(progName, logOptions, logFacility,
                            logPath);
                }

                break;
            }
            case 'v':
                logmask |= LOG_MASK(LOG_INFO);
                (void)setulogmask(logmask);
                break;
            case 'x':
                logmask |= LOG_MASK(LOG_DEBUG);
                (void)setulogmask(logmask);
                break;
            default:
                optopt = ch;
                /*FALLTHROUGH*/
            case '?': {
                nplError("Unknown option: \"%c\"", optopt);
                status = 1;
            }
        }                               /* option character switch */
    }                                   /* getopt() loop */

    if (0 == status) {
        if (optind < argc) {
            nplError("Extraneous command-line argument: \"%s\"",
                    argv[optind]);
            status = 1;
        }
    }

    if (0 != status) {
        nplError("Error decoding command-line");
        usage(progName);
    }
    else {
        nplNotice("Starting Up %s", PACKAGE_VERSION);
        nplNotice("%s", COPYRIGHT_NOTICE);

        if ((status = fifoNew(npages, &fifo)) != 0) {
            NPL_ADD0("Couldn't create FIFO");
            nplLog(LOG_ERR);
        }
        else {
            LdmProductQueue*    prodQueue;

            if ((status = lpqGet(prodQueuePath, &prodQueue)) != 0) {
                NPL_ADD0("Couldn't open LDM product-queue");
                nplLog(LOG_ERR);
            }
            else {
                if (NULL == mcastSpec) {
                    if (0 == (status = spawnProductMaker(NULL, fifo, prodQueue,
                                    &productMaker, &productMakerThread))) {
                        status = spawnFileReader(NULL, NULL, fifo, &reader,
                                &readerThread);
                    }
                }                               /* reading file */
                else {
                    pthread_attr_t  attr;

                    if (0 != (status = pthread_attr_init(&attr))) {
                        NPL_ERRNUM0(status,
                                "Couldn't initialize thread attribute");
                    }
                    else {
#ifndef _POSIX_THREAD_PRIORITY_SCHEDULING
                        nplWarn("Can't adjust thread priorities due to lack of "
                                "necessary support from environment");
#else
                        /*
                         * In order to not miss any data, the reader thread
                         * should preempt the product-maker thread as soon as
                         * data is available and run as long as data is
                         * available.
                         */
                        const int           SCHED_POLICY = SCHED_FIFO;
                        struct sched_param  param;

                        param.sched_priority =
                            sched_get_priority_max(SCHED_POLICY) - 1;

                        (void)pthread_attr_setinheritsched(&attr,
                                PTHREAD_EXPLICIT_SCHED);
                        (void)pthread_attr_setschedpolicy(&attr, SCHED_POLICY);
                        (void)pthread_attr_setschedparam(&attr, &param);
                        (void)pthread_attr_setscope(&attr,
                                PTHREAD_SCOPE_SYSTEM);
#endif
                        if (0 == (status = spawnProductMaker(&attr, fifo,
                                        prodQueue, &productMaker,
                                        &productMakerThread))) {
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
                            param.sched_priority++;
                            (void)pthread_attr_setschedparam(&attr, &param);
#endif
                            status = spawnMulticastReader(&attr, mcastSpec,
                                    interface, fifo, &reader, &readerThread);
                        }                       /* product-maker spawned */
                    }                           /* "attr" initialized */
                }                               /* reading multicast packets */

                if (0 != status) {
                    nplLog(LOG_ERR);
                    status = 1;
                }
                else {
                    pthread_t   statThread;

                    (void)gettimeofday(&startTime, NULL);
                    reportTime = startTime;

                    (void)pthread_create(&statThread, NULL,
                            reportStatsWhenSignaled, NULL);

                    set_sigactions();

                    (void)pthread_join(readerThread, NULL);

                    status = readerStatus(reader);

                    (void)pthread_cancel(statThread);
                    (void)pthread_join(statThread, NULL);
                    (void)fifoCloseWhenEmpty(fifo);
                    (void)pthread_join(productMakerThread, NULL);

                    if (0 != status)
                        status = pmStatus(productMaker);

                    reportStats();
                    readerFree(reader);
                }               /* "reader" spawned */

                (void)lpqClose(prodQueue);
            }                       /* "prodQueue" open */
        }                           /* "fifo" created */
    }                               /* command line decoded */

    return status;
}
