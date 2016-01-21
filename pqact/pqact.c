/*
 *   Copyright 2012 University Corporation for Atmospheric Research
 *
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */

/* 
 * ldm server mainline program module
 */

#include <config.h>

#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <regex.h>
#ifdef HAVE_WAITPID
    #include <sys/wait.h>
#endif 
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#ifndef NO_WAITPID
#include <sys/wait.h>
#endif 
#include "ldm.h"
#include "error.h"
#include "globals.h"
#include "remote.h"
#include "atofeedt.h"
#include "pq.h"
#include "palt.h"
#include "ldmprint.h"
#include "filel.h" /* pipe_timeo */
#include "state.h"
#include "timestamp.h"
#include "log.h"
#include "RegularExpressions.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

static volatile sig_atomic_t hupped = 0;
static const char*           conffilename = 0;
static int                   shmid = -1;
static int                   semid = -1;
static key_t                 key;
static key_t                 semkey;
timestampt                   oldestCursor;
timestampt                   currentCursor;
int                          currentCursorSet = 0;
/**
 * The SIGHUP signal set.
 */
static sigset_t              hup_sigset;
/**
 * The previous SIGHUP action when this module's SIGHUP action is registered.
 */
static struct sigaction      prev_hup_sigaction;
/**
 * The SIGHUP action for this module.
 */
static struct sigaction      hup_sigaction;

#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 15
#endif
#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE ANY
#endif
#ifndef DEFAULT_PATTERN
#define DEFAULT_PATTERN ".*"
#endif

/*
 * Timeout used for PIPE actions,
 * referenced in filel.c
 */
#ifndef DEFAULT_PIPE_TIMEO
#define DEFAULT_PIPE_TIMEO 60
#endif /* !DEFAULT_PIPE_TIMEO */
int pipe_timeo = DEFAULT_PIPE_TIMEO;


/*
 * called at exit
 */
static void
cleanup(void)
{
    log_notice("Exiting");

    if (done) {
        /*
         * We are not in the interrupt context, so these can
         * be performed safely.
         */
        fl_closeAll();

        if (pq)
            (void)pq_close(pq);

        if (currentCursorSet) {
            timestampt  now;

            (void)set_timestamp(&now);
            log_notice("Behind by %g s", d_diff_timestamp(&now, &currentCursor));

            if (stateWrite(&currentCursor) < 0) {
                log_error("Couldn't save insertion-time of last processed "
                    "data-product");
            }
        }

        while (reap(-1, WNOHANG) > 0)
            /*EMPTY*/;
    }

    if(shmid != -1) {
        log_notice("Deleting shared segment.");
        shmctl(shmid, IPC_RMID, NULL);
    }
    if(semid != -1) {
        semctl(semid, 0, IPC_RMID);
    }

    (void)log_fini();
}


/*
 * called upon receipt of signals
 */
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
                hupped = 1;
                if (prev_hup_sigaction.sa_handler != SIG_DFL &&
                        prev_hup_sigaction.sa_handler != SIG_IGN) {
                    (void)sigaction(SIGHUP, &prev_hup_sigaction, NULL);
                    raise(SIGHUP);
                    (void)sigprocmask(SIG_UNBLOCK, &hup_sigset, NULL);
                    (void)sigaction(SIGHUP, &hup_sigaction, NULL);
                }
                return;
        case SIGINT :
                exit(0);
                /*NOTREACHED*/
        case SIGTERM :
                done = 1;
                return;
        case SIGUSR1 :
                /* TODO? stats */
                return;
        case SIGUSR2 :
                log_roll_level();
                return;
        case SIGALRM :
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
        (void) sigaction(SIGXFSZ, &sigact, NULL);       /* file too large */

        /* Handle these */
#ifdef SA_RESTART       /* SVR4, 4.3+ BSD */
        /* usually, restart system calls */
        sigact.sa_flags |= SA_RESTART;
        /*
         * NOTE: The OSF/1 operating system doesn't conform to the UNIX standard
         * in this regard: the SA_RESTART flag does not affect writes to regular
         * files or, apparently, pipes.  Consequently, interrupted writes must
         * be handled explicitly.  See the discussion of the SA_RESTART option
         * at http://www.opengroup.org/onlinepubs/007908799/xsh/sigaction.html
         */
#endif
        // SIGHUP is special because it's also used by `log2log4c.c`
        (void)sigemptyset(&hup_sigset);
        (void)sigaddset(&hup_sigset, SIGHUP);
        hup_sigaction.sa_mask = hup_sigset;
        hup_sigaction.sa_flags = SA_RESTART;
        hup_sigaction.sa_handler = signal_handler;
        (void) sigaction(SIGHUP, &hup_sigaction, &prev_hup_sigaction);

        sigact.sa_handler = signal_handler;
        (void) sigaction(SIGTERM, &sigact, NULL);
        (void) sigaction(SIGUSR1, &sigact, NULL);
        (void) sigaction(SIGUSR2, &sigact, NULL);
        (void) sigaction(SIGALRM, &sigact, NULL);

        /* Don't restart after interrupt */
        sigact.sa_flags = 0;
