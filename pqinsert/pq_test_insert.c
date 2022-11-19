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
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>
#include <netinet/in.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static bool           have_input_file;
static const char*    inputPathname;
static char           myname[HOSTNAMESIZE];
static const char*    progname;
static feedtypet      feedtype = EXP;
static product        prod;
static unsigned       seq_start = 0;
static const long     ONE_MILLION = 1000000;
static unsigned long  max_prod_size = 200000;
static unsigned long  num_prods = 50000;
static unsigned long  inter_prod_gap = 100000000; // 0.1 s
static unsigned short seed[3];

static bool pti_decodeCommandLine(
        int                ac,
        char*              av[])
{
    extern int   optind;
    extern int   opterr;
    extern char* optarg;
    int          ch;
    bool         success = true;

    // Error messages are being explicitly handled
    opterr = 0;

    while ((ch = getopt(ac, av, ":f:g:l:m:n:q:s:vx")) != EOF) {
        switch (ch) {
        case 'f':
            if (strfeedtypet(optarg, &feedtype)) {
                log_add("Unknown feedtype \"%s\"", optarg);
                success = false;
            }
            break;
        case 'g':
            if (sscanf(optarg, "%lu", &inter_prod_gap) != 1) {
                log_add("Invalid inter-product gap duration: \"%s\"", optarg);
                success = false;
            }
            break;
        case 'l':
            if (log_set_destination(optarg)) {
                log_syserr("Couldn't set logging destination to \"%s\"",
                        optarg);
                success = false;
            }
            break;
        case 'm':
            if (sscanf(optarg, "%lu", &max_prod_size) != 1) {
                log_add("Invalid maximum product size: \"%s\"", optarg);
                success = false;
            }
            break;
        case 'n':
            if (sscanf(optarg, "%lu", &num_prods) != 1) {
                log_add("Invalid number of products: \"%s\"", optarg);
                success = false;
            }
            break;
        case 'q':
            setQueuePath(optarg);
            break;
        case 's':
            if (sscanf(optarg, "%u", &seq_start) != 1) {
                log_add("Invalid beginning sequence-number \"%s\"", optarg);
                success = false;
            }
            break;
        case 'v':
            if (!log_is_enabled_info)
                (void)log_set_level(LOG_LEVEL_INFO);
            break;
        case 'x':
            (void)log_set_level(LOG_LEVEL_DEBUG);
            break;
        case ':': {
            log_add("Option \"-%c\" requires an operand", optopt);
            success = false;
            break;
        }
        default:
            log_add("Unknown option: \"%c\"", optopt);
            success = false;
        }
    }

    if (success) {
        ac -= optind;
        av += optind ;

        if (ac == 0) {
            have_input_file = false;
        }
        else if (ac != 1) {
            log_add("Invalid number of operands");
            success = false;
        }
        else {
            inputPathname = *av;
            have_input_file = true;
        }
    }

    return success;
}

static void pti_usage(void)
{
    char        feedbuf[256];
    const char* pqPath = getDefaultQueuePath();

    (void)ft_format(feedtype, feedbuf, sizeof(feedbuf));
    log_error_q(
"Usage: %s [options] [file]\n"
"Options:\n"
"    -f feedtype   Use <feedtype> as data-product feed-type. Default is %s.\n"
"    -g sleep      Sleep <sleep> nanoseconds between inserting products.\n"
"                  Ignored if <file> given. Default is %lu\n"
"    -l dest       Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"                  (standard error), or file `dest`. Default is \"%s\"\n"
"    -m max_size   Maximum product size in bytes. Ignored if <file> given.\n"
"                  Default is %lu.\n"
"    -n num_prods  Number of products. Ignored if <file> given. Default is\n"
"                  %lu.\n"
"    -q queue      Use <queue> as product-queue. Default is \"%s\".\n"
"    -s seqno      Set initial product sequence number to <seqno>. Default is\n"
"                  %d.\n"
"    -v            Verbose logging level: log each product.\n"
"    -x            Debug logging level.\n"
"Operands:\n"
"    file          Pathname of file containing size and timestamp entries.\n"
"                  If given, then '-g', '-m', and '-n' options are ignored",
            progname, feedbuf, inter_prod_gap, log_get_default_destination(),
            max_prod_size, num_prods, pqPath, seq_start);
}

static void
pti_setSig(
        uint8_t* const sig,
        unsigned i)
{
    for (int j = sizeof(signaturet); --j >= 0;) {
        sig[j] = (i & 0xFF);
        i >>= 8;
    }
}

/**
 * Initializes this module:
 *     - Opens the product-queue named by `getQueuePath()`;
 *     - Redirects the standard input stream to the given input file;
 *     - Initializes the structure `prod`; and
 *     - Initializes the PRNG module associated with the function `random()`.
 *
 * @retval    true           if and only if successful.
 */
