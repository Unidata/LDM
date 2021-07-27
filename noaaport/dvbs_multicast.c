/**
 * Ingest a NOAAPORT stream to a shared-memory FIFO.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 *
 * @file dvbs_multicast.c
 */
#include  <config.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#ifndef __USE_MISC
#define __USE_MISC              /* To get "struct ip_mreq". Don't move! */
#endif
#include <netdb.h>
#include <netinet/in.h>         /* defines "struct ip_mreq" */
#include <arpa/inet.h>
#include <sched.h>		        /* Use Realtime Scheduler */

/* If we are setuid root, we can lock memory so it won't be swapped out */
#include <sys/mman.h>
/* setpriority */
#include <sys/resource.h>
#include <log.h>
#include <sys/wait.h>

#include "shmfifo.h"

/* LDM headers */
#include "ldm.h"
#include "pq.h"
#include "log.h"
#include "globals.h"

/* Local headers */
#include  "dvbs.h"

struct shmfifo_priv {
    int counter;
};

#define                         MAX_MSG 10000
static const int                CBUFPAG = 2000;
static struct shmhandle*        shm = NULL;
static int                      child = 0;
static int                      memsegflg = 0;
static struct shmfifo_priv      mypriv;
static volatile sig_atomic_t    logmypriv = 0;

