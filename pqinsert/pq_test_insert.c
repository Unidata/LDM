/**
 * Copyright 2015, University Corporation for Atmospheric Research
 * See the file COPYRIGHT in the top-level source-directory for copying and
 * redistribution conditions.
 *
 * @author Steven R. Emmerson
 * @file   Inserts synthetic data-products into a product-queue
 */

#include <config.h>

#include "atofeedt.h"
#include "globals.h"
#include "inetutil.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "pq.h"
#include "remote.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <math.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static char        myname[HOSTNAMESIZE];
static const char* progname;
static feedtypet   feedtype = EXP;
static product     prod;
static int         seq_start = 0;

static bool pti_decodeCommandLine(
        int                ac,
        char*              av[],
        const char** const inputPathname)
{
    extern int   optind;
    extern int   opterr;
    extern char* optarg;
    int          ch;
    feedtypet    ft = feedtype;
    int          seq = seq_start;
    const char*  pqPathname = getQueuePath();
    bool         success = true;

    // Error messages are being explicitly handled
    opterr = 0;

    while ((ch = getopt(ac, av, ":f:l:q:s:vx:")) != EOF) {
        switch (ch) {
        case 'f':
            ft = atofeedtypet(optarg);
            if (ft == NONE) {
                LOG_ADD1("Unknown feedtype \"%s\"", optarg);
                success = false;
            }
            break;
        case 'l':
            openulog(progname, ulog_get_options(), LOG_LDM, optarg);
            break;
        case 'q':
            pqPathname = optarg;
            break;
        case 's':
            seq = atoi(optarg);
            break;
        case 'v':
            (void)setulogmask(getulogmask() | LOG_MASK(LOG_INFO));
            break;
        case 'x':
            (void)setulogmask(getulogmask() | LOG_MASK(LOG_DEBUG));
            break;
        case ':': {
            LOG_ADD1("Option \"-%c\" requires an operand", optopt);
            success = false;
            break;
        }
        default:
            LOG_ADD1("Unknown option: \"%c\"", optopt);
            success = false;
        }
    }

    if (success) {
        ac -= optind;
        av += optind ;

        if(ac != 1) {
            LOG_ADD0("Invalid number of operands");
            success = false;
        }
        else {
            prod.info.feedtype = feedtype = ft;
            prod.info.seqno = seq_start = seq;
            setQueuePath(pqPathname);
            *inputPathname = *av;
        }
    }

    return success;
}

static void pti_usage(void)
{
    char        feedbuf[256];
    const char* pqPath = getQueuePath();

    (void)ft_format(feedtype, feedbuf, sizeof(feedbuf));
    log_add(
"Usage: %s [options] file\n"
"Options:\n"
"    -f feedtype   Use <feedtype> as data-product feed-type. Default is %s.\n"
"    -l logfile    Log to <logfile> (\"-\" means standard error stream).\n"
"                  Default depends on standard error stream:\n"
"                      is tty     => use standard error stream\n"
"                      is not tty => use system logging daemon.\n"
"    -q queue      Use <queue> as product-queue. Default is \"%s\".\n"
"    -s seqno      Set initial product sequence number to <seqno>. Default is\n"
"                  %d.\n"
"    -v            Verbose logging level: log each product.\n"
"    -x            Debug logging level.\n"
"Operands:\n"
"    file          Pathname of file containing size and timestamp entries.",
            progname, feedbuf, pqPath, seq_start);
    log_log(LOG_ERR);
}

static void cleanup(void)
{
    if (pq) {
        (void) pq_close(pq);
        pq = NULL;
    }

    (void)closeulog();
}

static void
pti_setSig(
        signaturet* const sig)
{
    for (int i = 0; i < sizeof(signaturet)/4; i++)
        ((int32_t*)sig)[i] = random();
}

/**
 * Initializes this module:
 *     - Registers the function `cleanup()` to be run at process termination;
 *     - Opens the product-queue named by `getQueuePath()`;
 *     - Redirects the standard input stream to the given input file;
 *     - Initializes the structure `prod`; and
 *     - Initializes the PRNG module associated with the function `random()`.
 *
 * @param[in] inputPathname  The pathname of the input file containing the size
 *                           and timestamp fields.
 * @retval    true           if and only if successful.
 */
