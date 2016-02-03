/**
 *   Copyright 2013, University Corporation for Atmospheric Research
 *   All rights reserved.
 *   <p>
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 *   <p>
 *   This file contains the program rtstats(1), which reports performance
 *   statistics on the product-queue to an LDM server.
 */

#include <config.h>
#include <libgen.h>
#include <fcntl.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <regex.h>
#include "ldm.h"
#include "atofeedt.h"
#include "globals.h"
#include "inetutil.h"
#include "remote.h"
#include "ldmprint.h"
#include "log.h"
#include "pq.h"
#ifndef HAVE_SETENV
    #include "setenv.h"
#endif
#include "RegularExpressions.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 15
#endif

static volatile int intr = 0;
static volatile int stats_req = 0;

#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE ANY
#endif

/* binstats.c */
extern int binstats(const prod_info *infop,
        const struct timeval *reftimep);

extern void dump_statsbins(void);
extern void syncbinstats(const char* hostname);

unsigned remotePort = LDM_PORT;


/*
 */
/*ARGSUSED*/
static int
addtostats(const prod_info *infop, const void *datap,
                void *xprod, size_t size,  void *notused)
{
        struct timeval tv;
        pq_ctimestamp(pq, &tv);
        if(tvIsNone(tv))
                tv = TS_ZERO;
        if(log_is_enabled_info)
                log_info("%s", s_prod_info(NULL, 0, infop,
                        log_is_enabled_debug));
        binstats(infop, &tv);
        return 0;
}


static void
usage(const char *av0) /*  id string */
{
    log_error("Usage: %s [options]", av0);
    log_error("where:");
    log_error("    -v           Log INFO-level messages (log each product).");
    log_error("    -x           Log DEBUG-level messages.");
    log_error("    -l logfile   Log to file \"logfile\" rather than syslogd(8).");
    log_error("                 \"-\" means standard error.");
    /*
     * NB: Don't use "s_feedtypet(DEFAULT_FEEDTYPE)" in the following because
     * it looks ugly for "ANY - EXP".
     */
    log_error("    -f feedtype  Scan for data of type \"feedtype\" (default: ");
    log_error("                 \"ANY - EXP\").");
    log_error("    -p pattern   Interested in products matching \"pattern\"");
    log_error("                 (default: \".*\").") ;
    log_error("    -q queue     Use file \"queue\" as product-queue (default: ");
    log_error("                 \"%s\").", getDefaultQueuePath());
    log_error("    -o offset    Oldest product to consider is \"offset\"");
    log_error("                 seconds before now (default: 0).");
    log_error("    -i interval  Poll queue every \"interval\" seconds (default:");
    log_error("                 %d).", DEFAULT_INTERVAL);
    log_error("    -h hostname  Send to LDM server on host \"hostname\"");
    log_error("                 (default: LOCALHOST).");
    log_error("    -P port      Send to port \"port\" (default: %d).", LDM_PORT);
    exit(1);
}


