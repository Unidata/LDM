/*
 *   Copyright 2011, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */
/**
 *   @file dvbs_multicast.c
 *
 *   This file contains the code for the \c dvbs_multicast(1) program. This
 *   program captures broadcast UDP packets from a NOAAPORT DVB-S receiver and
 *   writes the packet data to a shared-memory FIFO or directly to an LDM
 *   product-queue.
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
#include <assert.h>
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
      (void)fprintf(stderr, "Usage: %s [options] mcast_address\t\nOptions:\n",
              av0);
      (void)fprintf(stderr, "\t-n           Log notice messages\n");
      (void)fprintf(stderr, "\t-v           Verbose, tell me about each "
              "packet\n");
      (void)fprintf(stderr, "\t-x           Log debug messages\n");
      (void)fprintf(stderr, "\t-l logfile   Default logs to syslogd\n");
      (void)fprintf(stderr, "\t-q queue     default \"%s\"\n", getQueuePath());
      (void)fprintf(stderr, "\t-d           dump packets, no output");
      (void)fprintf(stderr, "\t-b pagnum    Number of pages for shared memory "
              "buffer\n");
      exit(1);
}

static void mypriv_stats(void)
{
    unotice("wait count %d",mypriv.counter);
}

/*
 * Called upon receipt of signals
 */
