/**
 * Reports performance statistics on the product-queue to an LDM server.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
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

static volatile sig_atomic_t intr = 0;
static volatile sig_atomic_t stats_req = 0;

#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE ANY
#endif

/* binstats.c */
extern int binstats_add(const prod_info *infop,
        const struct timeval *reftimep);

extern void binstats_dump(void);
extern void binstats_sendIfTime(const char* hostname);

// ldmsend.c
extern int  ldmsend_init();
extern void ldmsend_destroy();


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
        log_debug("%s", s_prod_info(NULL, 0, infop, true));
        binstats_add(infop, &tv);
        return 0;
}


static void
usage(const char *av0) /*  id string */
{
    log_add(
"Usage: %s [options]\n"
"where:\n"
"  -v           Log INFO-level messages (log each product).\n"
"  -x           Log DEBUG-level messages.\n"
"  -l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"               (standard error), or file `dest`. Default is \"%s\"\n"
"  -f feedtype  Scan for data of type \"feedtype\" (default: \n"
    /*
     * NB: Don't use "s_feedtypet(DEFAULT_FEEDTYPE)" in the following because
     * it looks ugly for "ANY - EXP".
     */
"               \"ANY - EXP\").\n"
"  -p pattern   Interested in products matching \"pattern\"\n"
"               (default: \".*\").\n"
"  -q queue     Use file \"queue\" as product-queue (default: \n"
"               \"%s\").\n"
"  -o offset    Oldest product to consider is \"offset\"\n"
"               seconds before now (default: 0).\n"
"  -i interval  Poll queue every \"interval\" seconds (default:\n"
"               %d).\n"
"  -h hostname  Send to LDM server on host \"hostname\"\n"
"               (default: localhost).\n"
"  -P port      Send to port \"port\" (default: %d).",
            av0,
            log_get_default_destination(),
            getDefaultQueuePath(),
            DEFAULT_INTERVAL,
            LDM_PORT);
    log_flush_error();
    exit(1);
}


void
cleanup(void)
{
        log_notice_q("Exiting");

        if(pq && !intr) 
            (void)pq_close(pq);

        ldmsend_destroy();
        log_fini();
}


static void
signal_handler(int sig)
{
    switch(sig) {
    case SIGINT :
        intr = 1;
        exit(0);
        /*NOTREACHED*/
    case SIGTERM :
        done = 1;
        return;
    case SIGUSR1 :
        log_refresh();
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
    sigact.sa_flags = 0;

    /* Ignore the following */
    sigact.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sigact, NULL);
    (void) sigaction(SIGALRM, &sigact, NULL);
    (void) sigaction(SIGCHLD, &sigact, NULL);

    /* Handle the following */
    sigact.sa_handler = signal_handler;

    /* Don't restart the following */
    (void) sigaction(SIGINT, &sigact, NULL);

    /* Restart the following */
    sigact.sa_flags |= SA_RESTART;
    (void) sigaction(SIGTERM, &sigact, NULL);
    (void) sigaction(SIGUSR1, &sigact, NULL);
    (void) sigaction(SIGUSR2, &sigact, NULL);

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

int main(int ac, char *av[])
{
        const char* const  progname = basename(av[0]);
        prod_class_t       clss;
        prod_spec          spec;
        int                status = 0; // Success
        int                interval = DEFAULT_INTERVAL;
        int                toffset = TOFFSET_NONE;
        extern const char* remote;
        char*              hostname = ghostname();

        /*
         * Setup default logging before anything else.
         */
        if (log_init(av[0])) {
            log_syserr("Couldn't initialize logging module");
            exit(1);
        }

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
                log_error_q("setenv: Couldn't set TZ: %s", strerror(errnum));
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
                        if (log_set_destination(optarg)) {
                            log_syserr("Couldn't set logging destination to \"%s\"",
                                    optarg);
                            usage(progname);
                        }
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
                            log_notice_q("Adjusting pathological regular-expression \"%s\"",
                                    spec.pattern);
                            re_vetSpec(spec.pattern);
                        }
                        break;
                    case 'f': {
                        int fterr = strfeedtypet(optarg, &spec.feedtype) ;
                        if (fterr != FEEDTYPE_OK) {
                            log_error_q("Bad feedtype \"%s\", %s", optarg,
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
                            log_error_q("Invalid offset %s", optarg);
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

                            log_error_q("Invalid port %s", optarg);
                            usage(progname);
                        }

                        remotePort = (unsigned)port;

                        break;
                    }
                    case 'i':
                        interval = atoi(optarg);
                        if (interval == 0 && *optarg != '0') {
                            log_error_q("Invalid interval %s", optarg);
                            usage(progname);
                        }
                        break;
                    case '?':
                        log_error_q("Invalid option \"%c\"", optopt);
                        usage(progname);
                        break;
                    case ':':
                        log_error_q("No argument for option \"%c\"", optopt);
                        usage(progname);
                        break;
                }
            } /* getopt() loop */

            if (optind != ac) {
                log_error_q("Invalid operand: \"%s\"", av[optind]);
                usage(progname);
            }
        } /* command-line decoding block */

        const char* const  pqfname = getQueuePath();

        if (regcomp(&spec.rgx, spec.pattern, REG_EXTENDED|REG_NOSUB)) {
            log_error_q("Bad regular expression \"%s\"\n", spec.pattern);
            usage(progname);
        }

        log_notice_q("Starting Up (%d)", getpgrp());

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
                    log_error_q("The product-queue \"%s\" is inconsistent\n",
                            pqfname);
                }
                else {
                    log_error_q("pq_open failed: %s: %s\n",
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

        status = ldmsend_init();

        if (status == 0) {
            while(exitIfDone(0)) {
                if(stats_req) {
                    binstats_dump();
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
                    if (status > 0) {
                        log_add("pq_sequence failed: %s (errno = %d)",
                                strerror(status), status);
                        log_flush_error();
                    }
                    exit(1);
                    break;
                }

                binstats_sendIfTime(hostname);

                if(interval == 0) {
                    done = 1;
                    break;
                }

                pq_suspend(interval);
            } // While loop
        } // `ldmsend_init()` successful

        return status;
}
