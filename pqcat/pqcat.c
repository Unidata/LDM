/**
 * Write data-products from the LDM product-queue to the standard output stream.
 *
 * Copyright 2019, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

#include "config.h"

#include "atofeedt.h"
#include "globals.h"
#include "ldm.h"
#include "ldmprint.h"
#include "log.h"
#include "md5.h"
#include "pq.h"
#include "RegularExpressions.h"

#include <assert.h>
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

static volatile int          intr = 0;
static volatile sig_atomic_t stats_req = 0;
static int                   showProdOrigin = 0;

static const char *pqfname;

static int nprods = 0;

static MD5_CTX *md5ctxp = NULL;

static void
dump_stats(void)
{
        log_notice_q("Number of products %d", nprods);
}

/*
 */
/*ARGSUSED*/
static int
writeprod(const prod_info *infop, const void *datap,
                void *xprod, size_t size,  void *notused)
{
        if(log_is_enabled_info)
        {
                if (showProdOrigin)
                {
                        log_info_q("%s %s", s_prod_info(NULL, 0, infop,
                                log_is_enabled_debug),
                                infop->origin);
                } else
                {
                        log_info_q("%s", s_prod_info(NULL, 0, infop,
                                log_is_enabled_debug));
                }
        }

        if(md5ctxp != NULL) /* -c option */
        {
                signaturet check;
                MD5Init(md5ctxp);
                MD5Update(md5ctxp, datap, infop->sz);
                MD5Final((unsigned char*)check, md5ctxp);
                if(memcmp(infop->signature, check, sizeof(signaturet)) != 0)
                {
                        char sb[33]; char cb[33];
                        log_error_q("signature mismatch: %s != %s",
                                 s_signaturet(sb, sizeof(sb), infop->signature),
                                 s_signaturet(cb, sizeof(cb), check));
                }
        }

        if( write(STDOUT_FILENO, datap, infop->sz) !=
                        infop->sz)
        {
                int errnum = errno;
                log_syserr( "data write failed") ;
                return errnum;
        }

        nprods++;

        return 0;
}

static int
tallyProds(const prod_info *infop, const void *datap,
                void *xprod, size_t size,  void *notused)
{
        nprods++;

        return 0;
}