static bool pti_init(void)
{
    int               success = false;
    const char* const pqfname = getQueuePath();
    int               status = pq_open(pqfname, PQ_DEFAULT, &pq);
    if (PQ_CORRUPT == status) {
        log_add("The product-queue \"%s\" is corrupt\n", pqfname);
    }
    else if (status) {
        log_add_errno(status, "Couldn't open product-queue \"%s\"", pqfname);
        log_flush_error();
    }
    else {
        prod.data = malloc(max_prod_size);
        if (prod.data == NULL) {
            log_syserr("Couldn't allocate buffer for data-product");
        }
        else {
            (void)strncpy(myname, ghostname(), sizeof(myname));
            myname[sizeof(myname)-1] = 0;
            prod.info.origin = myname;
            prod.info.feedtype = feedtype;
            srandom(1); // for `random()`
            seed[2] = random(); seed[1] = random(); seed[0] = random();
            (void)seed48(seed); // for `mrand48()`
            success = true;
        }
    }

    return success;
}

static void pti_fini(void)
{
    if (pq) {
        (void)pq_close(pq);
        pq = NULL;
    }
    if (prod.data) {
        free(prod.data);
        prod.data = NULL;
    }
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
            log_add("Couldn't read line %lu (origin 1) from input-file",
                    lineNo);
            status = -1;
        }
        else {
            status = 1;
        }
    }
    else if (status != 7) {
        log_add("Couldn't decode line %lu (origin 1) in input-file", lineNo);
        status = -1;
    }
    else if (seconds < 0 || seconds > 60) {
        log_add("Invalid number of seconds in line %lu", lineNo);
        status = -1;
    }
    else {
        *nanoSec = modf(seconds, &seconds) * 1000000000;
        tm->tm_sec = seconds;
        status = 0;
    }

    return status;
}

/**
 * Returns the difference between two `struct timeval`s.
 *
 * @param[out] result  The difference `left - right`.
 * @param[in]  left    The left operand.
 * @param[in]  right   The right operand.
 */
static inline void timeval_diff(
        struct timeval* const restrict       result,
        const struct timeval* const restrict left,
        const struct timeval* const restrict right)
{
    result->tv_sec = left->tv_sec - right->tv_sec;
    result->tv_usec = left->tv_usec - right->tv_usec;
    if (result->tv_usec < 0) {
        result->tv_usec += ONE_MILLION;
        result->tv_sec -= 1;
    }
}

/**
 * Returns the sum of two `struct timeval`s.
 *
 * @param[out] result  The sum `left + right`.
 * @param[in]  left    The left operand.
 * @param[in]  right   The right operand.
 */
static inline void timeval_sum(
        struct timeval* const restrict       result,
        const struct timeval* const restrict left,
        const struct timeval* const restrict right)
{
    result->tv_sec = left->tv_sec + right->tv_sec;
    result->tv_usec = left->tv_usec + right->tv_usec;
    if (result->tv_usec > ONE_MILLION) {
        result->tv_usec -= ONE_MILLION;
        result->tv_sec += 1;
    }
}

/**
 * Indicates if a `struct timeval` contains a positive value or not.
 *
 * @param[in] time   The time to examine.
 * @retval    true   `time` is positive.
 * @retval    false  `time` is zero or negative.
 */
static inline bool timeval_isPositive(
        struct timeval* const time)
{
    return time->tv_sec > 0 || (time->tv_sec == 0 && time->tv_usec > 0);
}

/**
 * Sets the creation-time of the next data-product and returns at that time.
 *
 * @param[in] init    Whether or not to initialize this function.
 * @param[in] tm      Input creation-time.
 * @param[in] ns      Nanosecond component of input creation-time.
 * @return    true    Success. `prod.info.arrival` is set.
 * @return    false   Failure. `log_add()` called.
 */
static bool pti_setCreationTime(
        const bool          init,
        struct tm* const    tm,
        const unsigned long ns)
{
    struct timeval        creationTime;
    struct timeval        creationInterval;
    struct timeval        returnTime;
    static struct timeval prevReturnTime;
    static struct timeval prevCreationTime;

    // Set the input creation-time
    creationTime.tv_sec = mktime(tm);
    creationTime.tv_usec = ns/1000;

    if (init) {
        prevReturnTime.tv_sec = prevReturnTime.tv_usec = 0;
        prevCreationTime = creationTime;
    }

    // Compute the time-interval since the previous input creation-time.
    timeval_diff(&creationInterval, &creationTime, &prevCreationTime);

    if (!timeval_isPositive(&creationInterval)) {
        (void)gettimeofday(&returnTime, NULL);
    }
    else {
        struct timeval now;
        struct timeval sleepInterval;

        /*
         * Compute when this function should return based on the previous return
         * time and the interval since the previous input creation-time.
         */
        timeval_sum(&returnTime, &prevReturnTime, &creationInterval);

        /*
         * Compute how long to sleep based on the return time and the current
         * time.
         */
        (void)gettimeofday(&now, NULL);
        timeval_diff(&sleepInterval, &returnTime, &now);

        // Sleep if necessary
        if (timeval_isPositive(&sleepInterval)) {
            struct timespec sleepTime = {.tv_sec=sleepInterval.tv_sec,
                    .tv_nsec=sleepInterval.tv_usec*1000};
            if (nanosleep(&sleepTime, NULL)) {
                log_syserr("Couldn't sleep");
                return false;
            }
        }
    }

    // Set the product's creation-time and save values for next time
    prod.info.arrival.tv_sec = returnTime.tv_sec;
    prod.info.arrival.tv_usec = returnTime.tv_usec;
    prevCreationTime = creationTime;
    prevReturnTime = returnTime;

    return true;
}

