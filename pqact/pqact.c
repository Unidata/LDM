/**
 * Process data-products from the LDM product-queue.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

/* 
 * ldm server mainline program module
 */

#include <config.h>

#include <fcntl.h>
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
#include "ldmfork.h"
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

/**
 * Configures the standard I/O file descriptors for subsequent execution of
 * child processes. The standard input, output, and error file descriptors are
 * redirected to `/dev/null` if they are closed to prevent child processes that
 * mistakenly write to them from misbehaving.
 *
 * @retval  0  Success
 * @retval -1  Failure. log_add() called.
 */
static int configure_stdio_file_descriptors(void)
{
    int status = open_on_dev_null_if_closed(STDIN_FILENO, O_RDONLY);
    if (status == 0) {
        status = open_on_dev_null_if_closed(STDOUT_FILENO, O_WRONLY);
        if (status == 0)
            status = open_on_dev_null_if_closed(STDERR_FILENO, O_RDWR);
    }
    return status;
}


/*
 * called at exit
 */
static void
cleanup(void)
{
    log_notice_q("Exiting");

    if (done) {
        /*
         * This function wasn't called by a signal handler, so these can be
         * performed safely.
         */
        fl_closeAll();

        if (pq)
            (void)pq_close(pq);

        if (tvIsNone(palt_last_insertion)) {
            log_notice("No product was processed");
        }
        else {
            timestampt  now;

            (void)set_timestamp(&now);
            log_notice("Last product processed was inserted %g s ago",
                    d_diff_timestamp(&now, &palt_last_insertion));

            if (stateWrite(&palt_last_insertion) < 0) {
                log_add("Couldn't save insertion-time of last processed "
                    "data-product");
                log_flush_error();
            }
        }

        while (reap(-1, WNOHANG) > 0)
            /*EMPTY*/;
    }

    if(shmid != -1) {
        log_notice_q("Deleting shared segment.");
        shmctl(shmid, IPC_RMID, NULL);
    }
    if(semid != -1) {
        semctl(semid, 0, IPC_RMID);
    }

    log_fini();
}


/*
 * called upon receipt of signals
 */
static void
signal_handler(int sig)
{
    switch(sig) {
    case SIGHUP :
            hupped = 1;
            return;
    case SIGINT :
            exit(0);
            /*NOTREACHED*/
    case SIGTERM :
            done = 1;
            return;
    case SIGUSR1 :
            log_refresh();
            return;
    case SIGUSR2 :
            log_roll_level();
            return;
    case SIGALRM:
    	    return; // Failsafe. Default action terminates process
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
    (void)sigaction(SIGPIPE, &sigact, NULL);
    (void)sigaction(SIGXFSZ, &sigact, NULL); // File is too large
    /*
     * The default SIGALRM action is to terminate the process. pq(3) and pbuf(3)
     * explicitly handle SIGALRM.
     */
    (void)sigaction(SIGALRM, &sigact, NULL);

    /* Handle the following */
    sigact.sa_handler = signal_handler;

    /* Don't restart the following */
    (void)sigaction(SIGINT, &sigact, NULL);

    /* Restart the following */
    sigact.sa_flags |= SA_RESTART;
    /*
     * NOTE: The OSF/1 operating system doesn't conform to the UNIX standard
     * in this regard: the SA_RESTART flag does not affect writes to regular
     * files or, apparently, pipes.  Consequently, interrupted writes must
     * be handled explicitly.  See the discussion of the SA_RESTART option
     * at http://www.opengroup.org/onlinepubs/007908799/xsh/sigaction.html
     */
    (void)sigaction(SIGHUP,  &sigact, NULL); // Sets `hupped`
    (void)sigaction(SIGTERM, &sigact, NULL); // Sets `done`
    (void)sigaction(SIGUSR1, &sigact, NULL); // Calls log_refresh()
    (void)sigaction(SIGUSR2, &sigact, NULL); // Calls log_roll_level()

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGPIPE);
    (void)sigaddset(&sigset, SIGXFSZ);
    (void)sigaddset(&sigset, SIGHUP);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}


