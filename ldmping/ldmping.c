/**
 * Ping an LDM server.
 *
 * Copyright 2018, University Corporation for Atmospheric Research
 * All rights reserved. See file COPYRIGHT in the top-level source-directory for
 * copying and redistribution conditions.
 */

/* 
 * pings remote host
 */

#include "config.h"

#include "globals.h"
#include "h_clnt.h"
#include "ldm5.h"
#include "ldm5_clnt.h"
#include "log.h"

#include <errno.h>
#include <rpc/rpc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#define DEFAULT_INTERVAL 25
#define DEFAULT_TIMEO 10


/*
 * ping the remote
 */
static enum clnt_stat
check_hstat(hcp, timeout)
h_clnt *hcp ;
int timeout ;
{
        return nullproc5(hcp, timeout) ;
}


static void
print_label()
{
        log_info_q("%10s %10s %4s   %-21s %s\n",
                        "State",
                        "Elapsed",
                        "Port",
                        "Remote_Host",
                        "rpc_stat"
                        ) ;
        return ;
}


static void
print_hstat(hcp)
h_clnt *hcp ;
{
        if(hcp->state == RESPONDING)
                log_info_q("%10s %3ld.%06ld %4d   %-11s  %s\n",
                        s_remote_state(hcp->state),
                        hcp->elapsed.tv_sec, hcp->elapsed.tv_usec,
                        hcp->port,
                        hcp->remote,
                        s_hclnt_sperrno(hcp)
                        ) ;
        else
                log_error_q("%10s %3ld.%06ld %4d   %-11s  %s\n",
                        s_remote_state(hcp->state),
                        hcp->elapsed.tv_sec, hcp->elapsed.tv_usec,
                        hcp->port,
                        hcp->remote,
                        s_hclnt_sperrno(hcp)
                        ) ;
        return ;
}


static void
usage(av0)
char *av0 ; /*  id string */
{
        (void)fprintf(stderr,
"Usage: %s [options] [remote ...] \t\nOptions:\n", av0);
        (void)fprintf(stderr,
"\t-v           Verbose (default if interactive)\n") ;
        (void)fprintf(stderr,
"\t-q           Quiet (to shut up when interactive)\n") ;
        (void)fprintf(stderr,
"\t-x           Debug mode\n") ;
        (void)fprintf(stderr,
"\t-l dest      Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t             (standard error), or file `dest`. Default is \"%s\"\n",
                log_get_default_destination());
        (void)fprintf(stderr,
"\t                 else uses syslogd)\n") ;
        (void)fprintf(stderr,
"\t-t timeout   set RPC timeout to \"timeout\" seconds (default %d)\n",
                        DEFAULT_TIMEO) ;
        fprintf(stderr,
"\t-i interval  Poll after \"interval\" secs (default %d when interactive,\n",
                DEFAULT_INTERVAL) ;
        (void)fprintf(stderr,
"\t                 0 => one trip otherwise)\n") ;
        (void)fprintf(stderr,
"\t-h remote    \"remote\" host to ping (default is localhost)\n") ;
        exit(1);
}

/**
 * Called upon receipt of signals. This callback routine is registered in
 * set_sigactions().
 *
 * @param[in] sig  Delivered signal
 */
static void
signal_handler(const int sig)
{
    switch(sig) {
    case SIGINT :
        /*FALLTHROUGH*/
    case SIGTERM:
        done = 1;
        return;
    case SIGUSR1:
        log_refresh();
        return;
    case SIGUSR2:
        log_roll_level();
        return;
    default:
        return;
    }
}

/**
 * Sets signal handling for this program.
 */