static bool pti_init(
        const char* const inputPathname)
{
    int success = false;

    if (atexit(cleanup) != 0) {
        LOG_SERROR0("Couldn't register exit handler");
    }
    else {
        const char* const pqfname = getQueuePath();
        int status = pq_open(pqfname, PQ_DEFAULT, &pq);
        if (PQ_CORRUPT == status) {
            LOG_ADD1("The product-queue \"%s\" is corrupt\n", pqfname);
        }
        else if (status) {
            LOG_ERRNUM1(status, "Couldn't open product-queue \"%s\"", pqfname);
        }
        else if (freopen(inputPathname, "r", stdin) == NULL) {
            LOG_SERROR1("Couldn't open input-file \"%s\"", inputPathname);
        }
        else {
            (void)strncpy(myname, ghostname(), sizeof(myname));
            myname[sizeof(myname)-1] = 0;
            prod.info.origin = myname;
            prod.data = malloc(20000000);
            srandom(1);
            success = true;
        }
    }

    return success;
}

/**
 * Reads data-product size and creation-time from the standard input stream.
 *
 * @retval 1   EOF.
 * @retval 0   Success.
 * @retval -1  Error. `log_add()` called.
 */
static int pti_decodeInputLine(
        const unsigned long           lineNo,
        unsigned* const restrict      size,
        struct tm* const restrict     tm,
        unsigned long* const restrict nanoSec)
{
    int           status;
    double        seconds;

    (void)memset(tm, 0, sizeof(*tm));
    status = scanf("%u %4u%2u%2u%2u%2u%lf \n", &prod.info.sz, &tm->tm_year,
            &tm->tm_mon, &tm->tm_mday, &tm->tm_hour, &tm->tm_min, &seconds);
    if (status == EOF) {
        if (ferror(stdin)) {
            LOG_ADD1("Couldn't read line %lu (origin 1) from input-file",
                    lineNo);
            status = -1;
        }
        else {
            status = 1;
        }
    }
    else if (status != 7) {
        LOG_ADD1("Couldn't decode line %lu (origin 1) in input-file", lineNo);
        status = -1;
    }
    else if (seconds < 0 || seconds > 60) {
        LOG_ADD1("Invalid number of seconds in line %lu", lineNo);
        status = -1;
    }
    else {
        *nanoSec = modf(seconds, &seconds) * 1000000000;
        tm->tm_sec = seconds;
        status = 0;
    }

    return status;
}

static bool pti_setCreationTime(
        const unsigned long lineNo,
        struct tm* const    tm,
        const unsigned long ns)
{
    struct timeval        creationTime;
    struct timeval        interCreationInterval;
    struct timeval        returnTime;
    struct timeval        now;
    struct timespec       sleepInterval;
    bool                  success = true;
    static struct timeval prevReturnTime;
    static struct timeval prevCreationTime;

    // Set the current creation-time
    creationTime.tv_sec = mktime(tm);
    creationTime.tv_usec = ns/1000;

    if (lineNo == 1) {
        prevReturnTime.tv_sec = prevReturnTime.tv_usec = 0;
        prevCreationTime = creationTime;
    }

    // Compute the inter-product creation interval
    interCreationInterval.tv_sec = creationTime.tv_sec - prevCreationTime.tv_sec;
    interCreationInterval.tv_usec = creationTime.tv_usec - prevCreationTime.tv_usec;
    if (interCreationInterval.tv_usec < 0) {
        interCreationInterval.tv_sec -= 1;
        interCreationInterval.tv_usec += 1000000;
    }

    /*
     * Compute when this function should return based on the previous return
     * time and the inter-product creation interval.
     */
    returnTime.tv_sec = prevReturnTime.tv_sec + interCreationInterval.tv_sec;
    returnTime.tv_usec = prevReturnTime.tv_usec + interCreationInterval.tv_usec;
    if (returnTime.tv_usec > 1000000) {
        returnTime.tv_sec += 1;
        returnTime.tv_usec -= 1000000;
    }

    // Compute how long to sleep based on the return time and the current time.
    (void)set_timestamp(&now);
    sleepInterval.tv_sec = returnTime.tv_sec - now.tv_sec;
    sleepInterval.tv_nsec = (returnTime.tv_usec - now.tv_usec) * 1000;
    if (sleepInterval.tv_nsec < 0) {
        sleepInterval.tv_sec -= 1;
        sleepInterval.tv_nsec += 1000000000;
    }

    // Sleep if necessary
    if (sleepInterval.tv_sec >= 0) {
        if (nanosleep(&sleepInterval, NULL)) {
            LOG_SERROR0("Couldn't sleep");
            success = false;
        }
    }

    // Set the product's creation-time and save values for next time
    if (success) {
        prevCreationTime = creationTime;
        (void)set_timestamp(&prod.info.arrival);
        prevReturnTime = prod.info.arrival;
    }

    return success;
}