/**
 * Reads data-product sizes and creation-times from the standard input stream.
 *
 * @retval true  if and only if success.
 */
static bool pti_process_input_file()
{
    bool success;

    if (freopen(inputPathname, "r", stdin) == NULL) {
        log_syserr("Couldn't open input-file \"%s\"", inputPathname);
        success = false;
    }
    else {
        struct tm     tm;
        unsigned long lineNo;
        char          id[KEYSIZE];
        char          feedStr[128];

        prod.info.ident = id;
        prod.info.seqno = seq_start;
        tm.tm_isdst = 0;

        (void)ft_format(feedtype, feedStr, sizeof(feedStr));
        log_notice_q("Starting up: feedtype=%s, seq_start=%u", feedStr,
                seq_start);

        for (lineNo = 1, success = true; success; prod.info.seqno++, lineNo++) {
            unsigned long ns;
            int           status = pti_decodeInputLine(lineNo, &prod.info.sz,
                    &tm, &ns);

            if (status) {
                success = status == 1; // EOF
                break;
            }

            (void)snprintf(id, sizeof(id), "%u", prod.info.seqno);
            prod.data = realloc(prod.data, prod.info.sz);
            if (prod.data == NULL) {
                log_syserr("Couldn't allocate memory for data-product");
                success = false;
                break;
            }
            (void)memset(prod.data, 0xbd, prod.info.sz);

            pti_setSig(prod.info.signature, prod.info.seqno);

            success = pti_setCreationTime(lineNo == 1, &tm, ns);
            if (!success)
                break;

            status = pq_insert(pq, &prod);

            switch (status) {
            case ENOERR:
                if (log_is_enabled_info)
                    log_info_q("%s", s_prod_info(NULL, 0, &prod.info, 1)) ;
                log_clear(); // just in case
                break;
            case PQUEUE_DUP:
                log_add("Product already in queue: %s",
                    s_prod_info(NULL, 0, &prod.info, 1));
                success = false;
                break;
            case PQUEUE_BIG:
                log_add("Product too big for queue: %s",
                    s_prod_info(NULL, 0, &prod.info, 1));
                success = false;
                break;
            case ENOMEM:
                log_add_errno(status, "Queue full?");
                log_flush_error();
                success = false;
                break;
            case EINTR:
            case EDEADLK:
                /* TODO: retry ? */
                /*FALLTHROUGH*/
            default:
                log_add("pq_insert: %s", status > 0
                    ? strerror(status) : "Internal error");
                success = false;
            }
        }                               /* input-file loop */
    }

    return success;
}

static bool pti_generate_products(void)
{
    int            status = 0;
    prod_info*     info = &prod.info;
    char           ident[80];

    info->ident = ident;
    (void)memset(info->signature, 0, sizeof(info->signature));

    for (unsigned i = seq_start; i != seq_start + num_prods; ++i) {
        const unsigned long size = max_prod_size*drand48() + 0.5;
        const ssize_t       nbytes = snprintf(ident, sizeof(ident), "%u", i);
        log_assert(nbytes >= 0 && nbytes < sizeof(ident));
        info->seqno = i;
        pti_setSig(info->signature, i);
        info->sz = size;
        status = set_timestamp(&info->arrival);
        log_assert(status == 0);

        char buf[LDM_INFO_MAX];
        status = pq_insert(pq, &prod);
        if (status) {
            log_add("pq_insert() failure: prodInfo=\"%s\"",
                    s_prod_info(buf, sizeof(buf), info, 1));
            break;
        }
        log_info_q("Inserted: prodInfo=\"%s\"",
                s_prod_info(buf, sizeof(buf), info, 1));

        if (inter_prod_gap) {
            struct timespec duration;
            duration.tv_sec = 0;
            duration.tv_nsec = inter_prod_gap;
            int status = nanosleep(&duration, NULL);
            log_assert(status == 0 || errno == EINTR);
        }
    }

    return status == 0;
}

static bool pti_initAndExecute(void)
{
    bool success = pti_init();

    if (!success) {
        log_add("Couldn't initialize program");
    }
    else {
        success = have_input_file
                ? pti_process_input_file()
                : pti_generate_products();

        if (!success)
            log_add("Failure executing program");

        pti_fini();
    }

    return success;
}

int main(
        int   ac,
        char* av[])
{
    int         status = EXIT_FAILURE;

    // Done first in case something happens that needs to be reported.
   progname = basename(av[0]);

    if (log_init(av[0])) {
        log_syserr("Couldn't initialize logging module");
    }
    else {
        if (!pti_decodeCommandLine(ac, av)) {
            log_error_q("Couldn't decode command-line");
            pti_usage();
        }
        else {
            if (!pti_initAndExecute()) {
                log_flush_error();
            }
            else {
                status = EXIT_SUCCESS;
            }
        }

        log_fini();
    }

    return status;
}
