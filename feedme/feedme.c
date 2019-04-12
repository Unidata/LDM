/**
 * This file implements a program that requests a feed from an upstream LDM.
 *
 * **THIS PROGRAM IS DEPRECATED**
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

/* 
 * listens for data
 */

#include <config.h>

#include "ldm.h"
#include "ldm5.h"
#include "globals.h"
#include "remote.h"
#include "ldmprint.h"
#include "atofeedt.h"
#include "log.h"
#include "inetutil.h"
#include "ldm5_clnt.h"
#include "RegularExpressions.h"
#include "xdr_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <sys/socket.h>
#include <errno.h>
#include <regex.h>

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef DEFAULT_REMOTE
#define DEFAULT_REMOTE "localhost"
#endif
#ifndef DEFAULT_TIMEO
#define DEFAULT_TIMEO  25
#endif
#ifndef DEFAULT_TOTALTIMEO
#define DEFAULT_TOTALTIMEO  (12*DEFAULT_TIMEO)
#endif
#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE ANY
#endif
#ifndef DEFAULT_PATTERN
#define DEFAULT_PATTERN ".*"
#endif


static const char *remote = DEFAULT_REMOTE; /* hostname of data remote */
static ldm_replyt reply = { OK };


/*
 * Called at exit.
 * This callback routine registered by atexit().
 */
static void
cleanup(void)
{
        log_notice_q("exiting");

        /* TODO: sign off */

        log_fini();
}


/*
 * Called upon receipt of signals.
 * This callback routine registered in set_sigactions() .
 */
static void
signal_handler(int sig)
{
    switch(sig) {
    case SIGINT :
            /*FALLTHROUGH*/
    case SIGTERM :
            done = 1;
            return;
    case SIGUSR1 :
            log_refresh();
            return;
    case SIGUSR2 :
            log_roll_level();
            return;
    case SIGPIPE :
            return;
    }
}