#ifdef SA_INTERRUPT     /* SunOS 4.x */
        sigact.sa_flags |= SA_INTERRUPT;
#endif
        (void) sigaction(SIGINT, &sigact, NULL);
}


static void
usage(
        const char* const av0)
{
        log_error("Usage: %s [options] [config_file]\t\nOptions:\n", av0);
        log_error("Options:");
        log_error(
                "\t-v           Log INFO-level messages, log each match "
                "(SIGUSR2 cycles)");
        log_error(
                "\t-x           Log DEBUG-level messages (SIGUSR2 cycles)");
        log_error(
                "\t-l logfile   Log to \"logfile\" (default: use system "
                "logging daemon)");
        log_error(
                "\t-d datadir   cd(1) to \"datadir\" before interpreting "
                "pathnames in configuration-file (default: \"%s\")",
                getPqactDataDirPath());
        log_error(
                "\t-q queue     Use product-queue \"queue\" (default: \"%s\")",
                getQueuePath());
        log_error(
                "\t-p pattern   Only process products matching \"pattern\" (default: \"%s\")", DEFAULT_PATTERN);
        log_error(
                "\t-f feedtype  Only process products from feed \"feedtype\" (default: %s)", s_feedtypet(DEFAULT_FEEDTYPE));
        log_error(
                "\t-i interval  Loop, polling every \"interval\" seconds (default: %d)", DEFAULT_INTERVAL);
        log_error(
                "\t-t timeo     Set write timeout for PIPE subprocs to \"timeo\" secs (default: %d)", DEFAULT_PIPE_TIMEO);
        log_error(
                "\t-o offset    Start with products arriving \"offset\" seconds before now (default: 0)");
        log_error(
                "\tconfig_file  Pathname of configuration-file (default: "
                "\"%s\")", getPqactConfigPath());
        exit(1);
        /*NOTREACHED*/
}


