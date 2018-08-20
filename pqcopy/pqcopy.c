/*
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */

/* 
 *  Copy a product-queue
 */

#include <config.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <regex.h>
#include "ldm.h"
#include "globals.h"
#include "atofeedt.h"
#include "ldmprint.h"
#include "log.h"
#include "pq.h"
#include "RegularExpressions.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

/* default "one trip" */
#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 0
#endif

#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE ANY
#endif

static volatile int     intr;
static volatile int     stats_req;
static pqueue           *inPq;
static pqueue           *outPq;
static int              nprods;


static void
dump_stats(void)
{
    log_notice_q("Number of products copied: %d", nprods);
}


/*ARGSUSED*/
static int
copyProduct(
    const prod_info     *infop,
    const void          *datap,
    void                *xprod,
    size_t              size,
    void                *notused)
{
    product             product;

    product.info = *infop;
    product.data = (void*)datap;

    switch (pq_insert(outPq, &product)) {
    case 0:
        if (log_is_enabled_info)
            log_info_q("%s", s_prod_info(NULL, 0, infop,
                    log_is_enabled_debug));
        nprods++;
        return 0;
    case PQUEUE_DUP:
        log_info_q("duplicate product: %s",
            s_prod_info(NULL, 0, infop,
                    log_is_enabled_debug));
        return 0;
    default:
        log_syserr_q("Product copy failed");
        return 1;
    }
}


static void
usage(const char *av0) /*  id string */
{
    (void)fprintf(stderr,
"Usage: %s [options] inPath outPath\n"
"where:\n"
"   -f feedtype  Scan for data of type \"feedtype\" (default \"%s\")\n"
"   -l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"                (standard error), or file `dest`. Default is \"%s\"\n"
"   -o offset    Set the \"from\" time \"offset\" secs before now\n"
"                (default \"from\" the beginning of the epoch\n"
"   -p pattern   Interested in products matching \"pattern\" (default \".*\")\n"
"   -v           Verbose, tell me about each product\n"
"   -x           Add debuging to diagnostic outout\n"
"   inPath       Path name of source product-queue\n"
"   outPath      Path name of destination product-queue. Must exist.\n",
        av0, s_feedtypet(DEFAULT_FEEDTYPE), log_get_default_destination());
    exit(1);
}


static void
cleanup(void)
{
    log_notice_q("Exiting");

    if (!intr) {
        if (inPq != NULL)  
            (void)pq_close(inPq);
        if (outPq != NULL)
            (void)pq_close(outPq);
    }

    dump_stats();

    log_fini();
}


static void
signal_handler(int sig)
{
#ifdef SVR3SIGNALS
    /* 
     * Some systems reset handler to SIG_DFL upon entry to handler.
     * In that case, we reregister our handler.
     */
    (void) signal(sig, signal_handler);
#endif
    switch(sig) {
    case SIGINT :
            intr = !0;
            exit(0);
    case SIGTERM :
            done = !0;      
            return;
    case SIGUSR1 :
            log_refresh();
            stats_req = !0;
            return;
    case SIGUSR2 :
            log_roll_level();
            return;
    }
}


/*
 * register the signal_handler
 */
static void
set_sigactions(void)
{
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Ignore these */
    sigact.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sigact, NULL);
    (void) sigaction(SIGALRM, &sigact, NULL);
    (void) sigaction(SIGCHLD, &sigact, NULL);

    /* Handle these */
#ifdef SA_RESTART       /* SVR4, 4.3+ BSD */
    /* usually, restart system calls */
    sigact.sa_flags |= SA_RESTART;
#endif
    sigact.sa_handler = signal_handler;
    (void) sigaction(SIGTERM, &sigact, NULL);
    (void) sigaction(SIGUSR1, &sigact, NULL);
    (void) sigaction(SIGUSR2, &sigact, NULL);

    /* Don't restart after interrupt */
    sigact.sa_flags = 0;
#ifdef SA_INTERRUPT     /* SunOS 4.x */
    sigact.sa_flags |= SA_INTERRUPT;
#endif
    (void) sigaction(SIGINT, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGPIPE);
    (void)sigaddset(&sigset, SIGALRM);
    (void)sigaddset(&sigset, SIGCHLD);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}