static void
usage(
        const char* const av0)
{
        log_error_q("Usage: %s [options] [config_file]\t\nOptions:\n", av0);
        log_error_q("Options:");
        log_error_q(
"\t-v           Log INFO-level messages, log each match (SIGUSR2 cycles)");
        log_error_q(
"\t-x           Log DEBUG-level messages (SIGUSR2 cycles)");
        log_error_q(
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
        log_error_q(
"\t-d datadir   cd(1) to \"datadir\" before interpreting pathnames in\n"
"\t             configuration-file (default: \"%s\")", getPqactDataDirPath());
        log_error_q(
"\t-q queue     Use product-queue \"queue\" (default: \"%s\")",
                getDefaultQueuePath());
        log_error_q(
"\t-p pattern   Only process products matching \"pattern\" (default: \"%s\")",
                DEFAULT_PATTERN);
        log_error_q(
"\t-f feedtype  Only process products from feed \"feedtype\" (default: %s)",
                s_feedtypet(DEFAULT_FEEDTYPE));
        log_error_q(
"\t-i interval  Loop, polling every \"interval\" seconds (default: %d)",
                DEFAULT_INTERVAL);
        log_error_q(
"\t-t timeo     Set write timeout for PIPE subprocs to \"timeo\" secs (default: %d)",
                DEFAULT_PIPE_TIMEO);
        log_error_q(
"\t-o offset    Start with products arriving \"offset\" seconds before now (default: 0)");
        log_error_q(
"\tconfig_file  Pathname of configuration-file (default: " "\"%s\")",
                getPqactConfigPath());
        exit(EXIT_FAILURE);
        /*NOTREACHED*/
}


int
main(int ac, char *av[])
{
        int          status = 0;
        /// Data directory, conffile paths may be relative
        const char*  datadir;
        int          interval = DEFAULT_INTERVAL;
        prod_spec    spec;
        prod_class_t clss;
        int          toffset = TOFFSET_NONE;
        unsigned     queue_size = 5000;
        const char*  progname = basename(av[0]);

        /*
         * Setup default logging before anything else.
         */
        if (log_init(progname)) {
            log_syserr("Couldn't initialize logging module");
            exit(1);
        }

        spec.feedtype = DEFAULT_FEEDTYPE;
        spec.pattern = DEFAULT_PATTERN;

        if(set_timestamp(&clss.from)) /* corrected by toffset below */
        {
                int errnum = errno;
                log_error_q("Couldn't set timestamp: %s", strerror(errnum));
                exit(EXIT_FAILURE);
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
                        if (!log_is_enabled_info)
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
                        if (log_set_destination(optarg)) {
                            log_syserr("Couldn't set logging destination to \"%s\"",
                                    optarg);
                            usage(progname);
                        }
                        break;
                case 'd':
                        setPqactDataDirPath(optarg);
                        break;
                case 'f':
                        fterr = strfeedtypet(optarg, &spec.feedtype);
                        if(fterr != FEEDTYPE_OK)
                        {
                                log_error_q("Bad feedtype \"%s\", %s\n",
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
                                log_error_q("invalid offset %s\n", optarg);
                                usage(progname);   
                        }
                        break;
                case 'i':
                        interval = atoi(optarg);
                        if(interval == 0 && *optarg != '0')
                        {
                                log_error_q("invalid interval %s\n", optarg);
                                usage(progname);   
                        }
                        break;
                case 't':
                        pipe_timeo = atoi(optarg);
                        if(pipe_timeo == 0 && *optarg != 0)
                        {
                                log_error_q("invalid pipe_timeo %s", optarg);
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

            datadir = getPqactDataDirPath();

            {
                int numOperands = ac - optind;

                if (1 < numOperands) {
                    log_error_q("Too many operands");
                    usage(progname);
                }
                else {
                    conffilename = (1 == numOperands)
                            ? av[optind]
                            : getPqactConfigPath();
                }
            }
        }

        const char* const pqfname = getQueuePath();

        {
            char cmdBuf[LINE_MAX];
            log_notice("Starting Up {cmd: \"%s\"}",
                    ldm_formatCmd(cmdBuf, sizeof(cmdBuf), ac, (const char**)av));
        }

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
                exit(EXIT_FAILURE);
            }
            (void)strncat(buf, "/", sizeof(buf)-strlen(buf)-1);
            (void)strncat(buf, conffilename, sizeof(buf)-strlen(buf)-1);
            conffilename = strdup(buf);
            if (conffilename == NULL) {
                log_syserr("Couldn't duplicate string \"%s\"", buf);
                exit(EXIT_FAILURE);
            }
        }

        /*
         * Initialize the previous-state module for this process.
         */
        if (stateInit(conffilename) < 0) {
            log_error_q("Couldn't initialize previous-state module");
            exit(EXIT_FAILURE);
            /*NOTREACHED*/
        }

        /*
         * Configure the standard I/O streams for execution of child processes.
         */
        if (configure_stdio_file_descriptors()) {
            log_error_q("Couldn't configure standard I/O streams for execution "
                    "of child processes");
            exit(EXIT_FAILURE);
        }

        /*
         * Inform the "filel" module about the number of available file
         * descriptors.  File descriptors are reserved for stdin, stdout,
         * stderr, the product-queue, the configuration-file, and (possibly) 
         * logging.
         */
        if (0 != set_avail_fd_count(openMax() - 6))
        {
            log_error_q("Couldn't set number of available file-descriptors");
            log_notice_q("Exiting");
            exit(EXIT_FAILURE);
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
                log_error_q("Adjusting pathological regular-expression: \"%s\"",
                    spec.pattern);
                re_vetSpec(spec.pattern);
        }
        status = regcomp(&spec.rgx, spec.pattern, REG_EXTENDED|REG_NOSUB);
        if(status != 0)
        {
                log_error_q("Can't compile regular expression \"%s\"",
                        spec.pattern);
                log_notice_q("Exiting");
                exit(EXIT_FAILURE);
                /*NOTREACHED*/
        }

        /*
         * register exit handler
         */
        if(atexit(cleanup) != 0)
        {
                log_syserr("atexit");
                log_notice_q("Exiting");
                exit(EXIT_FAILURE);
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
                exit(EXIT_FAILURE);
                /*NOTREACHED*/
        }
        else if (status == 0) {
            log_notice_q("Configuration-file \"%s\" has no entries. "
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
                    log_error_q("The product-queue \"%s\" is inconsistent\n",
                            pqfname);
                }
                else {
                    log_error_q("pq_open failed: %s: %s\n",
                            pqfname, strerror(status));
                }
                exit(EXIT_FAILURE);
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
                log_warning_q("Couldn't get insertion-time of last-processed "
                        "data-product from previous session");
            }
            else {
                timestampt now;
                (void)set_timestamp(&now);

                if (tvCmp(now, insertTime, <)) {
                    log_warning_q("Time of last-processed data-product from previous "
                            "session is in the future");
                }
                else {
                    char buf[80];
                    (void)strftime(buf, sizeof(buf), "%Y-%m-%d %T",
                        gmtime(&insertTime.tv_sec));
                    log_notice_q("Starting from insertion-time %s.%06ld UTC", buf,
                        (long)insertTime.tv_usec);

                    pq_cset(pq, &insertTime);
                    startAtTailEnd = false;
                }
            }

            if (startAtTailEnd) {
                log_notice_q("Starting at tail-end of product-queue");
                (void)pq_last(pq, &clss, NULL);
            }
        }

        if(log_is_enabled_info)
        {
                char buf[1984];
                log_info_q("%s", s_prod_class(buf, sizeof(buf), &clss));
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
                log_notice_q("Rereading configuration file %s", conffilename);
                (void) readPatFile(conffilename);
                hupped = 0;
            }

#if 0
            status = pq_sequence(pq, TV_GT, &clss, processProduct,
                    &palt_processing_error);
#else
            status = pq_next(pq, false, &clss, processProduct, false, NULL);
#endif

            if (status) {
                /*
                 * No data-product was processed.
                 */
                if (status == PQUEUE_END) {
                    log_debug("End of Queue");

                    if (interval == 0)
                        break;

                    /*
                     * Perform a non-blocking sync on all open file descriptors.
                     */
                    fl_sync(FALSE);
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
                    log_add_syserr(NULL);
                    log_flush_error();
                    /*
                     * Close the least recently used file descriptor.
                     */
                    fl_closeLru(FL_NOTRANSIENT);
                }
                else {
                    log_add("pq_next() failure: %s (errno = %d)",
                        strerror(status), status);
                    log_flush_error();
                    exit(EXIT_FAILURE);
                    /*NOTREACHED*/
                }

                (void)pq_suspend(interval);
            }                           /* No data-product processed */

            (void)exitIfDone(0);

            /*
             * Wait on any children which might have terminated.
             */
            while (reap(-1, WNOHANG) > 0)
                /*EMPTY*/;
        }                               /* main loop */

        return 0;
}