void
cleanup(void)
{
        log_notice("Exiting");

        if(pq && !intr) 
                (void)pq_close(pq);

        (void)log_fini();
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
        case SIGHUP :
                log_refresh();
                return;
        case SIGINT :
                intr = 1;
                exit(0);
                /*NOTREACHED*/
        case SIGTERM :
                done = 1;       
                return;
        case SIGUSR1 :
                stats_req = 1;
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

        /* Ignore these */
        sigact.sa_flags = 0;
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
        (void) sigaction(SIGHUP,  &sigact, NULL);
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

int main(int ac, char *av[])
{
        const char* const  progname = basename(av[0]);
        prod_class_t       clss;
        prod_spec          spec;
        int                status = 0;
        int                interval = DEFAULT_INTERVAL;
        int                toffset = TOFFSET_NONE;
        extern const char* remote;
        char*              hostname = ghostname();

        /*
         * Setup default logging before anything else.
         */
        (void)log_init(progname);

        const char* const  pqfname = getQueuePath();

        remote = "localhost";

        if(set_timestamp(&clss.from) != ENOERR) /* corrected by toffset below */
        {
                int errnum = errno;
                fprintf(stderr, "Couldn't set timestamp: %s", 
                        strerror(errnum));
                exit(1);
        }
        clss.to = TS_ENDT;
        clss.psa.psa_len = 1;
        clss.psa.psa_val = &spec;
        spec.feedtype = DEFAULT_FEEDTYPE;
        spec.pattern = ".*";
        
        /*
         * Sigh, in order to read back from existing stats files,
         * we call mktime(3). Since the stats are in UTC,
         * and mktime uses "local" time, the local time for
         * this program must be UTC.
         */
        if(setenv("TZ", "UTC0",1))
        {
                int errnum = errno;
                log_error("setenv: Couldn't set TZ: %s", strerror(errnum));
                exit(1);        
        }

        {
            int ch;

            opterr = 0; /* stops getopt() from printing to stderr */

            while ((ch = getopt(ac, av, ":vxl:p:f:q:o:i:H:h:P:")) != EOF) {
                switch (ch) {
                    case 'v':
                        if (!log_is_enabled_info)
                            (void)log_set_level(LOG_LEVEL_INFO);
                        break;
                    case 'x':
                        (void)log_set_level(LOG_LEVEL_DEBUG);
                        break;
                    case 'l':
                        (void)log_set_destination(optarg);
                        break;
                    case 'H':
                        hostname = optarg;
                        break;
                    case 'h':
                        remote = optarg;
                        break;
                    case 'p':
                        spec.pattern = optarg;
                        if (re_isPathological(spec.pattern)) {
                            log_notice("Adjusting pathological regular-expression \"%s\"",
                                    spec.pattern);
                            re_vetSpec(spec.pattern);
                        }
                        break;
                    case 'f': {
                        int fterr = strfeedtypet(optarg, &spec.feedtype) ;
                        if (fterr != FEEDTYPE_OK) {
                            log_error("Bad feedtype \"%s\", %s", optarg,
                                    strfeederr(fterr)) ;
                            usage(progname);
                        }
                        break;
                    }
                    case 'q':
                        setQueuePath(optarg);
                        break;
                    case 'o':
                        toffset = atoi(optarg);
                        if(toffset == 0 && *optarg != '0') {
                            log_error("Invalid offset %s", optarg);
                            usage(progname);
                        }
                        break;
                    case 'P': {
                        char*       suffix = "";
                        long        port;

                        errno = 0;
                        port = strtol(optarg, &suffix, 0);

                        if (0 != errno || 0 != *suffix ||
                            0 >= port || 0xffff < port) {

                            log_error("Invalid port %s", optarg);
                            usage(progname);
                        }

                        remotePort = (unsigned)port;

                        break;
                    }
                    case 'i':
                        interval = atoi(optarg);
                        if (interval == 0 && *optarg != '0') {
                            log_error("Invalid interval %s", optarg);
                            usage(progname);
                        }
                        break;
                    case '?':
                        log_error("Invalid option \"%c\"", optopt);
                        usage(progname);
                        break;
                    case ':':
                        log_error("No argument for option \"%c\"", optopt);
                        usage(progname);
                        break;
                }
            } /* getopt() loop */

            if (optind != ac) {
                log_error("Invalid operand: \"%s\"", av[optind]);
                usage(progname);
            }
        } /* command-line decoding block */

        if (regcomp(&spec.rgx, spec.pattern, REG_EXTENDED|REG_NOSUB)) {
            log_error("Bad regular expression \"%s\"\n", spec.pattern);
            usage(progname);
        }

        log_notice("Starting Up (%d)", getpgrp());

        /*
         * register exit handler
         */
        if(atexit(cleanup) != 0)
        {
                log_syserr("atexit");
                exit(1);
        }

        /*
         * set up signal handlers
         */
        set_sigactions();


        /*
         * Open the product queue
         */
        status = pq_open(pqfname, PQ_READONLY, &pq);
        if(status)
        {
                if (PQ_CORRUPT == status) {
                    log_error("The product-queue \"%s\" is inconsistent\n",
                            pqfname);
                }
                else {
                    log_error("pq_open failed: %s: %s\n",
                            pqfname, strerror(status));
                }
                exit(1);
        }

        if(toffset == TOFFSET_NONE)
        {
                /*
                 * Be permissive with the time filter,
                 * jump now to the end of the queue.
                 */
                clss.from = TS_ZERO;
                (void) pq_last(pq, &clss, NULL);
        }
        else
        {
                /*
                 * Filter and queue position set by
                 * toffset.
                 */
                clss.from.tv_sec -= toffset;
                pq_cset(pq, &clss.from);
        }

        while(exitIfDone(0))
        {
                if(stats_req)
                {
                        dump_statsbins();
                        stats_req = 0;
                }

                status = pq_sequence(pq, TV_GT, &clss, addtostats, 0);

                switch(status) {
                case 0: /* no error */
                        continue; /* N.B., other cases sleep */
                case PQUEUE_END:
                        log_debug("End of Queue");
                        break;
                case EAGAIN:
                case EACCES:
                        log_debug("Hit a lock");
                        break;
                default:
                        log_error("pq_sequence failed: %s (errno = %d)",
                                strerror(status), status);
                        exit(1);
                        break;
                }

                syncbinstats(hostname);

                if(interval == 0)
                {
                        done = 1;
                        break;
                }

                pq_suspend(interval);
        }

        return 0;
}