static void
usage(const char *av0) /*  id string */
{
        (void)fprintf(stderr,
"Usage: %s [options] [outputfile]\n\tOptions:\n", av0);
        (void)fprintf(stderr,
"\t-v           Verbose, tell me about each product\n");
        (void)fprintf(stderr,
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
        (void)fprintf(stderr,
"\t-f feedtype  Scan for data of type \"feedtype\" (default \"%s\")\n",
                s_feedtypet(DEFAULT_FEEDTYPE));
        (void)fprintf(stderr,
"\t-p pattern   Interested in products matching \"pattern\" (default \".*\")\n") ;
        (void)fprintf(stderr,
"\t-q pqfname   (default \"%s\")\n", getDefaultQueuePath());
        (void)fprintf(stderr,
"\t-o offset    Set the \"from\" time \"offset\" secs before now\n");
        (void)fprintf(stderr,
"\t             (default \"from\" the beginning of the epoch)\n");
        (void)fprintf(stderr,
"\t-i interval  Poll queue after \"interval\" secs (default %d)\n",
                DEFAULT_INTERVAL);
        (void)fprintf(stderr,
"\t             (\"interval\" of 0 means exit at end of queue)\n");
        (void)fprintf(stderr,
"\t-c           Check, verify MD5 signature\n");
        (void)fprintf(stderr,
"\t-O           Include product origin in verbose output\n");
        (void)fprintf(stderr,
"\t             (valid only with -v option)\n");
        (void)fprintf(stderr,
"\t-s           Check queue for sanity/non-corruption\n");
        (void)fprintf(stderr,
"Output defaults to standard output\n");
        exit(1);
}


static void
cleanup(void)
{
        log_notice_q("Exiting");

        if(!intr)
        {
                if(md5ctxp != NULL)
                        free_MD5_CTX(md5ctxp);  

                if(pq != NULL)  
                        (void)pq_close(pq);
        }

        dump_stats();

        log_fini();
}


static void
signal_handler(int sig)
{
    switch(sig) {
    case SIGINT :
        intr = !0;
        exit(0);
    case SIGTERM :
        done = !0;
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
        const char *progname = basename(av[0]);
        prod_class_t clss;
        prod_spec spec;
        int status = 0;
        int interval = DEFAULT_INTERVAL;
        int logoptions = (LOG_CONS|LOG_PID) ;
        int queueSanityCheck = FALSE;

        /*
         * Set up error logging.
         */
        if (log_init(progname)) {
            log_syserr("Couldn't initialize logging module");
            exit(1);
        }

        clss.from = TS_ZERO; /* default dump the whole file */
        clss.to = TS_ENDT;
        clss.psa.psa_len = 1;
        clss.psa.psa_val = &spec;
        spec.feedtype = ANY;
        spec.pattern = ".*";

        {
        extern int optind;
        extern int opterr;
        extern char *optarg;
        int ch;
        int fterr;

        pqfname = getQueuePath();
        opterr = 1;

        while ((ch = getopt(ac, av, "cvxOsl:p:f:q:o:i:")) != EOF)
                switch (ch) {
                case 'c':
                        md5ctxp = new_MD5_CTX();
                        if(md5ctxp == NULL)
                                log_syserr("new_md5_CTX failed");
                        break;
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
                case 'p':
                        spec.pattern = optarg;
                        /* compiled below */
                        break;
                case 'f':
                        fterr = strfeedtypet(optarg, &spec.feedtype) ;
                        if(fterr != FEEDTYPE_OK)
                        {
                                fprintf(stderr, "Bad feedtype \"%s\", %s\n",
                                        optarg, strfeederr(fterr)) ;
                                usage(progname);        
                        }
                        break;
                case 'q':
                        pqfname = optarg;
                        break;
                case 'o':
                        (void) set_timestamp(&clss.from);
                        clss.from.tv_sec -= atoi(optarg);
                        break;
                case 'i':
                        interval = atoi(optarg);
                        if(interval == 0 && *optarg != '0')
                        {
                                fprintf(stderr, "%s: invalid interval %s",
                                        progname, optarg);
                                usage(progname);
                        }
                        break;
                case 'O':
                        showProdOrigin = TRUE;
                        break;
                case 's':
                        queueSanityCheck = TRUE;
                        break;
                case '?':
                        usage(progname);
                        break;
                }


        if (re_isPathological(spec.pattern))
        {
                fprintf(stderr, "Adjusting pathological regular-expression: "
                    "\"%s\"\n", spec.pattern);
                re_vetSpec(spec.pattern);
        }
        status = regcomp(&spec.rgx,
                spec.pattern,
                REG_EXTENDED|REG_NOSUB);
        if(status != 0)
        {
                fprintf(stderr, "Bad regular expression \"%s\"\n",
                        spec.pattern);
                usage(av[0]);
        }

        setQueuePath(pqfname);

        /* last arg, outputfname, is optional */
        if(ac - optind > 0)
        {
                const char *const outputfname = av[optind];
                if(freopen(outputfname, "a+b", stdout) == NULL)
                {
                        status = errno;
                        fprintf(stderr, "%s: Couldn't open \"%s\": %s\n",
                                progname, outputfname, strerror(status));
                }
        }

        }

        {
            char buf[128];
            (void)pc_format(&clss, buf, sizeof(buf));
            buf[sizeof(buf)-1] = 0;
            log_notice_q("Starting Up (%d): prod_class=%s", getpgrp(), buf);
        }

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
         * Open the product que
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

        /*
         * Set the cursor position to starting time
         */
        pq_cset(pq, &clss.from );

        while(!done)
        {
                if(stats_req)
                {
                        dump_stats();
                        stats_req = 0;
                }

                if (queueSanityCheck)
                  status = pq_sequence(pq, TV_GT, &clss, tallyProds, 0);
                else
                  status = pq_sequence(pq, TV_GT, &clss, writeprod, 0);

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
                        log_error_q("pq_sequence failed: %s (errno = %d)",
                                strerror(status), status);
                        exit(1);
                        break;
                }

                if(interval == 0)
                        break;
                pq_suspend(interval);
                        
        }

        if (queueSanityCheck)
        {
          size_t queueProdCnt;
          size_t dummy1, dummy2, dummy3, dummy4, dummy5, dummy6, dummy7, dummy9;
          double dummy8;
          status = pq_stats(pq, &queueProdCnt, &dummy1, &dummy2, &dummy3, &dummy4, 
                   &dummy5, &dummy6, &dummy7, &dummy8, &dummy9);
          if (status) {
                log_error_q("pq_stats failed: %s (errno = %d)",
                       strerror(status), status);
                exit(1);
          }

          if (nprods == queueProdCnt)
          {
            log_notice_q("pqcat queueSanityCheck: Number of products tallied consistent with value in queue");
            exit(0);
          }
          else
          {
              log_error_q("pqcat queueSanityCheck: Product count doesn't match");
              log_error_q("products tallied: %d   Value in queue: %d", nprods, queueProdCnt);              exit(1);
          }
        }
        exit(0);
        /*NOTREACHED*/
}