static void
set_sigactions(void)
{
    struct sigaction sigact;
    (void)sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    // Ignore the following
    sigact.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sigact, NULL);

    // Handle the following
    sigact.sa_handler = signal_handler;

    // Don't restart the following
    (void) sigaction(SIGINT, &sigact, NULL);
    (void) sigaction(SIGTERM, &sigact, NULL);

    // Restart the following
    sigact.sa_flags = SA_RESTART;
    (void) sigaction(SIGUSR1, &sigact, NULL);
    (void) sigaction(SIGUSR2, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigaddset(&sigset, SIGPIPE);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

int main(ac,av)
int ac ;
char *av[] ;
{
        int verbose = 0 ;
        int interval = 0 ;
        int timeo = DEFAULT_TIMEO ; 
        int nremotes = 0 ;
#define MAX_REMOTES 14 /* 2 * MAX_REMOTES + 3 < max_open_file_descriptors */
        h_clnt stats[MAX_REMOTES + 1] ;
        h_clnt *sp ;

        /*
         * initialize logger
         */
        if (log_init(av[0])) {
            log_syserr("Couldn't initialize logging module");
            exit(1);
        }
        log_set_level(LOG_LEVEL_INFO);

        if(isatty(fileno(stderr)))
        {
                /* set interactive defaults */
                verbose = !0 ;
                interval = DEFAULT_INTERVAL ;
        }

        {
        extern int optind;
        extern int opterr;
        extern char *optarg;
        int ch;

        opterr = 1;

        while ((ch = getopt(ac, av, "vxl:t:h:P:qi:")) != EOF)
                switch (ch) {
                case 'v':
                        if (!log_is_enabled_info)
                            (void)log_set_level(LOG_LEVEL_INFO);
                        verbose = !0 ;
                        break;
                case 'q':
                        verbose = 0 ;
                        break ;
                case 'x':
                        (void)log_set_level(LOG_LEVEL_DEBUG);
                        break;
                case 'l':
                        if (log_set_destination(optarg)) {
                            log_syserr("Couldn't set logging destination to \"%s\"",
                                    optarg);
                            exit(1);
                        }
                        break;
                case 'h':
                        if(nremotes > MAX_REMOTES)
                        {
                                fprintf(stderr,
                                        "Can't handle more than %d remotes\n", MAX_REMOTES) ;
                                break ;
                        }
                        init_h_clnt(&stats[nremotes++], optarg,
                                LDMPROG, FIVE, IPPROTO_TCP) ;
                        break ;
                case 'P': {
                    log_warning("Port specification is ignored");
                    break;
                }
                case 't':
                        timeo = atoi(optarg) ;
                        if(timeo == 0 && *optarg != '0')
                        {
                                fprintf(stderr, "%s: invalid timeout %s", av[0], optarg) ;
                                usage(av[0]) ;  
                        }
                        break;
                case 'i':
                        interval = atoi(optarg) ;
                        if(interval == 0 && *optarg != '0')
                        {
                                fprintf(stderr, "%s: invalid interval %s", av[0], optarg) ;
                                        usage(av[0]) ;
                        }
                        break ;
                case '?':
                        usage(av[0]);
                        break;
                }

        for(; nremotes < MAX_REMOTES && optind < ac ; nremotes++, optind++)
        {
                init_h_clnt(&stats[nremotes], av[optind],
                        LDMPROG, FIVE, IPPROTO_TCP) ;
        }
        if(ac - optind > 0)
        {
                fprintf(stderr,
                        "Can't handle more than %d remotes\n", MAX_REMOTES) ;
        }
        if(nremotes == 0)
        {
                init_h_clnt(&stats[nremotes++], "localhost",
                        LDMPROG, FIVE, IPPROTO_TCP) ;
        }
        stats[nremotes].state = H_NONE ; /* terminator */
        }

        /*
         * set up signal handlers
         */
        set_sigactions();

        if(verbose)
                print_label() ;

        while(!done)
        {
                for(sp = stats ; sp->state != H_NONE ; sp++)
                {
                        check_hstat(sp, timeo) ; 
                        /* if not verbose, only report "significant" stuff */
                        if(verbose || sp->elapsed.tv_sec > 1 || sp->state != RESPONDING)
                                print_hstat(sp) ;
                        if(interval == 0 && sp->state != RESPONDING)
                                exit(1) ;
                }
                if(interval == 0)
                        break ; 
                sleep(interval) ;
        }

        exit(0) ;
}
