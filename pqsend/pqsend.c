/**
 * Sends the contents of a product-queue to an LDM.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
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
#include <regex.h>

#include "exitStatus.h"
#include "ldm.h"
#include "atofeedt.h"
#include "ldmprint.h"
#include "globals.h"
#include "remote.h"
#include "inetutil.h"
#include "log.h"
#include "LdmProxy.h"
#include "log.h"
#include "pq.h"
#include "prod_class.h"
#include "RegularExpressions.h"
#include "timer.h"

#ifdef NO_ATEXIT
#include "atexit.h"
#endif

#ifndef DEFAULT_TIMEOUT
#define DEFAULT_TIMEOUT 25
#endif
#ifndef DEFAULT_INTERVAL
#define DEFAULT_INTERVAL 15
#endif
#ifndef DEFAULT_TOTALTIMEOUT
#define DEFAULT_TOTALTIMEOUT 3600 /* give up after an hour */
#endif
#ifndef DEFAULT_FEEDTYPE
#define DEFAULT_FEEDTYPE ANY
#endif

static const char*           progname;
static volatile sig_atomic_t stats_req = 0;
static LdmProxy*             ldmProxy = NULL;
static int                   totalTimeo = DEFAULT_TOTALTIMEOUT;
static int                   sendStatus;
static const char*           pqfname;
static prod_spec             offerProdSpec;
static timestampt            timeOffset;
static unsigned              rpcTimeout = DEFAULT_TIMEOUT;
static const char*           remote = NULL; /* LDM hostname */
static prod_class_t          offer;
static prod_class_t*         want;
static int                   coupledTimes = 1;

struct sendstats {
    timestampt starttime; /* stats start time */
    int nprods; /* number of products sent */
    int nconnects; /* number of connects */
    int ndisco; /* number of disconnects */
    double downtime; /* accumulated disconnect time */
    timestampt last_disco; /* time of last disconnect */
    double last_downtime; /* length of last disconnect */
#define INIT_MIN_LATENCY 2147483647.
    double min_latency; /* min(shipped_from_here - info.arrival) */
    double max_latency; /* max(shipped_from_here - info.arrival) */
};
typedef struct sendstats sendstats;
static sendstats stats;

static void update_last_downtime(const timestampt *nowp) {
    /*
     * Update last_downtime
     */
    stats.last_downtime = d_diff_timestamp(nowp, &stats.last_disco);
    log_debug("last_downtime %10.3f", stats.last_downtime);
}

static void dump_stats(const sendstats *stp) {
    char cp[24];

    sprint_timestampt(cp, sizeof(cp), &stp->starttime);
    log_notice_q("> Up since:          %s", cp);

    if (stp->nconnects <= 0) {
        log_notice_q("> Never connected");
    } else {
        sprint_timestampt(cp, sizeof(cp), &stp->last_disco);
        if (stp->last_downtime != 0.) {
            log_notice_q(">  last disconnect:  %s for %10.3f seconds", cp,
                    stp->last_downtime);
        } else {
            log_notice_q(">  last disconnect:  %s", cp);
        }
        if (stp->nprods) {
            log_notice_q(">     nprods min_latency max_latency");
            log_notice_q("> %10d  %10.3f  %10.3f", stp->nprods,
                    stp->min_latency, stp->max_latency);
        } else {
            log_notice_q(">     nprods");
            log_notice_q("> %10d", stp->nprods);
        }
        log_notice_q(">  nconnects      ndisco  secs_disco");
        log_notice_q("> %10d  %10d  %10.3f", stp->nconnects, stp->ndisco,
                stp->downtime);
    }
}

/**
 * Prints a usage message at logging level NOTICE.
 * @param[in] av0  Name of the program
 */