int main(
    const int   ac,
    char        *av[])
{
    const char          *progname = basename(av[0]);
    prod_class_t        clss;
    prod_spec           spec;
    int                 status = 0;
    int                 interval = DEFAULT_INTERVAL;
    int                 queueSanityCheck = FALSE;
    char                *inPath;
    char                *outPath;

    /*
     * Set up error logging.
     */
    (void)log_init(progname);

    clss.from = TS_ZERO; /* default dump the whole file */
    clss.to = TS_ENDT;
    clss.psa.psa_len = 1;
    clss.psa.psa_val = &spec;
    spec.feedtype = ANY;
    spec.pattern = ".*";

    {
        extern int      optind;
        extern int      opterr;
        extern char     *optarg;
        int             ch;
        int             fterr;

        opterr = 1;

        while ((ch = getopt(ac, av, "f:l:o:p:vx")) != EOF) {
            switch (ch) {
            case 'f':
                fterr = strfeedtypet(optarg, &spec.feedtype) ;

                if(fterr != FEEDTYPE_OK) {
                    fprintf(stderr, "Bad feedtype \"%s\", %s\n",
                        optarg, strfeederr(fterr)) ;
                    usage(progname);        
                }
                break;
            case 'l':
                (void)log_set_destination(optarg);
                break;
            case 'o':
                (void) set_timestamp(&clss.from);
                clss.from.tv_sec -= atoi(optarg);
                break;
            case 'p':
                spec.pattern = optarg;
                /* compiled below */
                break;
            case 'v':
                if (!log_is_enabled_info)
                    (void)log_set_level(LOG_LEVEL_INFO);
                break;
            case 'x':
                (void)log_set_level(LOG_LEVEL_DEBUG);
                break;
            case '?':
                usage(progname);
                break;
            }
        }

        if (re_isPathological(spec.pattern)) {
            fprintf(stderr, "Adjusting pathological regular-expression: "
                "\"%s\"\n", spec.pattern);
            re_vetSpec(spec.pattern);
        }
        if (0 != (status = regcomp(&spec.rgx, spec.pattern,
                REG_EXTENDED|REG_NOSUB))) {
            fprintf(stderr, "Bad regular expression \"%s\"\n", spec.pattern);
            usage(progname);
        }

        if (2 != ac - optind)
            usage(progname);

        inPath = av[optind++];
        outPath = av[optind++];
    }                                   /* command-line decoding block */

    log_notice_q("Starting Up (%d)", getpgrp());

    /*
     * Register exit handler
     */
    if(atexit(cleanup) != 0) {
        log_syserr_q("atexit");
        return 1;
    }

    /*
     * Set up signal handlers
     */
    set_sigactions();

    /*
     * Open the input product-queue
     */
    if (0 != (status = pq_open(inPath, PQ_READONLY, &inPq))) {
        if (PQ_CORRUPT == status) {
            log_error_q("The input product-queue \"%s\" is inconsistent\n",
                inPath);
        }
        else {
            log_error_q("pq_open failed: %s: %s\n", inPath, strerror(status));
        }
        return 1;
    }

    /*
     * Open the output product-queue
     */
    if (0 != (status = pq_open(outPath, 0, &outPq))) {
        if (PQ_CORRUPT == status) {
            log_error_q("The output product-queue \"%s\" is inconsistent\n",
                outPath);
        }
        else {
            log_error_q("pq_open failed: %s: %s\n", outPath, strerror(status));
        }
        return 1;
    }

    /*
     * Set the cursor position to starting time
     */
    pq_cset(inPq, &clss.from );

    while(!done) {
        if(stats_req) {
            dump_stats();
            stats_req = 0;
        }

        status = pq_sequence(inPq, TV_GT, &clss, copyProduct, 0);

        switch(status) {
        case 0: /* no error */
            continue;                   /* N.B., other cases sleep */
        case PQUEUE_END:
            log_debug("End of Queue");
            done = 1;
            status = 0;
            break;
        case EAGAIN:
        case EACCES:
            log_debug("Hit a lock");
            return 1;
            break;
        default:
            log_error_q("pq_sequence failed: %s (errno = %d)", strerror(status),
                status);
            return 1;
            break;
        }
    }

    return status;
}
