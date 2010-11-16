/*
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */

/* 
 *  Copy a product-queue
 */

#include <config.h>
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
#include "ulog.h"
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
static int              showProdOrigin;
static const char       *inPath;
static const char       *outPath;
static pqueue           *inPq;
static pqueue           *outPq;
static int              nprods;


static void
dump_stats(void)
{
    unotice("Number of products copied: %d", nprods);
}


/*ARGSUSED*/
static int
copyProduct(
    const prod_info     *infop,
    void                *datap,
    void                *xprod,
    size_t              size,
    void                *notused)
{
    product             product;

    product.info = *infop;
    product.data = datap;

    switch (pq_insert(outPq, &product)) {
    case 0:
        if (ulogIsVerbose())
            uinfo("%s", s_prod_info(NULL, 0, infop, ulogIsDebug()));
        nprods++;
        return 0;
    case PQUEUE_DUP:
        uinfo("duplicate product: %s",
            s_prod_info(NULL, 0, infop, ulogIsDebug()));
        return 0;
    default:
        serror("Product copy failed");
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
"   -l logfile   Log to a file rather than stderr\n"
"   -o offset    Set the \"from\" time \"offset\" secs before now\n"
"                (default \"from\" the beginning of the epoch\n"
"   -p pattern   Interested in products matching \"pattern\" (default \".*\")\n"
"   -v           Verbose, tell me about each product\n"
"   -x           Add debuging to diagnostic outout\n"
"   inPath       Path name of source product-queue\n"
"   outPath      Path name of destination product-queue\n",
        av0, s_feedtypet(DEFAULT_FEEDTYPE));
    exit(1);
}


static void
cleanup(void)
{
    unotice("Exiting"); 

    if (!intr) {
        if (inPq != NULL)  
            (void)pq_close(inPq);
        if (outPq != NULL)
            (void)pq_close(outPq);
    }

    dump_stats();

    (void)closeulog();
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
            stats_req = !0;
            return;
    case SIGUSR2 :
            rollulogpri();
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
    (void) sigaction(SIGHUP, &sigact, NULL);
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
}


int main(
    const int   ac,
    char        *av[])
{
    const char          *progname = ubasename(av[0]);
    char                *logfname;
    prod_class_t        clss;
    prod_spec           spec;
    int                 status = 0;
    int                 interval = DEFAULT_INTERVAL;
    int                 logoptions = (LOG_CONS|LOG_PID) ;
    int                 queueSanityCheck = FALSE;
    char                *inPath;
    char                *outPath;

    logfname = "";

    if(isatty(fileno(stderr)))
    {
        /* set interactive defaults */
        logfname = "-" ;
        logoptions = 0 ;
    }

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
        int             logmask = (LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING) |
            LOG_MASK(LOG_NOTICE));
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
                logfname = optarg;
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
                logmask |= LOG_MASK(LOG_INFO);
                break;
            case 'x':
                logmask |= LOG_MASK(LOG_DEBUG);
                break;
            case '?':
                usage(progname);
                break;
            }
        }

        (void) setulogmask(logmask);

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

    /*
     * Set up error logging.
     */
    (void) openulog(progname, logoptions, LOG_LDM, logfname);
    unotice("Starting Up (%d)", getpgrp());

    /*
     * Register exit handler
     */
    if(atexit(cleanup) != 0) {
        serror("atexit");
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
            uerror("The input product-queue \"%s\" is inconsistent\n",
                inPath);
        }
        else {
            uerror("pq_open failed: %s: %s\n", inPath, strerror(status));
        }
        return 1;
    }

    /*
     * Open the output product-queue
     */
    if (0 != (status = pq_open(outPath, 0, &outPq))) {
        if (PQ_CORRUPT == status) {
            uerror("The output product-queue \"%s\" is inconsistent\n",
                outPath);
        }
        else {
            uerror("pq_open failed: %s: %s\n", outPath, strerror(status));
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
            udebug("End of Queue");
            done = 1;
            status = 0;
            break;
        case EAGAIN:
        case EACCES:
            udebug("Hit a lock");
            return 1;
            break;
        default:
            uerror("pq_sequence failed: %s (errno = %d)", strerror(status),
                status);
            return 1;
            break;
        }
    }

    return status;
}
