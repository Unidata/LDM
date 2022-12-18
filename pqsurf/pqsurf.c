/**
 * Separates a compound WMO bulletin into individual bulletins.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

/*
 * Need to create a queue before running this:
 * pqcreate -c -s 2M -S 13762 /home/ldm/data/pqsurf.pq
 */

#include <config.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <assert.h>
#include <regex.h>
#include "ldm.h"
#include "ldmfork.h"
#include "atofeedt.h"
#include "globals.h"
#include "registry.h"
#include "remote.h"
#include "ldmprint.h"
#include "log.h"
#include "pq.h"
#include "surface.h"
#include "RegularExpressions.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

extern int usePil;  /* 1/0 flag to signal use of AFOS like pil identifier */

#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 15
#endif

static volatile sig_atomic_t intr = 0;
static volatile sig_atomic_t stats_req = 0;

#ifndef DEFAULT_PATTERN
#define DEFAULT_PATTERN  "^S[AIMNP]"
#endif
#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE  (IDS|DDS)
#endif

static pqueue *opq = NULL;

#ifndef DEFAULT_PIPE_TIMEO
#define DEFAULT_PIPE_TIMEO 60
#endif

#ifndef DEFAULT_AGE
#define DEFAULT_AGE (1. + (double)(DEFAULT_INTERVAL)/3600.)
#endif

static pid_t act_pid;

/*
 * Set to non-root privilege if possible.
 * Do it in such a way that it is safe to fork.
 * TODO: this is duplicated from ../server/priv.c
 */
static void
endpriv(void)
{
        const uid_t euid = geteuid();
        const uid_t uid = getuid();

        /* if either euid or uid is unprivileged, use it */
        if(euid > 0)
                (void)setuid(euid);
        else if(uid > 0)
                (void)setuid(uid);

        /* else warn??? or set to nobody??? */
}

static pid_t
run_child(int argc, char *argv[])
{
        pid_t pid;

        if(log_is_enabled_debug)
        {
                char command[1024];
                size_t left = sizeof(command) - 1;
                int ii;

                command[0] = 0;

                for (ii = 0; ii < argc; ++ii)
                {
                        size_t  nbytes;

                        if (ii > 0) {
                                (void)strncat(command, " ", left);
                                left -= (1 <= left) ? 1 : left;
                        }

                        (void)strncat(command, argv[ii], left);
                        nbytes = strlen(argv[ii]);
                        left -= (nbytes <= left) ? nbytes : left;
                }
                log_debug("exec'ing: \"%s\"", command);
        }

        pid = ldmfork();
        if(pid == -1)
        {
                log_flush_error();
                return pid;
        }

        if(pid == 0)
        {       /* child */
                (void)signal(SIGCHLD, SIG_DFL);
                (void)signal(SIGTERM, SIG_DFL);

                /* keep same descriptors as parent */

                /* don't let child get real privilege */
                endpriv();

                (void) execvp(argv[0], &argv[0]);

                log_syserr("Couldn't execute decoder \"%s\"; PATH=%s", argv[0],
                        getenv("PATH"));
                _exit(127);
        }
        /* else, parent */

        return pid;
}


static int nprods = 0;
static int nsplit = 0;
static int ndups = 0;

static void
dump_stats(void)
{
        log_notice_q("Number of products %d", nprods);
        log_notice_q("Number of observations %d", nsplit);
        log_notice_q("Number of dups %d", ndups);
}


/* defined in surf_split.c */
extern int surf_split(const prod_info *infop, const void *datap,
                int (*doit)(const prod_info *, const void *));

