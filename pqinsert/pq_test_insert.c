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

/**
 * Redirects the standard input stream to the given input file.
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
            (void)memset(prod.info.signature, 0, sizeof(prod.info.signature));
            prod.data = malloc(20000000);
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
    unsigned long ns;
    int           status;

    (void)memset(tm, 0, sizeof(*tm));
    status = scanf("%u %4u%2u%2u%2u%2u%2u.%9lu \n", &prod.info.sz, &tm->tm_year,
            &tm->tm_mon, &tm->tm_mday, &tm->tm_hour, &tm->tm_min, &tm->tm_sec,
            &ns);
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
    else if (status != 8) {
        LOG_ADD1("Couldn't decode line %lu (origin 1) in input-file", lineNo);
        status = -1;
    }
    else {
        if (ns < 1000) {
            ns *= 1000000;
        }
        else if (ns < 1000000) {
            ns *= 1000000000;
        }
        *nanoSec = ns;
        status = 0;
    }

    return status;
}

static bool pti_setCreationTime(
        const unsigned long lineNo,
        struct tm* const    tm,
        const unsigned long ns)
{
    bool                   success = true;
    static struct timespec prevTime;

    if (lineNo == 1) {
        prevTime.tv_sec = mktime(tm);
        prevTime.tv_nsec = ns;
    }

    struct timespec createTime;
    createTime.tv_sec = mktime(tm);
    createTime.tv_nsec = ns;

    struct timespec sleepAmount;
    sleepAmount.tv_sec = createTime.tv_sec - prevTime.tv_sec;
    sleepAmount.tv_nsec = createTime.tv_nsec - prevTime.tv_nsec;
    if (sleepAmount.tv_nsec < 0) {
        sleepAmount.tv_sec -= 1;
        sleepAmount.tv_nsec += 1000000000;
    }

    if (sleepAmount.tv_sec >= 0) {
        if (nanosleep(&sleepAmount, NULL)) {
            LOG_SERROR0("Couldn't sleep");
            success = false;
        }
    }

    if (success) {
        (void)set_timestamp(&prod.info.arrival);
        prevTime = createTime;
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

        success = pti_setCreationTime(lineNo, &tm, ns);
        if (!success)
            break;

        size_t nbytes = sizeof(prod.info.seqno);
        (void)memcpy(prod.info.signature+sizeof(prod.info.signature)-nbytes,
            &prod.info.seqno, nbytes);

        status = pq_insert(pq, &prod);

        switch (status) {
        case ENOERR:
            if(ulogIsVerbose())
                uinfo("%s", s_prod_info(NULL, 0, &prod.info, 1)) ;
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