int
main(int ac, char *av[])
{
        const char*  pqfname;
        int          status = 0;
        char*        logfname = 0;
        /// Data directory, conffile paths may be relative
        const char*  datadir;
        int          interval = DEFAULT_INTERVAL;
        prod_spec    spec;
        prod_class_t clss;
        int          toffset = TOFFSET_NONE;
        int          loggingToStdErr = 0;
        unsigned     queue_size = 5000;
        const char*  progname = basename(av[0]);
        unsigned     logopts = LOG_CONS|LOG_PID;

        /*
         * Setup default logging before anything else.
         */
        (void)log_init(progname);

        pqfname = getQueuePath();
        conffilename = getPqactConfigPath();

        spec.feedtype = DEFAULT_FEEDTYPE;
        spec.pattern = DEFAULT_PATTERN;

        if(set_timestamp(&clss.from)) /* corrected by toffset below */
        {
                int errnum = errno;
                log_error("Couldn't set timestamp: %s", strerror(errnum));
                exit(1);
                /*NOTREACHED*/
        }
        clss.to = TS_ENDT;
        clss.psa.psa_len = 1;
        clss.psa.psa_val = &spec;

        /*
         * deal with the command line, set options
         */
        {
            extern int optind;
            extern int opterr;
            extern char *optarg;

            int ch;
            int fterr;

            opterr = 1;

            while ((ch = getopt(ac, av, "vxel:d:f:q:o:p:i:t:")) != EOF) {
                switch (ch) {
                case 'v':
                        (void)log_set_level(LOG_LEVEL_INFO);
                        break;
                case 'x':
                        (void)log_set_level(LOG_LEVEL_DEBUG);
                        break;
                case 'e':
                        key = ftok("/etc/rc.d/rc.local",'R');
                        semkey = ftok("/etc/rc.d/rc.local",'e');
                        shmid = shmget(key, sizeof(edex_message) * queue_size,
                                0666 | IPC_CREAT);
                        semid = semget(semkey, 2, 0666 | IPC_CREAT);
                        break;
                case 'l':
                        logfname = optarg;
                        (void)log_set_destination(logfname);
                        break;
                case 'd':
                        setPqactDataDirPath(optarg);
                        break;
                case 'f':
                        fterr = strfeedtypet(optarg, &spec.feedtype);
                        if(fterr != FEEDTYPE_OK)
                        {
                                log_error("Bad feedtype \"%s\", %s\n",
                                        optarg, strfeederr(fterr));
                                usage(progname);
                        }
                        break;
                case 'q':
                        setQueuePath(optarg);
                        break;
                case 'o':
                        toffset = atoi(optarg);
                        if(toffset == 0 && *optarg != '0')
                        {
                                log_error("invalid offset %s\n", optarg);
                                usage(progname);   
                        }
                        break;
                case 'i':
                        interval = atoi(optarg);
                        if(interval == 0 && *optarg != '0')
                        {
                                log_error("invalid interval %s\n", optarg);
                                usage(progname);   
                        }
                        break;
                case 't':
                        pipe_timeo = atoi(optarg);
                        if(pipe_timeo == 0 && *optarg != 0)
                        {
                                log_error("invalid pipe_timeo %s", optarg);
                                usage(progname);   
                        }
                        break;
                case 'p':
                        spec.pattern = optarg;
                        break;
                default:
                        usage(progname);
                        break;
                }
            }

            {
                int numOperands = ac - optind;

                if (1 < numOperands) {
                    log_error("Too many operands");
                    usage(progname);
                }
                else if (1 == numOperands) {
                    conffilename = av[optind];
                }
            }
        }

        datadir = getPqactDataDirPath();

        log_notice("Starting Up");

        if ('/' != conffilename[0]) {
            /*
             * The pathname of the configuration-file is relative. Convert it
             * to absolute so that it can be (re)read even if the current
             * working directory changes.
             */
#ifdef PATH_MAX
            char    buf[PATH_MAX];          /* includes NUL */
#else
            char    buf[_POSIX_PATH_MAX];   /* includes NUL */
#endif
            if (getcwd(buf, sizeof(buf)) == NULL) {
                log_syserr("Couldn't get current working directory");
                exit(1);
            }
            (void)strncat(buf, "/", sizeof(buf)-strlen(buf)-1);
            (void)strncat(buf, conffilename, sizeof(buf)-strlen(buf)-1);
            conffilename = strdup(buf);
            if (conffilename == NULL) {
                log_syserr("Couldn't duplicate string \"%s\"", buf);
                exit(1);
            }
        }

        /*
         * Initialze the previous-state module for this process.
         */
        if (stateInit(conffilename) < 0) {
            log_error("Couldn't initialize previous-state module");
            exit(EXIT_FAILURE);
            /*NOTREACHED*/
        }

        /*
         * The standard input stream is redirected to /dev/null because this
         * program doesn't use it and doing so prevents child processes
         * that mistakenly read from it from terminating abnormally.
         */
        if (NULL == freopen("/dev/null", "r", stdin))
        {
            err_log_and_free(
                ERR_NEW1(0, NULL,
                    "Couldn't redirect stdin to /dev/null: %s",
                    strerror(errno)),
                ERR_FAILURE);
            exit(EXIT_FAILURE);
            /*NOTREACHED*/
        }

        /*
         * The standard output stream is redirected to /dev/null because this
         * program doesn't use it and doing so prevents child processes
         * that mistakenly write to it from terminating abnormally.
         */
        if (NULL == freopen("/dev/null", "w", stdout))
        {
            err_log_and_free(
                ERR_NEW1(0, NULL,
                    "Couldn't redirect stdout to /dev/null: %s",
                    strerror(errno)),
                ERR_FAILURE);
            exit(EXIT_FAILURE);
            /*NOTREACHED*/
        }

        /*
         * Inform the "filel" module about the number of available file
         * descriptors.  File descriptors are reserved for stdin, stdout,
         * stderr, the product-queue, the configuration-file, and (possibly) 
         * logging.
         */
        if (0 != set_avail_fd_count(openMax() - 6))
        {
            log_error("Couldn't set number of available file-descriptors");
            log_notice("Exiting");
            exit(1);
            /*NOTREACHED*/
        }

        /*
         * Inform the "filel" module of the shared memory segment
         */
        if (shmid != -1 && semid != -1)
        {
            set_shared_space(shmid, semid, queue_size);
        }

        /*
         * Compile the pattern.
         */
        if (re_isPathological(spec.pattern))
        {
                log_error("Adjusting pathological regular-expression: \"%s\"",
                    spec.pattern);
                re_vetSpec(spec.pattern);
        }
        status = regcomp(&spec.rgx, spec.pattern, REG_EXTENDED|REG_NOSUB);
        if(status != 0)
        {
                log_error("Can't compile regular expression \"%s\"",
                        spec.pattern);
                log_notice("Exiting");
                exit(1);
                /*NOTREACHED*/
        }

        /*
         * register exit handler
         */
        if(atexit(cleanup) != 0)
        {
                log_syserr("atexit");
                log_notice("Exiting");
                exit(1);
                /*NOTREACHED*/
        }

        /*
         * set up signal handlers
         */
        set_sigactions();

        /*
         * Read in (compile) the configuration file.  We do this first so
         * its syntax may be checked without opening a product queue.
         */
        if ((status = readPatFile(conffilename)) < 0) {
                exit(1);
                /*NOTREACHED*/
        }
        else if (status == 0) {
            log_notice("Configuration-file \"%s\" has no entries. "
                "You should probably not start this program instead.",
                conffilename);
        }

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
                /*NOTREACHED*/
        }

        if(toffset != TOFFSET_NONE) {
            /*
             * Filter and queue position set by "toffset".
             */
            clss.from.tv_sec -= toffset;
            pq_cset(pq, &clss.from);
        }
        else {
            bool       startAtTailEnd = true;
            timestampt insertTime;

            clss.from = TS_ZERO;

            /*
             * Try getting the insertion-time of the last,
             * successfully-processed data-product from the previous session.
             */
            status = stateRead(&insertTime);

            if (status) {
                log_warning("Couldn't get insertion-time of last-processed "
                        "data-product from previous session");
            }
            else {
                timestampt now;
                (void)set_timestamp(&now);

                if (tvCmp(now, insertTime, <)) {
                    log_warning("Time of last-processed data-product from previous "
                            "session is in the future");
                }
                else {
                    char buf[80];
                    (void)strftime(buf, sizeof(buf), "%Y-%m-%d %T",
                        gmtime(&insertTime.tv_sec));
                    log_notice("Starting from insertion-time %s.%06lu UTC", buf,
                        (unsigned long)insertTime.tv_usec);

                    pq_cset(pq, &insertTime);
                    startAtTailEnd = false;
                }
            }

            if (startAtTailEnd) {
                log_notice("Starting at tail-end of product-queue");
                (void)pq_last(pq, &clss, NULL);
            }
        }

        if(log_is_enabled_info)
        {
                char buf[1984];
                log_info("%s", s_prod_class(buf, sizeof(buf), &clss));
        }

        /*
         * Change directories if datadir was specified
         */
        if(datadir != NULL && *datadir != 0) 
        {
                /* change to data directory */
                if (chdir(datadir) == -1)
                {
                        log_syserr("cannot chdir to %s", datadir);
                        exit(4);
                        /*NOTREACHED*/
                }
        }


        /*
         *  Do special pre main loop actions in pattern/action file
         *  N.B. Deprecate.
         */
        dummyprod("_BEGIN_");


        /*
         * Main loop
         */
        for (;;) {
            if (hupped) {
                log_notice("Rereading configuration file %s", conffilename);
                (void) readPatFile(conffilename);
                hupped = 0;
            }

            bool wasProcessed = false;
            status = pq_sequence(pq, TV_GT, &clss, processProduct,
                    &wasProcessed);

            if (status == 0) {
                /*
                 * No product-queue error.
                 */
                timestampt insertionTime, oldestInsertionTime;

                pq_ctimestamp(pq, &insertionTime);
                status = pq_getOldestCursor(pq, &oldestInsertionTime);

                if (status == 0 &&
                        tvEqual(oldestInsertionTime, insertionTime)) {
                    timestampt  now;
                    (void)set_timestamp(&now);
                    log_warning("Processed oldest product in queue: %g s",
                        d_diff_timestamp(&now, &currentCursor));
                }

                if (wasProcessed) {
                    /*
                     * The insertion-time of the last successfully-processed
                     * data-product is only set if it was successfully processed
                     * by all matching entries in order to allow re-processing
                     * of a partially processed data-product in the next session
                     * by a corrected action.
                     */
                    currentCursor = insertionTime;
                    currentCursorSet = 1;
                }

                (void)exitIfDone(0);
            }
            else {
                /*
                 * Product-queue error. Data-product wasn't processed.
                 */
                if (status == PQUEUE_END) {
                    log_debug("End of Queue");

                    if (interval == 0)
                        break;
                }
                else if (status == EAGAIN || status == EACCES) {
                    log_debug("Hit a lock");
                    /*
                     * Close the least recently used file descriptor.
                     */
                    fl_closeLru(FL_NOTRANSIENT);
                }
                else if (status == EDEADLK
#if defined(EDEADLOCK) && EDEADLOCK != EDEADLK
                    || status == EDEADLOCK
#endif
                ) {
                    log_syserr(NULL);
                    /*
                     * Close the least recently used file descriptor.
                     */
                    fl_closeLru(FL_NOTRANSIENT);
                }
                else {
                    log_error("pq_sequence failed: %s (errno = %d)",
                        strerror(status), status);
                    exit(1);
                    /*NOTREACHED*/
                }

                (void)pq_suspend(interval);
                (void)exitIfDone(0);
            }                           /* data-product not processed */

            /*
             * Perform a non-blocking sync on all open file descriptors.
             */
            fl_sync(-1, FALSE);

            /*
             * Wait on any children which might have terminated.
             */
            while (reap(-1, WNOHANG) > 0)
                /*EMPTY*/;
        }                               /* main loop */

        return 0;
}