static void usage(
    const char* const av0)      /*  id string */
{
      (void)fprintf(stderr,
"Usage: %s [options] mcast_address\t\nOptions:\n", av0);
      (void)fprintf(stderr,
"\t-n           Log notice messages\n");
      (void)fprintf(stderr,
"\t-v           Verbose, tell me about each packet\n");
      (void)fprintf(stderr,
"\t-x           Log debug messages\n");
      (void)fprintf(stderr,
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
      (void)fprintf(stderr,
"\t-q queue     default \"%s\"\n", getDefaultQueuePath());
      (void)fprintf(stderr,
"\t-d           dump packets, no output");
      (void)fprintf(stderr,
"\t-b pagnum    Number of pages for shared memory buffer\n");
      exit(1);
}

static void mypriv_stats(void)
{
    log_notice_q("wait count %d",mypriv.counter);
}

/*
 * Called upon receipt of signals
 */
static void signal_handler(
        const int       sig)
{
    switch (sig) {
    case SIGINT:
        exit(0);
    case SIGTERM:
        exit(0);
    case SIGPIPE:
        return;
    case SIGUSR1:
        log_refresh();
        logmypriv = !0;
        return;
    case SIGUSR2:
        log_roll_level();
        return;
    }

    return;
}

/*
 * Register the signal_handler
 */
static void set_sigactions(void)
{
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Ignore the following */
    sigact.sa_handler = SIG_IGN;
    (void)sigaction(SIGALRM, &sigact, NULL);
    (void)sigaction(SIGCHLD, &sigact, NULL);
    (void)sigaction(SIGCONT, &sigact, NULL);

    /* Handle the following */
    sigact.sa_handler = signal_handler;

    /* Don't restart the following */
    (void)sigaction(SIGINT, &sigact, NULL);
    (void)sigaction(SIGPIPE, &sigact, NULL);

    /* Restart the following */
    sigact.sa_flags |= SA_RESTART;
    (void)sigaction(SIGTERM, &sigact, NULL);
    (void)sigaction(SIGUSR1, &sigact, NULL);
    (void)sigaction(SIGUSR2, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGALRM);
    (void)sigaddset(&sigset, SIGCHLD);
    (void)sigaddset(&sigset, SIGCONT);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigaddset(&sigset, SIGPIPE);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

static void cleanup(void)
{
    int status;

    log_notice_q("cleanup %d", child);

    if (shm != NULL)
        shmfifo_detach(shm);

    if ((!memsegflg ) && (child == 0))		/* child */
        return;

    if (!memsegflg) {
        log_notice_q("waiting for child");
        wait(&status);
    }

    if (shm != NULL)
        shmfifo_dealloc(shm);

    if (pq != NULL) {
        log_notice_q("Closing product_queue\0");
        pq_close(pq);
    }

    log_notice_q("parent exiting");
}

/**
 * Captures NOAAPORT broadcast UDP packets from a DVB-S or DVB-S2 receiver and
 * writes the data into a shared-memory FIFO or an LDM product-queue.
 *
 * Usage:
 *
 *     dvbs_multicast [-dmnrvx] [-b <em>npage</em>] [-l <em>log</em>] [-q <em>queue</em>] [-I <em>interface</em>] <em>mcastAddr</em>\n
 *
 * Where:
 * <dl>
 *      <dt>-b <em>npage</em></dt>
 *      <dd>Use \e npage as the size, in pages, for the shared-memory FIFO.
 *      The number of bytes in a page can be found via the command
 *      \c "getconf PAGE_SIZE".</dd>
 *
 *      <dt>-d</dt>
 *      <dd>Write the UDP packet data directly into the LDM product-queue
 *      rather than into the shared-memory FIFO. Note that the LDM
 *      data-products so created will consist of the individual data packets
 *      rather than the higher-level NOAAPORT data-products.</dd>
 *
 *      <dt>-I <em>interface</em></dt>
 *      <dd>Listen for broadcast UDP packets on interface \e interface.
 *      The default is to listen on all interfaces.</dd>
 *
 *      <dt>-l <em>log</em></dt>
 *      <dd>Log to \e log. if \e log is \c "-", then logging occurs to the 
 *      standard error stream; otherwise, \e log is the pathname of a file to
 *      which logging will occur. If not specified, then log messages will go
 *      to the system logging daemon. </dd>
 *
 *      <dt>-m</dt>
 *      <dd>Create a non-private shared-memory FIFO and write data to it (the
 *      normal case).  If not specified, then a private shared-memory FIFO will
 *      be created and a child process will be spawned to read from it (the
 *      test case).</dd>
 *
 *      <dt>-n</dt>
 *      <dd>Log messages of level NOTICE and higher priority.</dd>
 *
 *      <dt>-q <em>queue</em></dt>
 *      <dd>Use \e queue as the pathname of the LDM product-queue. The default
 *      is to use the default LDM pathname of the product-queue.</dd>
 *
 *      <dt>-p <em>niceness</em></dt>
 *      <dd>Set the priority of this process to \e niceness. Negative values
 *      have higher priority. Not normally done.</dd>
 *
 *      <dt>-r</dt>
 *      <dd>Attempt to set this process to the highest priority using the
 *      POSIX round-robin real-time scheduler. Not usually done.</dd>
 *
 *      <dt>-v</dt>
 *      <dd>Log messages of level INFO and higher priority. This will log
 *      information on every received UDP packet.</dd>
 *
 *      <dt>-x</dt>
 *      <dd>Log messages of level DEBUG and higher priority.</dd>
 *
 *      <dt><em>mcastAddr</em></dt>
 *      <dd>Multicast address of the UDP packets to listen for (e.g., 
 *      \c "224.0.1.1").</dd>
 * </dl>
 *
 * @retval 0 if successful.
 * @retval 1 if an error occurred. At least one error-message is logged.
 */
int main(
    const int           argc,
    char* const         argv[])
{
    int                 sd, rc, n;
    socklen_t           cliLen;
    struct ip_mreq      mreq;
    struct sockaddr_in  cliAddr, servAddr = {};
    struct in_addr      mcastAddr;
    struct hostent*     h;
    /*
     * The following is *not* the DVB PID: it's the least significant byte of
     * the IPv4 multicast address (e.g., the "3" in "224.0.1.3").
     */
    int                 pid_channel;
    int                 dumpflag = 0;
    char*               imr_interface = NULL;
    FILE*               f = stdout;
    extern int          optind;
    extern int          opterr;
    extern char*        optarg;
    int                 ch;
    int                 status, ipri=0, rtflag = 0;
    int                 bufpag = CBUFPAG;
    product             prod;
    static char*        prodident = "dvbs";

    /* Initialize the logger. */
    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }
    (void)log_set_level(LOG_LEVEL_ERROR);
    const char*         pqfname = getQueuePath();

    opterr = 1;

    while ((ch = getopt(argc, argv, "dmnrvxl:q:b:p:I:")) != EOF) {
        switch (ch) {
        case 'v':
            if (!log_is_enabled_info)
                (void)log_set_level(LOG_LEVEL_INFO);
            break;
        case 'x':
            (void)log_set_level(LOG_LEVEL_DEBUG);
            break;
        case 'n':
            (void)log_set_level(LOG_LEVEL_NOTICE);
            break;
        case 'l':
            if (optarg[0] == '-' && optarg[1] != 0) {
                log_error_q("logfile \"%s\" ??", optarg);
                usage(argv[0]);
            }
            /* else */
            if (log_set_destination(optarg)) {
                log_syserr("Couldn't set logging destination to \"%s\"",
                        optarg);
                exit(1);
            }
            break;
        case 'q':
            pqfname = optarg;
            break;
        case 'I':
            imr_interface = optarg;
            break;
        case 'r':
            rtflag = 1;
            break;
        case 'd':
            dumpflag = 1;
            break;
        case 'b':
            bufpag = atoi(optarg);

            if (bufpag < 500)
                bufpag = 500;
            if (bufpag > 40000)
                bufpag = 40000;

            break;
        case 'm':
            memsegflg = 1;
            break;
        case 'p':
            ipri = atoi(optarg);

            if (ipri < -20) /* generally PRIO_MIN ... PRIO_MAX */
                ipri = -20;
            else if (ipri > 20)
                ipri = 20;

            break;
        case '?':
            usage(argv[0]);
            break;
        }
    }

    if (argc - optind < 1)
        usage(argv[0]);

    setQueuePath(pqfname);

    log_notice_q("Starting Up %s", PACKAGE_VERSION);

    /*
     * Use mlockall command to prevent paging of our process, then exit root
     * privileges
     */
    if (mlockall( MCL_CURRENT|MCL_FUTURE ) != 0 )
        log_syserr("mlockall");

    if (rtflag) {
#if _XOPEN_REALTIME != -1
       {
           struct sched_param   schedparam;
           if ((schedparam.sched_priority = sched_get_priority_max(SCHED_RR))
                   != -1) {
              status = sched_setscheduler(0, SCHED_RR, &schedparam);

              if (status != -1)
                  log_notice_q("Realtime scheduler %d",status);
              else
                  log_syserr("scheduler");
           }
       }
#else
       log_error_q("rtmode not configured");
#endif
    }
    else if (ipri != 0) {
       if (setpriority(PRIO_PROCESS, 0, ipri) != 0)
           log_syserr("setpriority");
    }

    if ((pq == NULL) && (dumpflag)) {
        if (pq_open(pqfname, PQ_DEFAULT, &pq)) {
            log_error_q("couldn't open the product queue %s\0", pqfname);
            exit(1);
        }

        prod.info.feedtype = EXP;
        prod.info.ident = prodident;
        prod.info.origin = argv[optind];

        memset(prod.info.signature, 0, 16);
    }

    /*
     * Set up signal handlers
     */
    set_sigactions();

    /*
     * Register atexit routine
     */
    if (atexit(cleanup) != 0) {
        log_syserr("atexit");
        exit(1);
    }

    /* Get IP socket port for multicast address as s_port[pid_channel-1] */
    int nbytes;
    if (sscanf(argv[optind], "%*d.%*d.%*d.%d %n", &pid_channel, &nbytes) != 1
            || argv[optind][nbytes] != 0) {
        log_error_q("Unable to decode multicast address \"%s\"", argv[optind]);
        exit(1);
    }

    if ((pid_channel < 1) || (pid_channel > MAX_DVBS_PID)) {
        log_error_q("multicast address %s outside range of expected server ports\n",
                argv[optind]);
        exit(1);
    }

    /* Ensure that the shared-memory FIFO exists. */
    shm = memsegflg
           ? shmfifo_create(bufpag, sizeof(struct shmfifo_priv),
                   s_port[pid_channel - 1])
           : shmfifo_create(bufpag, sizeof(struct shmfifo_priv), -1);

    if (shm == NULL) {
        log_error_q("Couldn't ensure existence of shared-memory FIFO");
        exit(1);
    }

    if (!memsegflg)
        child = fork();

    if (memsegflg || (child != 0)) {
        /* Parent */
        unsigned long   sbnnum, lastnum = 0;
        char            msg[MAX_MSG];

        if (shmfifo_attach(shm) == -1) {
            log_error_q("parent cannot attach");
            exit(1);
        };

        mypriv.counter = 0;

        shmfifo_setpriv(shm, &mypriv);

        h = gethostbyname(argv[optind]);

        if (h == NULL) {
            log_error_q("unknown group '%s'", argv[optind]);
            exit(1);
        }

        memcpy(&mcastAddr, h->h_addr_list[0], h->h_length);

        /* Check given address is multicast */
        if (!IN_MULTICAST(ntohl(mcastAddr.s_addr))) {
            log_error_q("given address '%s' is not multicast",
                    inet_ntoa(mcastAddr));
            exit(1);
        }

        /* Get IP socket port for multicast address as s_port[pid_channel-1]
        sscanf(argv[optind], "%*d.%*d.%*d.%d", &pid_channel);
        if ((pid_channel < 1) || (pid_channel > MAX_DVBS_PID))
          {
            printf
              ("multicast address %s outside range of expected server ports\n",
               argv[optind]);
            exit(1);
          }*/

        /* Create socket */
        sd = socket(AF_INET, SOCK_DGRAM, 0);

        if (sd < 0) {
            log_error_q("%s : cannot create socket", argv[0]);
            exit(1);
        }

        /* Bind port */
        servAddr.sin_family = AF_INET;
        servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        servAddr.sin_port = htons(s_port[pid_channel - 1]);
        
        if (bind(sd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
            log_error_q("cannot bind port %d", s_port[pid_channel - 1]);
            exit(1);
        }

        /* Join multicast group */
        mreq.imr_multiaddr.s_addr = mcastAddr.s_addr;
        mreq.imr_interface.s_addr = (imr_interface == NULL )
            ? htonl(INADDR_ANY)
            : inet_addr(imr_interface);
        /*mreq.imr_interface.s_addr = inet_addr("192.168.0.83");*/

        rc = setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *) &mreq,
                      sizeof(mreq));

        if (rc < 0) {
            log_error_q("cannot join multicast group '%s'",
                    inet_ntoa(mcastAddr));
            exit(1);
        }
        else {
            /* Infinite server loop */
            for (;;) {
                static int        haslogged=0;

                /*
                 * Check to see if we need to log any information from signal
                 * handler.
                 */
                if (logmypriv) {
                    mypriv_stats();
                    logmypriv = 0;
                }

                cliLen = sizeof(cliAddr);
                n = recvfrom(sd, msg, MAX_MSG, 0, (struct sockaddr *) &cliAddr,
                            &cliLen);

                if (n <= 0) {
                    if (haslogged) {
                        log_notice_q("recvfrom returned %d",n);
                    }
                    else {
                       if (n == 0)
                          log_error_q("recvfrom returns zero");
                       else
                          log_syserr("recvfrom failure");
                    }

                    haslogged = 1;

                    sleep(1);

                    continue;
                }

                if (haslogged) {
                    log_notice_q("recvfrom has succeeded");

                    haslogged = 0;
                }

                log_assert(n <= MAX_MSG);

                sbnnum = ((((((unsigned char) msg[8] << 8) +
                      (unsigned char) msg[9]) << 8) +
                      (unsigned char) msg[10]) << 8) + (unsigned char) msg[11];

                log_debug("received %d bytes", n);

                if (sbnnum <= lastnum) {
                    log_notice_q("Retrograde packet number: previous=%lu, "
                        "latest=%lu, difference=%lu", lastnum, sbnnum,
                        lastnum - sbnnum);
                }
                else {
                    unsigned long       gap = sbnnum - lastnum - 1;

                    if ((lastnum != 0) && (0 < gap)) {
                        log_error_q("Gap in SBN last %lu, this %lu, gap %lu",
                            lastnum, sbnnum, gap);
                    }
                    else if (log_is_enabled_info) {
                        log_info_q("SBN number %u", sbnnum);
                    }
                }

                lastnum = sbnnum;

                if ((status = shmfifo_put(shm, msg, n)) != 0 &&
                        (status != E2BIG)) {
                    exit(1);
                }
            }
        }
    }				/* parent end */
    else {
        /* Child */
        char            msg[MAX_MSG];
        unsigned long   sbnnum, lastnum = 0;

        log_debug("I am the child");

        if (shmfifo_attach(shm) == -1) {
            log_error_q("child cannot attach");
            exit(1);
        }

        for (;;) {
            /* Check for data without locking */
            while (shmfifo_empty(shm)) {
                if (log_is_enabled_info)
                    log_info_q("nothing in shmem, waiting...");
                struct timespec tspec = {.tv_sec=0, .tv_nsec=500000};
                nanosleep(&tspec, NULL);
            }

            if (shmfifo_get(shm, msg, MAX_MSG, &n) != 0) {
                log_error_q( "circbuf read failed to return data...");
                exit(1);
            }

            sbnnum = ((((((unsigned char) msg[8] << 8) +
                  (unsigned char) msg[9]) << 8) +
                  (unsigned char) msg[10]) << 8) + (unsigned char) msg[11];

            if (log_is_enabled_debug)
                log_debug("child received %d bytes", n);
            if ((lastnum != 0) && (lastnum + 1 != sbnnum))
                log_error_q("Gap in SBN last %lu, this %lu, gap %lu", lastnum,
                    sbnnum, sbnnum - lastnum);
            else if (log_is_enabled_info)
                log_info_q("SBN number %u", sbnnum);

            lastnum = sbnnum;

            if (!dumpflag) {
                fwrite(msg, 1, n, f);
            }
            else {
                prod.info.seqno = sbnnum;
                prod.data = msg;
                prod.info.sz = n;

                /*
                 * Use the first 16 bytes of msg as the signature.  The SBN
                 * identifier is the first 16 bytes of msg, where the unique
                 * SBN sequence number is bytes 8-11.
                 */
                /* memcpy( prod.info.signature, msg, 16 ); */
                memcpy(prod.info.signature, msg + 8, 4);

                status = set_timestamp(&prod.info.arrival);
                if (status) {
                    log_add("Couldn't set timestamp");
                    log_flush_error();
                }

                status = pq_insert(pq, &prod);
                if (status != 0) {
                    if (status == PQUEUE_DUP) {
                        log_notice_q("SBN %u already in queue", prod.info.seqno);
                    }
                    else {
                        log_error_q("pqinsert failed [%d] SBN %u", status,
                              prod.info.seqno);
                    }
                }
            }
        }
    }				/* child end */

    cleanup();

    return 0;
}