static int
doOne(const prod_info *infop, const void *datap)
{
        struct product prod;
        int status = ENOERR;

        if(log_is_enabled_debug)
                log_debug("%s", s_prod_info(NULL, 0, infop, 1));
        
        prod.info = *infop;
        prod.data = (void *)datap; /* cast away const */

        nsplit++; /* ?? Do it here on only on success ?? */

        status = pq_insertNoSig(opq, &prod);
        if(status == ENOERR)
        {
                return status; /* Normal return */
        }

        /* else */
        if(status == PQUEUE_DUP)
        {
                ndups++;
                if(log_is_enabled_info)
                        log_info_q("Product already in queue: %s",
                                s_prod_info(NULL, 0, &prod.info,
                                         log_is_enabled_debug));
                return status;
        }

        /* else, error */
        log_error_q("pq_insert() returned %d", status);

        return status;
}


/*
 */
static int
split_prod(const prod_info *infop, const void *datap,
                void *xprod, size_t size,  void *vp)
{
        size_t *nsp = (size_t *)vp;
        int ns;

        if(log_is_enabled_info)
                log_info_q("%s", s_prod_info(NULL, 0, infop,
                        log_is_enabled_debug));

        ns = surf_split(infop, datap, doOne);

        nprods++;

        (void)kill(SIGCONT, act_pid);

        if(nsp != NULL && ns >= 0)
                *nsp = (size_t)ns;

        return 0;
}