static void printUsage(const char* const av0)
{
    log_add(
"Usage:\n"
"    %s [-vx] [-l logfile] [-f feedtype] [-p pattern] [-t timeout] \\\n"
"        [-q queue] [-d] [-T totalTimeo] [-o offset] [-i interval] [-h host]\n"
"Where:\n"
"    -d            Decouple the \"-T\" and \"-o\" options; otherwise, the\n"
"                  time-offset value can't be greater than the total time-out\n"
"                  value and the product-queue cursor will never be earlier\n"
"                  than the total time-out ago.\n"
"    -f feedpat    Send products whose feedtype matches \"feedpat\". Default\n"
"                  is \"%s\".\n"
"    -h host       Send to the LDM on \"host\". Default is \"localhost\".\n"
"    -i interval   Poll the product-queue every \"interval\" seconds after\n"
"                  reaching its end. Default is %u. \"0\" means execute this\n"
"                  program only once.\n"
"    -l dest       Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"                  (standard error), or file `dest`. Default is \"%s\"\n"
"    -o offset     Send products that were inserted into the queue no earlier\n"
"                  than \"offset\" seconds ago. The default includes the \n"
"                  oldest product in the queue if \"-d\" is specified;\n"
"                  otherwise, the default is the value of the \"-T\" option.\n"
"    -p pattern    Send products whose product-identifier matches \"pattern\"\n"
"                  Default is \".*\". May be modified by receiving LDM.\n"
"    -q queue      Use \"queue\" as the product-queue. Default is\n"
"                  \"%s\".\n"
"    -T totalTimeo Total time-out in seconds. Terminate after executing for\n"
"                  this much time. Default is %u.\n"
"    -t timeout    Timeout in seconds for RPC messages. Default is %u.\n"
"    -v            Verbose-level logging. Log each product sent.\n"
"    -x            Debug-level logging.\n", progname,
            s_feedtypet(DEFAULT_FEEDTYPE), DEFAULT_INTERVAL,
            log_get_default_destination(), getDefaultQueuePath(),
            DEFAULT_TOTALTIMEOUT, DEFAULT_TIMEOUT);
    log_level_t level = log_get_level();
    (void)log_set_level(LOG_LEVEL_NOTICE);
    log_flush_notice();
    log_set_level(level);
}

static void cleanup(void) {
    log_notice_q("Exiting");

    lp_free(ldmProxy);
    ldmProxy = NULL;

    (void) pq_close(pq);
    pq = NULL;

    stats.downtime += stats.last_downtime;

    dump_stats(&stats);

    log_fini();
}

static void signal_handler(int sig) {
    switch (sig) {
    case SIGINT:
        exit(1);
    case SIGTERM:
        done = 1;
        return;
    case SIGUSR1:
        log_refresh();
        stats_req = 1;
        return;
    case SIGUSR2:
        log_roll_level();
        return;
    }
}

/*
 * Register the signal_handler
 */