static void signal_handler(
        const int       sig)
{
#ifdef SVR3SIGNALS
    /*
     * Some systems reset handler to SIG_DFL upon entry to handler.
     * In that case, we reregister our handler.
     */
    (void)signal(sig, signal_handler);
#endif

    switch (sig) {
    case SIGINT:
        exit(0);
    case SIGTERM:
        exit(0);
    case SIGPIPE:
        return;
    case SIGUSR1:
        logmypriv = !0;
        return;
    case SIGUSR2:
        rollulogpri();
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

    /* Ignore these */
    sigact.sa_handler = SIG_IGN;
    (void)sigaction(SIGHUP, &sigact, NULL);
    (void)sigaction(SIGALRM, &sigact, NULL);
    (void)sigaction(SIGCHLD, &sigact, NULL);
    (void)sigaction(SIGCONT, &sigact, NULL);

    /* Handle these */
#ifdef SA_RESTART		/* SVR4, 4.3+ BSD */
    /* Usually, restart system calls */
    sigact.sa_flags |= SA_RESTART;
#endif
    sigact.sa_handler = signal_handler;
    (void)sigaction(SIGTERM, &sigact, NULL);
    (void)sigaction(SIGUSR1, &sigact, NULL);
    (void)sigaction(SIGUSR2, &sigact, NULL);

    /* Don't restart after interrupt */
    sigact.sa_flags = 0;
#ifdef SA_INTERRUPT		/* SunOS 4.x */
    sigact.sa_flags |= SA_INTERRUPT;
#endif
    (void)sigaction(SIGINT, &sigact, NULL);
    (void)sigaction(SIGPIPE, &sigact, NULL);
}

static void cleanup(void)
{
    int status;

    unotice("cleanup %d", child);

    if (shm != NULL)
        shmfifo_detach(shm);

    if ((!memsegflg ) && (child == 0))		/* child */
        return;

    if (!memsegflg) {
        unotice("waiting for child");
        wait(&status);
    }

    if (shm != NULL)
        shmfifo_dealloc(shm);

    if (pq != NULL) {
        unotice("Closing product_queue\0");
        pq_close(pq);
    }

    unotice("parent exiting");
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
    const char* const   progName = ubasename(argv[0]);
    const char*         pqfname = getQueuePath();
    int                 sd, rc, n;
    socklen_t           cliLen;
    struct ip_mreq      mreq;
    struct sockaddr_in  cliAddr, servAddr;
    struct in_addr      mcastAddr;
    struct hostent*     h;
    int                 pid_channel, dumpflag = 0;
    char*               imr_interface = NULL;
    FILE*               f = stdout;
    extern int          optind;
    extern int          opterr;
    extern char*        optarg;
    int                 ch;
    int                 logmask = LOG_MASK(LOG_ERR);
    const char*         logfname = NULL;        /* use system logging daemon */
    unsigned            logOptions = LOG_CONS | LOG_PID;
    unsigned            logFacility = LOG_LDM;  /* use default LDM facility */
    int                 status, ipri=0, rtflag = 0;
    int                 bufpag = CBUFPAG;
    product             prod;
    static char*        prodident = "dvbs";

    /* Initialize the logger. */
    (void)setulogmask(logmask);
    (void)openulog(progName, logOptions, logFacility, logfname);

    opterr = 1;

    while ((ch = getopt(argc, argv, "dmnrvxl:q:b:p:I:")) != EOF) {
        switch (ch) {
        case 'v':
            logmask |= LOG_MASK(LOG_INFO);
            (void)setulogmask(logmask);
            break;
        case 'x':
            logmask |= LOG_MASK(LOG_DEBUG);
            (void)setulogmask(logmask);
            break;
        case 'n':
            logmask |= LOG_MASK(LOG_NOTICE);
            (void)setulogmask(logmask);
            break;
        case 'l':
            if (optarg[0] == '-' && optarg[1] != 0) {
                uerror("logfile \"%s\" ??", optarg);
                usage(argv[0]);
            }
            /* else */
            logfname = optarg;
            (void)openulog(progName, logOptions, logFacility, logfname);
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

    (void)setulogmask(logmask);

    if (argc - optind < 1)
        usage(argv[0]);

    unotice("Starting Up %s", PACKAGE_VERSION);

    /*
     * Use mlockall command to prevent paging of our process, then exit root
     * privileges
     */
    if (mlockall( MCL_CURRENT|MCL_FUTURE ) != 0 )
        serror("mlockall");

    if (rtflag) {
#if _XOPEN_REALTIME != -1
       {
           struct sched_param   schedparam;
           if ((schedparam.sched_priority = sched_get_priority_max(SCHED_RR))
                   != -1) {
              status = sched_setscheduler(0, SCHED_RR, &schedparam);

              if (status != -1)
                  unotice("Realtime scheduler %d",status);
              else
                  serror("scheduler");
           }
       }
#else
       uerror("rtmode not configured");
#endif
    }
    else if (ipri != 0) {
       if (setpriority(PRIO_PROCESS, 0, ipri) != 0)
           serror("setpriority");
    }

    if ((pq == NULL) && (dumpflag)) {
        if (pq_open(pqfname, PQ_DEFAULT, &pq)) {
            uerror("couldn't open the product queue %s\0", pqfname);
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
        serror("atexit");
        exit(1);
    }

    /* Get IP socket port for multicast address as s_port[pid_channel-1] */
    sscanf(argv[optind], "%*d.%*d.%*d.%d", &pid_channel);

    if ((pid_channel < 1) || (pid_channel > MAX_DVBS_PID)) {
        uerror("multicast address %s outside range of expected server ports\n",
                argv[optind]);
        exit(1);
    }

    /* Ensure that the shared-memory FIFO exists. */
    shm = memsegflg
           ? shmfifo_create(bufpag, sizeof(struct shmfifo_priv),
                   s_port[pid_channel - 1])
           : shmfifo_create(bufpag, sizeof(struct shmfifo_priv), -1);

    if (shm == NULL) {
        uerror("%s: Couldn't ensure existence of shared-memory FIFO", progName);
        exit(1);
    }

    if (!memsegflg)
        child = fork();

    if (memsegflg || (child != 0)) {
        /* Parent */
        unsigned long   sbnnum, lastnum = 0;
        char            msg[MAX_MSG];

        if (shmfifo_attach(shm) == -1) {
            uerror("parent cannot attach");
            exit(1);
        };

        mypriv.counter = 0;

        shmfifo_setpriv(shm, &mypriv);

        h = gethostbyname(argv[optind]);

        if (h == NULL) {
            uerror("%s : unknown group '%s'", argv[0], argv[optind]);
            exit(1);
        }

        memcpy(&mcastAddr, h->h_addr_list[0], h->h_length);

        /* Check given address is multicast */
        if (!IN_MULTICAST(ntohl(mcastAddr.s_addr))) {
            uerror("%s : given address '%s' is not multicast", argv[0],
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
            uerror("%s : cannot create socket", argv[0]);
            exit(1);
        }

        /* Bind port */
        servAddr.sin_family = AF_INET;
        servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        servAddr.sin_port = htons(s_port[pid_channel - 1]);
        
        if (bind(sd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
            uerror("%s : cannot bind port %d", argv[0],
                    s_port[pid_channel - 1]);
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
            uerror("%s : cannot join multicast group '%s'", argv[0],
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
                        unotice("recvfrom returned %d",n);
                    }
                    else {
                       if (n == 0)
                          uerror("recvfrom returns zero");
                       else
                          serror("recvfrom failure");
                    }

                    haslogged = 1;

                    sleep(1);

                    continue;
                }

                if (haslogged) {
                    unotice("recvfrom has succeeded");

                    haslogged = 0;
                }

                assert(n <= MAX_MSG);

                sbnnum = ((((((unsigned char) msg[8] << 8) +
                      (unsigned char) msg[9]) << 8) +
                      (unsigned char) msg[10]) << 8) + (unsigned char) msg[11];

                if (ulogIsDebug())
                    udebug("received %d bytes", n);

                if (sbnnum <= lastnum) {
                    unotice("Retrograde packet number: previous=%lu, "
                        "latest=%lu, difference=%lu", lastnum, sbnnum,
                        lastnum - sbnnum);
                }
                else {
                    unsigned long       gap = sbnnum - lastnum - 1;

                    if ((lastnum != 0) && (0 < gap)) {
                        uerror("Gap in SBN last %lu, this %lu, gap %lu",
                            lastnum, sbnnum, gap);
                    }
                    else if (ulogIsVerbose()) {
                        uinfo("SBN number %u", sbnnum);
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

        udebug("I am the child");

        if (shmfifo_attach(shm) == -1) {
            uerror("child cannot attach");
            exit(1);
        }

        for (;;) {
            /* Check for data without locking */
            while (shmfifo_empty(shm)) {
                if (ulogIsVerbose())
                    uinfo("nothing in shmem, waiting...");
                    usleep(500);
            }

            if (shmfifo_get(shm, msg, MAX_MSG, &n) != 0) {
                uerror( "circbuf read failed to return data...");
                exit(1);
            }

            sbnnum = ((((((unsigned char) msg[8] << 8) +
                  (unsigned char) msg[9]) << 8) +
                  (unsigned char) msg[10]) << 8) + (unsigned char) msg[11];

            if (ulogIsDebug())
                udebug("child received %d bytes", n);
            if ((lastnum != 0) && (lastnum + 1 != sbnnum))
                uerror("Gap in SBN last %lu, this %lu, gap %lu", lastnum,
                    sbnnum, sbnnum - lastnum);
            else if (ulogIsVerbose())
                uinfo("SBN number %u", sbnnum);

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
                status = pq_insert(pq, &prod);

                if (status != 0) {
                    if (status == PQUEUE_DUP) {
                        unotice("SBN %u already in queue", prod.info.seqno);
                    }
                    else {
                        uerror("pqinsert failed [%d] SBN %u", status,
                              prod.info.seqno);
                    }
                }
            }
        }
    }				/* child end */

    cleanup();

    return 0;
}
