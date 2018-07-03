/**
 *   Copyright 2018, University Corporation for Atmospheric Research
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */

/* 
 *  Check a product-queue.
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
#include "atofeedt.h"
#include "globals.h"
#include "ldmprint.h"
#include "log.h"
#include "pq.h"
#include "md5.h"

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

static const char *pqfname;

static void
usage(const char *av0) /*  id string */
{
        (void)fprintf(stderr,
"Usage: %s [options]\n\tOptions:\n", av0);
        (void)fprintf(stderr,
"\t-F           Force. Set the writer-counter to zero "
"(creating it if necessary).\n");
        (void)fprintf(stderr,
"\t-v           Verbose\n");
        (void)fprintf(stderr,
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
        (void)fprintf(stderr,
"\t-q pqfname   (default \"%s\")\n", getDefaultQueuePath());
        (void)fprintf(stderr,
"Output defaults to standard output\n");
        exit(1);
}


static void
cleanup(void)
{
        log_notice_q("Exiting");
        log_fini();
}


/*
 * Set signal handling.
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
}


/*
 * Returns:
 *      0       Success.  Write-count of product-queue is zero.
 *      1       System failure.  See error-message.
 *      2       Product-queue doesn't support a writer-counter.  Not possible
 *              if "-F" option used.
 *      3       Write-count of product-queue is greater than zero.  Not possible
 *              if "-F" option used.
 *      4       The product-queue is internally inconsistent.
 */
int main(int ac, char *av[])
{
        const char *progname = basename(av[0]);
        int status = 0;
        unsigned write_count;
        int force = 0;

        /*
         * Set up error logging.
         */
        (void)log_init(progname);

        {
            extern int opterr;
            extern char *optarg;
            int ch;

            pqfname = getQueuePath();
            opterr = 1;

            while ((ch = getopt(ac, av, "Fvxl:q:")) != EOF)
                    switch (ch) {
                    case 'F':
                            force = 1;
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
                    case 'q':
                            pqfname = optarg;
                            break;
                    case '?':
                            usage(progname);
                            break;
                    }
        }

        setQueuePath(pqfname);
        log_notice_q("Starting Up (%d)", getpgrp());

        /*
         * register exit handler
         */
        if(atexit(cleanup) != 0)
        {
                log_syserr_q("atexit");
                return 1;
        }

        /*
         * set up signal handlers
         */
        set_sigactions();

        if (force) {
            /*
             * Add writer-counter capability to the file, if necessary, and set
             * the writer-counter to zero.
             */
            status = pq_clear_write_count(pqfname);
            if (status) {
                if (PQ_CORRUPT == status) {
                    log_error_q("The product-queue \"%s\" is inconsistent", pqfname);
                    return 4;
                }
                else {
                    log_error_q("pq_clear_write_count() failure: %s: %s",
                            pqfname, strerror(status));
                    return 1;
                }
            }
            write_count = 0;
        }
        else {
            /*
             * Get the writer-counter of the product-queue.
             */
            status = pq_get_write_count(pqfname, &write_count);
            if (status) {
                if (ENOSYS == status) {
                    log_error_q("Product-queue \"%s\" doesn't have a writer-counter",
                        pqfname);
                    return 2;
                }
                else if (PQ_CORRUPT == status) {
                    log_error_q("Product-queue \"%s\" is inconsistent", pqfname);
                    return 4;
                }
                else {
                    log_error_q("pq_get_write_count() failure: %s: %s",
                        pqfname, strerror(status));
                    return 1;
                }
            }
        }

        log_info_q("The writer-counter of the product-queue is %u", write_count);

        return write_count == 0 ? 0 : 3;
}