static void
usage(const char *av0) /*  id string */
{
        (void)fprintf(stderr,
"Usage: %s [options] [confilename]\t\nOptions:\n",
                av0);
        (void)fprintf(stderr,
"\t-v           Verbose, log each match (SIGUSR2 toggles)\n");
        (void)fprintf(stderr,
"\t-x           Debug mode\n");
        (void)fprintf(stderr,
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
        (void)fprintf(stderr,
"\t-d datadir   cd to \"datadir\" before interpreting filenames in\n");
        (void)fprintf(stderr,
"\t             conffile (default %s)\n",
                getPqsurfDataDirPath());
        (void)fprintf(stderr,
"\t-q queue     default \"%s\"\n", getDefaultQueuePath());
        (void)fprintf(stderr,
"\t-p pattern   Interested in products matching \"pattern\" (default \"%s\")\n", DEFAULT_PATTERN);
        (void)fprintf(stderr,
"\t-f feedtype  Interested in products from feed \"feedtype\" (default %s)\n", s_feedtypet(DEFAULT_FEEDTYPE));
        (void)fprintf(stderr,
"\t-i interval  loop, polling each \"interval\" seconds (default %d)\n", DEFAULT_INTERVAL);
        (void)fprintf(stderr,
"\t-a age       Expire products older than \"age\" hours (default %.4f)\n", DEFAULT_AGE);
        (void)fprintf(stderr,
"\t-t timeo     set write timeo for PIPE subprocs to \"timeo\" secs (default %d)\n", DEFAULT_PIPE_TIMEO);
        (void)fprintf(stderr,
"\t-o offset    the oldest product we will consider is \"offset\" secs before now (default: most recent in output queue)\n");
        (void)fprintf(stderr,
"\t-Q outQueue    default \"%s\"\n", getSurfQueuePath());
        (void)fprintf(stderr,
"\t(default conffilename is %s)\n", getPqsurfConfigPath());
        exit(1);
}


static pid_t
reap_act(int options)
{
        pid_t wpid = 0;

        if (act_pid != 0) {
            int status = 0;

#ifdef HAVE_WAITPID
            wpid = waitpid(act_pid, &status, options);
#else
            if(options == 0)
                    wpid = wait(&status);
            /* customize here for older systems, use wait3 or whatever */
#endif
            if(wpid == -1)
            {
                    if(!(errno == ECHILD && act_pid == -1))
                    {
                             /* Only complain if relevant */
                            log_syserr("waitpid");
                    }
                    return -1;
            }
            /* else */

            if(wpid != 0) 
            {
                    /* tag_pid_entry(wpid); */

#ifdef HAVE_WAITPID
                    if(WIFSTOPPED(status))
                    {
                            log_notice_q("child %d stopped by signal %d",
                                    wpid, WSTOPSIG(status));
                    }
                    else if(WIFSIGNALED(status))
                    {
                            log_notice_q("child %d terminated by signal %d",
                                    wpid, WTERMSIG(status));
                            /* DEBUG */
                            switch(WTERMSIG(status)) {
                            /*
                             * If a child dumped core,
                             * shut everything down.
                             */
                            case SIGQUIT:
                            case SIGILL:
                            case SIGTRAP: /* ??? */
                            case SIGABRT:
#if defined(SIGEMT)
                            case SIGEMT: /* ??? */
#endif
                            case SIGFPE: /* ??? */
                            case SIGBUS:
                            case SIGSEGV:
#if defined(SIGSYS)
                            case SIGSYS: /* ??? */
#endif
#ifdef SIGXCPU
                            case SIGXCPU:
#endif
#ifdef SIGXFSZ
                            case SIGXFSZ:
#endif
                                    act_pid = -1;
                                    exit(1);
                                    break;
                            }
                    }
                    else if(WIFEXITED(status))
                    {
                            if(WEXITSTATUS(status) != 0)
                                    log_notice_q("child %d exited with status %d",
                                            wpid, WEXITSTATUS(status));
                            else
                                    log_debug("child %d exited with status %d",
                                            wpid, WEXITSTATUS(status));
                            act_pid = -1;
                            exit(WEXITSTATUS(status));
                    }
#endif
            }
        }

        return wpid;
}


void
cleanup(void)
{
        log_notice_q("Exiting");

        if(act_pid != -1)
        {
                (void)signal(SIGCHLD, SIG_IGN);
                kill(act_pid, SIGTERM);
                (void) reap_act(0);
        }

        if(opq != NULL)
        {
                off_t highwater = 0;
                size_t maxregions = 0;
                (void) pq_highwater(opq, &highwater, &maxregions);
                (void) pq_close(opq);
                opq = NULL;

                log_notice_q("  Queue usage (bytes):%8ld",
                                        (long)highwater);
                log_notice_q("           (nregions):%8ld",
                                        (long)maxregions);
        }

        if(pq != NULL)
        {
                (void) pq_close(pq);
                pq = NULL;
        }

        dump_stats();

        log_fini();
}


static void
signal_handler(int sig)
{
    switch(sig) {
    case SIGINT :
        intr = 1;
        exit(0);
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
    case SIGCHLD :
        /* usually calls exit */
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

    /* Handle the following */
    sigact.sa_handler = signal_handler;

    /* Don't restart the following */
    (void) sigaction(SIGINT, &sigact, NULL);

    /* Restart the following */
    sigact.sa_flags |= SA_RESTART;
    (void) sigaction(SIGTERM, &sigact, NULL);
    (void) sigaction(SIGUSR1, &sigact, NULL);
    (void) sigaction(SIGUSR2, &sigact, NULL);
    (void) sigaction(SIGCHLD, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGPIPE);
    (void)sigaddset(&sigset, SIGALRM);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGCHLD);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}


static int
expire(pqueue *epq, const unsigned interval, const double age)
{
        int status = ENOERR;
        static timestampt now;
        static prod_class eclss;
        static prod_spec spec;
        timestampt ts;
        timestampt cursor;
        double diff = 0.;
        double max_latency = 0.;
        size_t nr;

        if(eclss.psa.psa_val == 0)
        {
                /* first time */
                eclss.from = TS_ZERO;
                eclss.psa.psa_len = 1;
                eclss.psa.psa_val = &spec;
                spec.feedtype = ANY;
                spec.pattern = ".*";
                (void)regcomp(&spec.rgx, spec.pattern, REG_EXTENDED|REG_NOSUB);
        }

        (void) set_timestamp(&now);
        if(d_diff_timestamp(&now, &eclss.to) < interval + age)
        {
                /* only run this routine every interval seconds */
                log_debug("not yet");
                return ENOERR;
        }
        /* else */
        eclss.to = now;
        eclss.to.tv_sec -= age;

        if(log_is_enabled_debug)
        {
                char cp[64];
                sprint_timestampt(cp, sizeof(cp), &eclss.to);
                log_debug("to %s", cp);
        }

        pq_cset(epq, &TS_ZERO);

        while(exitIfDone(0) && !stats_req)
        {
                nr = 0;
                status = pq_seqdel(epq, TV_GT, &eclss, 0, &nr, &ts);

                switch(status) {
                case ENOERR:
                        pq_ctimestamp(epq, &cursor);
                        diff = d_diff_timestamp(&cursor, &ts);
                        if(diff > max_latency)
                        {
                                max_latency = diff;
                                log_debug("max_latency %.3f", max_latency);
                        }
                        
                        if(nr == 0)
                        {
                                diff = d_diff_timestamp(&cursor, &eclss.to);
                                log_debug("diff %.3f", diff);
                                if(diff > interval + max_latency)
                                {
                                        log_debug("heuristic depth break");
                                        break;
                                }

                        }
                        continue; /* N.B., other cases break and return */
                case PQUEUE_END:
                        log_debug("expire: End of Queue");
                        break;
                case EAGAIN:
                case EACCES:
                        log_debug("Hit a lock");
                        break;
#if defined(EDEADLOCK) && EDEADLOCK != EDEADLK
                case EDEADLOCK:
#endif
                case EDEADLK:
                        log_add_errno(status, NULL);
                        log_flush_error();
                        break;
                default:
                        log_add_errno(status, "pq_seqdel failed");
                        log_flush_error();
                        break;
                }
                break;
        }
        return status;
}


int main(int ac, char *av[])
{
        const char *opqfname = getSurfQueuePath();
        const char *progname = basename(av[0]);
        prod_class_t clss;
        prod_spec spec;
        int status = 0;
        unsigned interval = DEFAULT_INTERVAL;
        double age = DEFAULT_AGE;
        /* these are containers for the pqact args */
        char *argv[16];
        int argc = 0;
        int toffset = TOFFSET_NONE;

        /*
         * Set up error logging.
         */
        if (log_init(progname)) {
            log_syserr("Couldn't initialize logging module");
            exit(1);
        }

        const char* pqfname = getQueuePath();

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
        spec.pattern = DEFAULT_PATTERN;

        memset(argv, 0, sizeof(argv));
        argv[0] = "pqact";
        argc++;

        {
        extern int optind;
        extern int opterr;
        extern char *optarg;
        int ch;
        int fterr;
        const char *conffilename = getPqsurfConfigPath();
        const char *datadir = getPqsurfDataDirPath();

        usePil = 1;
        opterr = 1;

        while ((ch = getopt(ac, av, "vxl:d:f:p:q:Q:o:i:a:t:")) != EOF)
                switch (ch) {
                case 'v':
                        if (!log_is_enabled_info)
                            (void)log_set_level(LOG_LEVEL_INFO);
                        argv[argc++] = "-v";
                        break;
                case 'x':
                        (void)log_set_level(LOG_LEVEL_DEBUG);
                        argv[argc++] = "-x";
                        break;
                case 'l':
                        argv[argc++] = "-l";
                        argv[argc++] = optarg;
                        if (log_set_destination(optarg)) {
                            log_syserr("Couldn't set logging destination to \"%s\"",
                                    optarg);
                            usage(progname);
                        }
                        break;
                case 'd':
                        datadir = optarg;
                        break;
                case 'f':
                        fterr = strfeedtypet(optarg, &spec.feedtype);
                        if(fterr != FEEDTYPE_OK)
                        {
                                fprintf(stderr, "%s: %s: \"%s\"\n",
                                        av[0], strfeederr(fterr), optarg);
                                usage(progname);        
                        }
                        argv[argc++] = "-f";
                        argv[argc++] = optarg;
                        break;
                case 'p':
                        spec.pattern = optarg;
                        /* compiled below */
                        break;
                case 'q':
                        pqfname = optarg;
                        break;
                case 'Q':
                        opqfname = optarg;
                        break;
                case 'o':
                        toffset = atoi(optarg);
                        if(toffset == 0 && *optarg != '0')
                        {
                                fprintf(stderr, "%s: invalid offset %s\n",
                                         av[0], optarg);
                                usage(av[0]);   
                        }
                        argv[argc++] = "-o";
                        argv[argc++] = optarg;
                        break;
                case 'i':
                        interval = atoi(optarg);
                        if(interval == 0 && *optarg != '0')
                        {
                                fprintf(stderr, "%s: invalid interval \"%s\"\n",
                                        av[0], optarg);
                                usage(av[0]);
                        }
                        /* N.B. -i just used for input queue. */
                        break;
                case 'a':
                        age = atof(optarg);
                        if(age < 0.)
                        {
                            (void) fprintf(stderr,
                                        "age (%s) must be non negative\n",
                                        optarg);
                                usage(av[0]);   
                        }
                        break;
                case 't':
                        /* pipe_timeo */
                        argv[argc++] = "-t";
                        argv[argc++] = optarg;
                        break;
                case '?':
                        usage(progname);
                        break;
                }

        setQueuePath(pqfname);

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

        if(ac - optind == 1)
                conffilename = av[optind];

        argv[argc++] = "-d";
        argv[argc++] = (char*)datadir;
        argv[argc++] = "-q";
        argv[argc++] = (char*)opqfname;
        argv[argc++] = (char*)conffilename;

        age *= 3600.;

        }

        if(toffset != TOFFSET_NONE)
        {
                clss.from.tv_sec -= toffset;
        }
        else
        {
                clss.from.tv_sec -= (age - interval);
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
         * Open the output product queue
         */
        status = pq_open(opqfname, PQ_DEFAULT, &opq);
        if(status)
        {
                if (PQ_CORRUPT == status) {
                    log_error_q("The output product-queue \"%s\" is inconsistent\n",
                            opqfname);
                }
                else {
                    log_add_errno(status, "pq_open failed: %s", opqfname);
                    log_flush_error();
                }
                exit(1);
        }


        act_pid = run_child(argc, argv);
        if(act_pid == (pid_t)-1)
                exit(1);

        /*
         * Open the input product queue
         */
        status = pq_open(pqfname, PQ_READONLY, &pq);
        if(status)
        {
                if (PQ_CORRUPT == status) {
                    log_error_q("The product-queue \"%s\" is inconsistent\n",
                            pqfname);
                }
                else {
                    log_add_errno(status, "pq_open failed: %s", pqfname);
                    log_flush_error();
                }
                exit(1);
        }
        if(toffset == TOFFSET_NONE)
        {
                /* Jump to the end of the queue */
                timestampt sav;
                sav = clss.from;
                clss.from = TS_ZERO;
                (void) pq_last(pq, &clss, NULL);
                clss.from = sav;
        }
        else
        {
                pq_cset(pq, &clss.from);
        }

        if(log_is_enabled_info)
        {
                char buf[1984];
                log_info_q("%s",
                         s_prod_class(buf, sizeof(buf), &clss));
        }

        while(exitIfDone(0))
        {
                if(stats_req)
                {
                        dump_stats();
                        stats_req = 0;
                }

                status = pq_sequence(pq, TV_GT, &clss, split_prod, NULL);

                switch(status) {
                case 0: /* no error */
                        continue; /* N.B., other cases sleep */
                case PQUEUE_END:
                        log_debug("surf: End of Queue");
                        break;
                case EAGAIN:
                case EACCES:
                        log_debug("Hit a lock");
                        break;
                default:
                        log_add_errno(status, "pq_sequence failed");
                        log_flush_error();
                        exit(1);
                        break;
                }

                if(interval == 0)
                {
                        break;
                }


                (void) expire(opq, interval, age);

                pq_suspend(interval);

                (void) reap_act(WNOHANG);
        }

        /*
         * TODO: how can we determine that pqact has finished
         *       the work in opq?
         */
        sleep(5);

        exit(0);
}
