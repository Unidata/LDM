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

#include "log.h"
#include "fifo.h"
#include "fileReader.h"
#include "getFacilityName.h"
#include "globals.h"
#include "ldmProductQueue.h"
#include "multicastReader.h"
#include "productMaker.h"
#include "reader.h"

#include <ldm.h>
#include <ulog.h>

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
#if OLD_STYLE
    static pthread_t         readerThread;
    static pthread_t         productMakerThread;
#endif
static ProductMaker*     productMaker;
static unsigned          logFacility = LOG_LDM;  /* LDM facility */
static const char* const COPYRIGHT_NOTICE = \
    "Copyright (C) 2014 University Corporation for Atmospheric Research";
static struct timeval    startTime;      /**< Start of execution */
static struct timeval    reportTime;     /**< Time of last report */
static int               reportStatistics;
static pthread_mutex_t   mutex;
static pthread_cond_t    cond = PTHREAD_COND_INITIALIZER;

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
        long)npages, lpqGetQueuePath(), getFacilityName(logFacility));

    (void)setulogmask(logmask);
}

/**
 * Initializes logging. Uses `logFacility`.
 *
 * @retval 0    Success
 * @retval 1    Usage error.
 */
static int initLogging(
    const char* const   progName,       /**< [in] Name of the program */
    const unsigned      logOptions,     /**< [in] Logging options */
    const char* const   logPath)        /**< [in] Pathname of the log file,
                                          *  "-", or NULL */
{
    int status;

    if (openulog(progName, logOptions, logFacility, logPath) == -1) {
        LOG_SERROR0("Couldn't initialize logging");
        log_log(LOG_ERR);
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
 * Creates a product-maker and starts it in a new thread.
 *
 * @retval 0               Success.
 * @retval USAGE_ERROR     Usage error. `log_start()` called.
 * @retval SYSTEM_FAILURE  System failure. `log_start()` called.
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
        LOG_ADD0("Couldn't create new LDM product-maker");
    }
    else {
        pthread_t   thrd;

        status = pthread_create(&thrd, attr, pmStart, pm);

        if (status) {
            LOG_ERRNUM0(status, "Couldn't start product-maker thread");
            status = SYSTEM_FAILURE;
        }
        else {
            *productMaker = pm;
            *thread = thrd;
        }
    }

    return status;
}