static void
set_sigactions(void)
{
#ifdef HAVE_SIGACTION
        struct sigaction sigact;

        sigact.sa_handler = signal_handler;
        (void)sigemptyset(&sigact.sa_mask);

        sigact.sa_flags = SA_RESTART;
        (void) sigaction(SIGUSR1, &sigact, NULL);
        (void) sigaction(SIGUSR2, &sigact, NULL);

        sigact.sa_flags = 0;
        (void) sigaction(SIGINT, &sigact, NULL);
        (void) sigaction(SIGTERM, &sigact, NULL);
        (void) sigaction(SIGPIPE, &sigact, NULL);
#else
        (void) signal(SIGINT, signal_handler);
        (void) signal(SIGTERM, signal_handler);
        (void) signal(SIGUSR1, signal_handler);
        (void) signal(SIGUSR2, signal_handler);
        (void) signal(SIGPIPE, signal_handler);
#endif
        sigset_t sigset;
        (void)sigemptyset(&sigset);
        (void)sigaddset(&sigset, SIGINT);
        (void)sigaddset(&sigset, SIGPIPE);
        (void)sigaddset(&sigset, SIGTERM);
        (void)sigaddset(&sigset, SIGUSR1);
        (void)sigaddset(&sigset, SIGUSR2);
        (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}


static void
usage(
        char *av0  /*  id string */
)
{
        (void)fprintf(stderr,
"Usage: %s [options] \t\nOptions:\n", av0);
        (void)fprintf(stderr,
"\t-v             Verbose, report each notification\n");
        (void)fprintf(stderr,
"\t-x             Debug mode\n");
        (void)fprintf(stderr,
"\t-l dest        Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t               (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
        (void)fprintf(stderr,
"\t-h remote      Have \"remote\" send us data (default \"%s\")\n",
                DEFAULT_REMOTE);
        (void)fprintf(stderr,
"\t-f feedtype    Interested in products from feed \"feedtype\" (default %s)\n",
                s_feedtypet(DEFAULT_FEEDTYPE));
        (void)fprintf(stderr,
"\t-p pattern     Interested in products matching \"pattern\" (default \"%s\")\n",
                DEFAULT_PATTERN);
        (void)fprintf(stderr,
"\t-o offset      Set the \"from\" time offset secs before now\n");
        (void)fprintf(stderr,
"\t-t timeout     Set RPC timeout to \"timeout\" seconds (default %d)\n",
                DEFAULT_TIMEO);
        (void)fprintf(stderr,
"\t-T TotalTimeo  Give up after this many secs (default %d)\n",
                DEFAULT_TOTALTIMEO);
        exit(1);
}


/*ARGSUSED*/
ldm_replyt * 
hiya_5_svc(prod_class *clssp, struct svc_req *rqstp)
{

        (void) memset((char*)&reply, 0, sizeof(reply));

        if(log_is_enabled_info)
        {
                log_info_q("hiya5: %s: %s",
                        remote,
                         s_prod_class(NULL, 0, clssp));
        }

        return(&reply);
}


static prod_class clss;


/*
 * Used by comingsoon_5_svc and blkdata_5_svc
 * to keep track of the product in progress.
 * N.B., this program only handles product in progress
 * at a time.
 */
static signaturet signature;
static unsigned remaining;
static timestampt arrival;
static char infostr[LDM_INFO_MAX]; /* only used for log messages */

/*ARGSUSED*/
ldm_replyt * 
comingsoon_5_svc(comingsoon_args *argsp, struct svc_req *rqstp)
{
        prod_info *infop = argsp->infop;
        (void) memset((char*)&reply, 0, sizeof(reply));

        (void) s_prod_info(infostr, sizeof(infostr), infop, 0);
        if(log_is_enabled_debug)
        {
                log_debug("comingsoon5: %s %s (pktsz %u)",
                        s_signaturet(NULL, 0, infop->signature),
                        infostr, argsp->pktsz);
        }

        /* set up control structure */
        remaining = infop->sz;
        (void)memcpy(signature, infop->signature, sizeof(signature));
        arrival = infop->arrival;

        return(&reply);
}


/*ARGSUSED*/
ldm_replyt * 
blkdata_5_svc(datapkt *dpkp, struct svc_req *rqstp)
{
        (void) memset((char*)&reply, 0, sizeof(reply));

        if(log_is_enabled_debug)
        {
                log_debug("   blkdata5: %s %8u %5u",
                        s_signaturet(NULL, 0, *dpkp->signaturep),
                        dpkp->data.dbuf_len,
                        dpkp->pktnum);
        }

        if(memcmp(*dpkp->signaturep, signature, sizeof(signaturet)) != 0)
        {
                log_error_q("signature mismatch");
                goto err;
        }
        /* else */

        remaining -= dpkp->data.dbuf_len;


        /*
         * Update the request filter with the timestamp
         * we just recieved.
         * N.B.: There can still be duplicates after
         * a reconnect.
         */
        if(remaining == 0)
        {
                clss.from = arrival;
                timestamp_incr(&clss.from);
                log_info_q("%s", infostr);
        }


        /* 
         * your code here, example writes to stdout
         */
        if( write(STDOUT_FILENO, dpkp->data.dbuf_val, dpkp->data.dbuf_len) !=
                        dpkp->data.dbuf_len)
        {
                log_add_syserr( "data write failed") ;
                log_flush_error();
                exit(1) ;
        }

        xd_reset();                     /* reset XDR data-buffer */

        return(&reply);
err:
        svcerr_systemerr(rqstp->rq_xprt);
        xd_reset();
        return NULL;
}


/*
 * The RPC dispatch routine for this program.
 * Registered as a callback by svc_register() below.
 * Note that only NULLPROC, HIYA, COMINGSOON, and BLKDATA are
 * handled by this program.
 */
static void
feedmeprog_5(struct svc_req *rqstp, SVCXPRT *transp)
{
        union {
                prod_class hiya_5_arg;
                comingsoon_args comingsoon_5_arg;
                datapkt blkdata_5_arg;
        } argument;
        char *result;
        xdrproc_t xdr_argument, xdr_result;
        char *(*local)(char *, struct svc_req *);

        switch (rqstp->rq_proc) {
        case NULLPROC:
                (void) svc_sendreply(transp, (xdrproc_t) xdr_void, (char *)NULL);
                return;

        case HIYA:
                xdr_argument = (xdrproc_t) xdr_prod_class;
                xdr_result = (xdrproc_t) xdr_ldm_replyt;
                local = (char *(*)(char *, struct svc_req *)) hiya_5_svc;
                break;

        case COMINGSOON:
                xdr_argument = (xdrproc_t) xdr_comingsoon_args;
                xdr_result = (xdrproc_t) xdr_ldm_replyt;
                local = (char *(*)(char *, struct svc_req *)) comingsoon_5_svc;
                break;

        case BLKDATA:
                xdr_argument = (xdrproc_t) xdr_datapkt;
                xdr_result = (xdrproc_t) xdr_ldm_replyt;
                local = (char *(*)(char *, struct svc_req *)) blkdata_5_svc;
                break;

        default:
                svcerr_noproc(transp);
                return;
        }
        (void) memset((char *)&argument, 0, sizeof (argument));
        if (!svc_getargs(transp, xdr_argument, (caddr_t) &argument)) {
                svcerr_decode(transp);
                return;
        }
        result = (*local)((char *)&argument, rqstp);

        (void)exitIfDone(0);

        if (result != NULL && !svc_sendreply(transp, xdr_result, result)) {
                svcerr_systemerr(transp);
        }
        if (!svc_freeargs(transp, xdr_argument, (caddr_t) &argument)) {
                log_error_q("unable to free arguments");
                exit(1);
        }
        return;
}


int main(int ac, char *av[])
{
    int      TotalTimeo = DEFAULT_TOTALTIMEO;
    unsigned timeo = DEFAULT_TIMEO;
    unsigned interval = DEFAULT_TIMEO;
    prod_spec spec;
    int status;
    prod_class *clssp;

    /*
     * initialize logger
     */
    if (log_init(av[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }

    if(set_timestamp(&clss.from) != 0)
    {
            (void)fprintf(stderr, "Couldn't set timestamp\n");
            exit(1);
    }
    clss.to = TS_ENDT;
    clss.psa.psa_len = 1;
    clss.psa.psa_val = &spec;
    spec.feedtype = DEFAULT_FEEDTYPE;
    spec.pattern = DEFAULT_PATTERN;

    { /* Begin getopt block */
    extern int optind;
    extern int opterr;
    extern char *optarg;
    int ch;
    int fterr;

    opterr = 1;

    while ((ch = getopt(ac, av, "vxl:f:o:t:h:p:T:")) != EOF)
            switch (ch) {
            case 'v':
                    if (!log_is_enabled_info)
                        (void)log_set_level(LOG_LEVEL_INFO);
                    break;
            case 'x':
                    (void)log_set_level(LOG_LEVEL_DEBUG);
                    break;
            case 'l':
                    log_set_destination(optarg);
                    break;
            case 'h':
                    remote = optarg;
                    break;
            case 'p':
                    spec.pattern = optarg;
                    /* compiled below */
                    break;
            case 'f':
                    fterr = strfeedtypet(optarg, &spec.feedtype);
                    if(fterr != FEEDTYPE_OK)
                    {
                            (void)fprintf(stderr, "Bad feedtype \"%s\", "
                                "%s\n", optarg, strfeederr(fterr));
                            usage(av[0]);
                    }
                    break;
            case 'o':
                    clss.from.tv_sec -= atoi(optarg);
                    break;
            case 'T':
                    TotalTimeo = atoi(optarg);
                    if(TotalTimeo <= 0)
                    {
                            (void)fprintf(stderr,
                                "%s: invalid TotalTimeo %s", av[0], optarg);
                            usage(av[0]);
                    }
                    break;
            case 't':
                    timeo = (unsigned)atoi(optarg);
                    if(timeo == 0 || timeo > 32767)
                    {
                            (void)fprintf(stderr,
                                "%s: invalid timeout %s", av[0], optarg);
                            usage(av[0]);
                    }
                    break;
            case '?':
                    usage(av[0]);
                    break;
            }

    if(ac - optind > 0)
            usage(av[0]);

    if (re_isPathological(spec.pattern))
    {
            (void)fprintf(stderr,
                "Adjusting pathological regular-expression: "
                "\"%s\"\n", spec.pattern);
            re_vetSpec(spec.pattern);
    }
    status = regcomp(&spec.rgx,
            spec.pattern,
            REG_EXTENDED|REG_NOSUB);
    if(status != 0)
    {
            (void)fprintf(stderr, "Bad regular expression \"%s\"\n",
                    spec.pattern);
            usage(av[0]);
    }

    if(TotalTimeo < timeo)
    {
            (void)fprintf(stderr, "TotalTimeo %d < timeo %u\n",
                     TotalTimeo, timeo);
            usage(av[0]);
    }

    } /* End getopt block */

    log_notice_q("Starting Up: %s: %s",
                    remote,
                    s_prod_class(NULL, 0, &clss));

    /*
     * register exit handler
     */
    if(atexit(cleanup) != 0)
    {
            log_add_syserr("atexit");
            log_flush_error();
            exit(1);
    }

    /*
     * set up signal handlers
     */
    set_sigactions();


    /*
     * Try forever.
     */
    while (exitIfDone(0))
    {
            clssp = &clss;
            status = forn5(FEEDME, remote, &clssp,
                            timeo, TotalTimeo, feedmeprog_5);

            (void)exitIfDone(0);

            switch(status) {
                    /* problems with remote, retry */
            case ECONNABORTED:
            case ECONNRESET:
            case ETIMEDOUT:
            case ECONNREFUSED:
                    (void)sleep(interval);
                    break;
            case 0:
                    /* assert(done); */
                    break;
            default:
                    /* some wierd error */
                    done = 1;
                    exit(1);
            }
    }

    return 0;
}