/**
 * Reads data-product sizes and creation-times from the standard input stream.
 *
 * @retval true  if and only if success.
 */
static bool pti_execute()
{
    bool          success;
    struct tm     tm;
    unsigned long lineNo;
    char          id[KEYSIZE];
    char          feedStr[128];

    prod.info.ident = id;
    tm.tm_isdst = 0;

    (void)ft_format(feedtype, feedStr, sizeof(feedStr));
    unotice("Starting up: feedtype=%s, seq_start=%d", feedStr, seq_start);

    for (lineNo = 1, success = true; success; prod.info.seqno++, lineNo++) {
        unsigned long ns;
        int           status = pti_decodeInputLine(lineNo, &prod.info.sz, &tm,
                &ns);

        if (status) {
            success = status == 1; // EOF
            break;
        }

        (void)snprintf(id, sizeof(id), "%u", prod.info.seqno);
        prod.data = realloc(prod.data, prod.info.sz);
        if (prod.data == NULL) {
            LOG_SERROR0("Couldn't allocate memory for data-product");
            success = false;
            break;
        }

        pti_setSig(&prod.info.signature);

        success = pti_setCreationTime(lineNo, &tm, ns);
        if (!success)
            break;

        status = pq_insert(pq, &prod);

        switch (status) {
        case ENOERR:
            if (ulogIsVerbose())
                uinfo("%s", s_prod_info(NULL, 0, &prod.info, 1)) ;
            log_clear(); // just in case
            break;
        case PQUEUE_DUP:
            LOG_ADD1("Product already in queue: %s",
                s_prod_info(NULL, 0, &prod.info, 1));
            success = false;
            break;
        case PQUEUE_BIG:
            LOG_ADD1("Product too big for queue: %s",
                s_prod_info(NULL, 0, &prod.info, 1));
            success = false;
            break;
        case ENOMEM:
            LOG_ERRNUM0(status, "Queue full?");
            success = false;
            break;
        case EINTR:
        case EDEADLK:
            /* TODO: retry ? */
            /*FALLTHROUGH*/
        default:
            LOG_ADD1("pq_insert: %s", status > 0
                ? strerror(status) : "Internal error");
            success = false;
        }
    }                               /* input-file loop */

    free(prod.data);

    return success;
}

static bool pti_initAndExecute(
        const char* const inputPathname)
{
    bool success = pti_init(inputPathname);

    if (!success) {
        LOG_ADD0("Couldn't initialize program");
    }
    else {
        success = pti_execute();

        if (!success)
            LOG_ADD0("Failure executing program");
    }

    return success;
}

int main(
        int   ac,
        char* av[])
{
    // Done first in case something happens that needs to be reported.
   progname = basename(av[0]);
   log_initLogging(progname, LOG_NOTICE, LOG_LDM);

    const char* inputPathname;
    int         status = EXIT_FAILURE;
    if (!pti_decodeCommandLine(ac, av, &inputPathname)) {
        log_add("Couldn't decode command-line");
        log_log(LOG_ERR);
        pti_usage();
    }
    else {
        if (!pti_initAndExecute(inputPathname)) {
            log_log(LOG_ERR);
        }
        else {
            status = EXIT_SUCCESS;
        }
    }

    return status;
}