static void set_sigactions(void) {
    struct sigaction sigact;
    (void) sigemptyset(&sigact.sa_mask);
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

/*
 * Sends a data-product to the LDM. Called by the function pq_sequence().
 *
 * To avoid conflict with the return-values of pq_sequence(), this function
 * always returns zero and sets "sendStatus" to the actual status:
 *      CONNECTION_TIMEDOUT     The connection timed-out. "log_add()" called.
 *      CONNECTION_ABORTED      The connection failed for a reason other than a
 *                              time-out). "log_add()" called.
 *
 * Arguments:
 *      infop           The data-product's metadata.
 *      datap           The data-product's data.
 *      xprod           XDR-encoded version of the data-product.
 *      size            Size of "xprod" in bytes.
 *      arg             Ignored.
 *
 * Returns:
 *      0               Success.
 */
/*ARGSUSED*/
static int mySend(const prod_info* infop, const void* datap, void* xprod,
        size_t size, void* arg) {
    if (!prodInClass(want, infop)) {
        log_info_q("%s doesn't want %s", lp_host(ldmProxy),
                s_prod_info(NULL, 0, infop,
                        log_is_enabled_debug));
    } else {
        product product;

        product.info = *infop;
        product.data = (void*) datap; /* safe to remove "const" */

        sendStatus = lp_send(ldmProxy, &product);

        if (0 != sendStatus) {
            if (LP_UNWANTED == sendStatus) {
                if (log_is_enabled_info)
                    log_info_q(" dup: %s",
                            s_prod_info(NULL, 0, infop,
                                    log_is_enabled_debug));
                sendStatus = 0;
            } else {
                sendStatus =
                        (LP_TIMEDOUT == sendStatus) ?
                                CONNECTION_TIMEDOUT : CONNECTION_ABORTED;
            }
        } else {
            timestampt now;
            double latency;

            if (log_is_enabled_info)
                log_info_q("%s",
                        s_prod_info(NULL, 0, infop,
                                log_is_enabled_debug));

            set_timestamp(&now);
            latency = d_diff_timestamp(&now, &infop->arrival);
            stats.nprods++;

            if (latency < stats.min_latency)
                stats.min_latency = latency;
            if (latency > stats.max_latency)
                stats.max_latency = latency;
        }
    }

    return 0;
}

/*
 * Gets the execution configuration. Sets static parameters.
 *
 * Arguments:
 *      ac                      The number of arguments.
 *      av                      The argument strings.
 * Returns:
 *      0                       Success.
 *      SYSTEM_ERROR            O/S failure. "log_add()" called.
 *      INVOCATION_ERROR        User-error on command-line. "log_add()"
 *                              called.
 */
static int getConfiguration(int ac, char* const * const av) {
    int status = 0; /* success */
    extern int opterr;
    extern char* optarg;
    int ch;
    int fterr;

    progname = basename(av[0]);
    pqfname = getQueuePath();
    timeOffset = TS_NONE;
    interval = DEFAULT_INTERVAL;
    remote = "localhost";

    /* Initialize statistics */
    if (set_timestamp(&stats.starttime) != ENOERR) {
        log_add_syserr("Couldn't set timestamp");
        log_flush_error();
        status = SYSTEM_ERROR;
    } else {
        stats.last_disco = stats.starttime;
        stats.min_latency = INIT_MIN_LATENCY;
        stats.last_downtime = 0.;

        offer.from = TS_ZERO;
        offer.to = TS_ENDT;
        offer.psa.psa_len = 1;
        offer.psa.psa_val = &offerProdSpec;
        offerProdSpec.feedtype = ANY;
        offerProdSpec.pattern = ".*";

        /* If called as something other than "pqsend", use it as the remote */
        if (strcmp(progname, "pqsend") != 0)
            remote = progname;

        opterr = 1;

        while (0 == status
                && (ch = getopt(ac, av, "df:h:i:l:o:p:q:T:t:vx")) != EOF) {
            switch (ch) {
            case 'd': {
                coupledTimes = 0;
                break;
            }
            case 'f':
                fterr = strfeedtypet(optarg, &offerProdSpec.feedtype);
                if (fterr != FEEDTYPE_OK) {
                    log_add("Bad feedtype: \"%s\"; %s", optarg,
                            strfeederr(fterr));
                    status = INVOCATION_ERROR;
                }
                break;
            case 'h':
                remote = optarg;
                break;
            case 'i':
                interval = (unsigned) atoi(optarg);
                if (interval == 0 && *optarg != '0') {
                    log_add("Invalid interval: \"%s\"", optarg);
                    status = INVOCATION_ERROR;
                }
                break;
            case 'l':
                (void) log_set_destination(optarg);
                break;
            case 'o':
                timeOffset.tv_sec = atoi(optarg);
                if (timeOffset.tv_sec == 0 && *optarg != '0') {
                    log_add("Invalid offset: \"%s\"", optarg);
                    status = INVOCATION_ERROR;
                }
                timeOffset.tv_usec = 0;
                break;
            case 'p':
                offerProdSpec.pattern = optarg;
                /* compiled below */
                break;
            case 'q':
                pqfname = optarg;
                setQueuePath(optarg);
                break;
            case 'T':
                totalTimeo = atoi(optarg);
                if (totalTimeo == 0) {
                    log_add("Invalid Total timeout: \"%s\"", optarg);
                    status = INVOCATION_ERROR;
                }
                break;
            case 't':
                rpcTimeout = (unsigned) atoi(optarg);
                if (rpcTimeout == 0) {
                    log_add("Invalid timeout: \"%s\"", optarg);
                    status = INVOCATION_ERROR;
                }
                break;
            case 'v':
                if (!log_is_enabled_info)
                    (void) log_set_level(LOG_LEVEL_INFO);
                break;
            case 'x':
                (void) log_set_level(LOG_LEVEL_DEBUG);
                break;
            default:
                status = INVOCATION_ERROR;
                break;
            }
        } /* "getopt()" loop */

        if (0 == status) {
            if ((2 * rpcTimeout) >= totalTimeo) {
                log_add("Total timeout %u too small for RPC timeout %u",
                        totalTimeo, rpcTimeout);
                status = INVOCATION_ERROR;
            } else {
                if (coupledTimes && timeOffset.tv_sec > totalTimeo) {
                    log_add("Total timeout %u too small for time-offset %ld",
                            totalTimeo, (long) timeOffset.tv_sec);
                    status = INVOCATION_ERROR;
                } else {
                    if (coupledTimes) {
                        if (tvIsNone(timeOffset)) {
                            timeOffset.tv_sec = totalTimeo;
                            timeOffset.tv_usec = 0;
                        }
                    } else {
                        if (tvIsNone(timeOffset)) {
                            offer.from = TS_ZERO;
                        } else {
                            timestampt now;

                            (void) set_timestamp(&now);
                            offer.from = diff_timestamp(&now, &timeOffset);
                        }
                    }

                    if (re_isPathological(offerProdSpec.pattern)) {
                        log_add("Adjusting pathological regular-expression: "
                                "\"%s\"", offerProdSpec.pattern);
                        (void) re_vetSpec(offerProdSpec.pattern);
                    }
                    status = regcomp(&offerProdSpec.rgx, offerProdSpec.pattern,
                    REG_EXTENDED | REG_NOSUB);
                    if (status != 0) {
                        log_add("Bad regular expression: \"%s\"",
                                offerProdSpec.pattern);
                        status = INVOCATION_ERROR;
                    }
                }
            }
        }
    }

    return status;
}

/*
 * Connects to the LDM and transfers data-products.
 *
 * Returns:
 *      0                       Success. No more data-products to send.
 *      CONNECTION_TIMEDOUT     The connection attempt timed-out. "log_add()"
 *                              called.
 *      CONNECTION_ABORTED      The connection attempt failed for a reason
 *                              other than a time-out). "log_add()" called.
 *      PQ_ERROR                I/O error with the product-queue. "log_add()"
 *                              called.
 *      SYSTEM_ERROR            O/S error. "log_add()" called.
 */
static int executeConnection(void) {
    int status;
    CLIENT* clnt = NULL;
    ErrorObj* error;
    timestampt now;

    if (stats_req) {
        dump_stats(&stats);
        stats_req = 0;
    }

    (void) set_timestamp(&now);

    /* Offer what we can */
    if (coupledTimes)
        offer.from = diff_timestamp(&now, &timeOffset);

    /* Connect to the LDM. */
    exitIfDone(INTERRUPTED);
    status = lp_new(remote, &ldmProxy);

    if (0 != status) {
        if (LP_TIMEDOUT == status) {
            status = CONNECTION_TIMEDOUT;
        } else if (LP_SYSTEM == status) {
            status = SYSTEM_ERROR;
        } else {
            status = CONNECTION_ABORTED;
        }
    } else {
        /* This process is connected to the remote host. */
        stats.nconnects++;
        (void) set_timestamp(&now);
        update_last_downtime(&now);
        stats.downtime += stats.last_downtime;

        /* Check totalTimeo */
        if (coupledTimes) {
            if (d_diff_timestamp(&now, &offer.from) > totalTimeo) {
                offer.from = now;
                offer.from.tv_sec -= totalTimeo;
            }
            pq_cset(pq, &offer.from);
        }

        exitIfDone(INTERRUPTED);
        status = lp_hiya(ldmProxy, &offer, &want);

        if (0 != status) {
            if (LP_TIMEDOUT == status) {
                status = CONNECTION_TIMEDOUT;
            } else {
                status = CONNECTION_ABORTED;
            }
        } else {
            /* Loop over data-products to be sent. */
            for (;;) {
                exitIfDone(INTERRUPTED);
                status = pq_sequence(pq, TV_GT, &offer, mySend, NULL);

                if (0 != sendStatus) {
                    /* mySend() encountered a problem */
                    break;
                } /* mySend() error */
                else if (0 != status) {
                    /* pq_sequence() encountered a problem */
                    if (PQUEUE_END == status) {
                        log_debug("End of Queue");

                        /* Flush the connection. */
                        status = lp_flush(ldmProxy);
                        if (0 != status) {
                            status =
                                    (LP_TIMEDOUT == status) ?
                                            CONNECTION_TIMEDOUT :
                                            CONNECTION_ABORTED;
                            break;
                        }

                        if (interval == 0)
                            break; /* one-time execution */

                        /* Wait for more products to send. */
                        exitIfDone(INTERRUPTED);
                        pq_suspend(interval);
                    } else if (EAGAIN == status || EACCES == status) {
                        log_debug("Hit a lock");
                    } else if (EIO == status) {
                        log_add_syserr("Product-queue I/O error");
                        log_flush_error();
                        status = PQ_ERROR;
                        break;
                    } else {
                        log_add_syserr("Unexpected pq_sequence() return: %d",
                                status);
                        log_flush_error();
                        status = PQ_ERROR;
                        break;
                    }
                } /* pq_sequence() error */
            } /* data-product loop */
        } /* successful HIYA */

        stats.ndisco++;
        (void) set_timestamp(&stats.last_disco);

        lp_free(ldmProxy);
        ldmProxy = NULL;
    } /* "ldmProxy" allocated */

    return status;
}

/*
 * Executes this program.
 *
 * Returns:
 *      0                       Success.
 *      SYSTEM_ERROR            O/S failure. "log_add()" called.
 *      PQ_ERROR                Product-queue I/O failure. "log_add()" called.
 *      SESSION_TIMEDOUT        The time-limit on the session was reached.
 *                              "log_add()" called.
 *      INTERRUPTED             The process received a SIGTERM.
 */
static int execute(void) {
    int status;

    /*
     * N.B. log ident is the remote
     */
    (void) log_set_id(remote);
    log_notice_q("Starting Up (%d)", getpgrp());

    /*
     * Register exit handler
     */
    if (atexit(cleanup) != 0) {
        log_syserr("atexit");
        status = SYSTEM_ERROR;
    } else {
        /*
         * Set up signal handlers
         */
        set_sigactions();

        /*
         * Open product queue
         */
        status = pq_open(pqfname, PQ_READONLY, &pq);
        if (status) {
            if (PQ_CORRUPT == status) {
                log_error_q("The product-queue \"%s\" is inconsistent\n",
                        pqfname);
            } else {
                log_error_q("pq_open failed: %s: %s\n", pqfname,
                        strerror(status));
            }
            status = PQ_ERROR;
        } else {
            Timer* timer;

            /*
             * Set RPC timeout for LDM proxies.
             */
            lp_setRpcTimeout(rpcTimeout);

            /*
             * Set countdown timer for session time-limit.
             */
            timer = timer_new(totalTimeo);

            if (NULL == timer) {
                status = SYSTEM_ERROR;
            } else {
                if (!coupledTimes)
                    pq_cset(pq, &offer.from);

                /*
                 * Loop over connection attempts.
                 */
                while (!done) {
                    status = executeConnection();

                    if (0 == status) {
                        break;
                    } else if (CONNECTION_ABORTED == status) {
                        exitIfDone(INTERRUPTED);
                        sleep(rpcTimeout);
                    } else if (CONNECTION_TIMEDOUT == status) {
                        log_flush_error();
                        if (timer_hasElapsed(timer)) {
                            log_add(
                                    "Session time-limit reached " "(%lu seconds)",
                                    totalTimeo);
                            status = SESSION_TIMEDOUT;
                            break;
                        }
                    } else if (PQ_ERROR == status || SYSTEM_ERROR == status) {
                        break;
                    }
                } /* connection loop */

                if (done) {
                    status = INTERRUPTED;
                }

                timer_free(timer);
            } /* "timer" allocated */
        } /* product-queue open */
    } /* exit-handler registered */

    return status;
}

/*
 * Returns:
 *      0                       Success.
 *      INVOCATION_ERROR        User-error on command-line. "log_flush()" called.
 *      PQ_ERROR                Product-queue I/O failure. "log_flush()" called.
 *      SESSION_TIMEDOUT        The time-limit on the session was reached.
 *                              "log_flush()" called.
 *      SYSTEM_ERROR            O/S failure. "log_flush()" called.
 *      INTERRUPTED             The process received a SIGTERM.
 */
int main(int ac, char* const * const av) {
    int status;

    if (log_init(av[0])) {
        log_syserr("Couldn't initialize logging module");
        status = EXIT_FAILURE;
    }
    else {
        status = getConfiguration(ac, av);
        if (0 != status) {
            log_error_q("Couldn't get execution parameters");
            if (INVOCATION_ERROR == status)
                printUsage(av[0]);
        } else {
            status = execute();
            if (0 != status)
                log_flush_error();
        }
    }

    return status;
}