#if OLD_STYLE
/**
 * Creates a file data-reader and starts it in a new thread.
 *
 * @retval 0    Success
 * @retval 1    Usage failure. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
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
        LOG_ADD0("Couldn't create file-reader");
    }
    else {
        pthread_t   thrd;

        if ((status = pthread_create(&thrd, attr, readerStart, fileReader)) !=
                0) {
            LOG_ERRNUM0(status, "Couldn't start file-reader thread");
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
 * @retval 1    Usage failure. \c log_start() called.
 * @retval 2    O/S failure. \c log_start() called.
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
        LOG_ADD0("Couldn't create multicast-reader");
    }
    else {
        pthread_t   thrd;

        if (0 != (status = pthread_create(&thrd, attr, readerStart,
                        multicastReader))) {
            LOG_ERRNUM0(status, "Couldn't start multicast-reader thread");
            status = 1;
        }
        else {
            *reader = multicastReader;
            *thread = thrd;
        }
    }

    return status;
}
#endif

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
 * input is read from the standard input stream. Sets `reader`. Reports
 * statistics before returning.
 *
 * @retval    0               Success. `done` was set.
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

        reportStats(); // requires `reader`
        readerFree(reader);
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
         * are received on this, possibly higher-priority, input thread.
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
                    LOG_ADD0("Couldn't read input");
            }

            fifo_close(fifo); // shouldn't hurt
            (void)pthread_join(productMakerThread, NULL);
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
    const int           argc,           /**< [in] Number of arguments */
    char* const         argv[])         /**< [in] Arguments */
{
    int                 status = 0;     /* default success */
    extern int          optind;
    extern int          opterr;
    int                 ch;
    const char* const   progName = ubasename(argv[0]);
    int                 logmask = LOG_UPTO(LOG_WARNING);
    const unsigned      logOptions = LOG_CONS | LOG_PID;
    int                 ttyFd = open("/dev/tty", O_RDONLY);
    int                 processPriority = 0;
    const char*         logPath = (-1 == ttyFd)
        ? NULL                          /* log to system logging daemon */
        : "-";                          /* log to standard error stream */

    (void)close(ttyFd);
    (void)setulogmask(logmask);

    status = initLogging(progName, logOptions, logPath);
    opterr = 0;                         /* no error messages from getopt(3) */

    while (0 == status && (ch = getopt(argc, argv, "b:I:l:m:np:q:r:s:t:u:vx")) != -1)
    {
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
                logPath = optarg;
                status = initLogging(progName, logOptions, logPath);
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
                    LOG_SERROR1("Couldn't decode priority \"%s\"", optarg);
                    log_log(LOG_ERR);
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
                     uerror("No other mechanism other than MHS is currently supported\n");
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

                    logFacility = logFacilities[i];

                    status = initLogging(progName, logOptions, logPath);
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
                /* no break */
            case '?': {
                uerror("Unknown option: \"%c\"", optopt);
                status = 1;
                break;
            }
        }                               /* option character switch */
    }                                   /* getopt() loop */

    if (0 == status) {
        if (optind < argc) {
            uerror("Extraneous command-line argument: \"%s\"",
                    argv[optind]);
            status = 1;
        }
    }

    if (0 != status) {
        uerror("Error decoding command-line");
        usage(progName);
    }
    else {
        unotice("Starting Up %s", PACKAGE_VERSION);
        unotice("%s", COPYRIGHT_NOTICE);

        status = execute();

#if OLD_STYLE
        if ((status = fifo_new(npages, &fifo)) != 0) {
            LOG_ADD0("Couldn't create FIFO");
            log_log(LOG_ERR);
        }
        else {
            LdmProductQueue*    prodQueue;

            if ((status = lpqGet(prodQueuePath, &prodQueue)) != 0) {
                LOG_ADD0("Couldn't open LDM product-queue");
                log_log(LOG_ERR);
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
                        LOG_ERRNUM0(status,
                                "Couldn't initialize thread attribute");
                    }
                    else {
#ifndef _POSIX_THREAD_PRIORITY_SCHEDULING
                        uwarn("Can't adjust thread priorities due to lack of "
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
#ifdef RETRANS_SUPPORT
                        if (retrans_xmit_enable == OPTION_ENABLE){
                            /* Copy mcastAddress needed to obtain the cpio entries */
                            strcpy(mcastAddr, mcastSpec);
                        }
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
                    log_log(LOG_ERR);
                    status = 1;
                }
                else {
                    pthread_t   statThread;

                    (void)gettimeofday(&startTime, NULL);
                    reportTime = startTime;

                    (void)pthread_create(&statThread, NULL,
                            reportStatsWhenSignaled, NULL);

                    set_sigactions();

                    void* statusPtr;
                    (void)pthread_join(readerThread, &statusPtr);
                    status = done ? 0 : *(int*)statusPtr;

                    fifo_close(fifo); // terminates `productMakerThread`
                    (void)pthread_join(productMakerThread, NULL);

                    (void)pthread_cancel(statThread);
                    (void)pthread_join(statThread, NULL);

                    if (0 != status)
                        status = done ? 0 : pmStatus(productMaker);

                    reportStats();
                    readerFree(reader);
#ifdef RETRANS_SUPPORT
                    /** Release buffer allocated for retransmission **/
                    if(retrans_xmit_enable == OPTION_ENABLE){
                        freeRetransMem();
                    }
#endif
                }               /* "reader" spawned */

                (void)lpqClose(prodQueue);
            }                       /* "prodQueue" open */
        }                           /* "fifo" created */
#endif
    }                               /* command line decoded */

    return status;
}
