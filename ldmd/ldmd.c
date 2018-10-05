/**
 * LDM server mainline program module
 *
 * Copyright 2018 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 */

#include <config.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>      /* pmap_unset() */
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>     /* sysconf */
#include <errno.h>
#ifndef ENOERR
    #define ENOERR 0
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#ifdef HAVE_WAITPID
    #include <sys/wait.h>
#endif 

#if WANT_MULTICAST
    #include "down7.h"
#endif
#include "ldm.h"
#include "ldm4.h"
#include "ldmfork.h"
#include "log.h"
#include "pq.h"
#ifndef HAVE_SETENV
    #include "setenv.h"
#endif
#include "priv.h"
#include "abbr.h"
#include "ldm_config_file.h"                // LDM configuration-file
#include "down6.h"              /* down6_destroy() */
#include "globals.h"
#include "child_process_set.h"
#include "inetutil.h"
#if WANT_MULTICAST
    #include "down7_manager.h"
    #include "mldm_sender_map.h"
    #include "up7.h"
    #include "UpMcastMgr.h"
#endif
#include "registry.h"
#include "remote.h"
#include "requester6.h"
#include "rpcutil.h"  /* clnt_errmsg() */
#include "up6.h"
#include "uldb.h"

#ifdef NO_ATEXIT
    #include "atexit.h"
#endif

#ifndef LDM_SELECT_TIMEO
    #define LDM_SELECT_TIMEO  6
#endif

static int      portIsMapped = 0;
static unsigned maxClients = 256;
static int      exit_status = 0;

static pid_t reap(
        pid_t pid,
        int options)
{
    pid_t wpid = 0;
    int status = 0;

#ifdef HAVE_WAITPID
    wpid = waitpid(pid, &status, options);
#else
    if(options == 0) {
        wpid = wait(&status);
    }
    /* customize here for older systems, use wait3 or whatever */
#endif
    if (wpid == -1) {
        if (!(errno == ECHILD && pid == -1)) /* Only complain when relevant */
            log_syserr_q("waitpid");
        return -1;
    }
    /* else */

    if (wpid != 0) {
        char command[512];
        int  nbytes = lcf_getCommandLine(wpid, command, sizeof(command));
        command[sizeof(command)-1] = 0;

#if !defined(WIFSIGNALED) && !defined(WIFEXITED)
#error "Can't decode wait status"
#endif

#if defined(WIFSTOPPED)
        if (WIFSTOPPED(status)) {
            log_notice_q(
                    nbytes <= 0
                        ? "child %ld stopped by signal %d"
                        : "child %ld stopped by signal %d: %*s",
                    (long)wpid, WSTOPSIG(status), nbytes, command);
        }
        else
#endif /*WIFSTOPPED*/
#if defined(WIFSIGNALED)
        if (WIFSIGNALED(status)) {
            cps_remove(wpid);       // Upstream LDM processes
            lcf_freeExec(wpid);     // EXEC processes
#if WANT_MULTICAST
            (void)msm_remove(wpid); // Multicast LDM senders
#endif

            log_notice_q(
                    nbytes <= 0
                        ? "child %ld terminated by signal %d"
                        : "child %ld terminated by signal %d: %*s",
                    (long)wpid, WTERMSIG(status), nbytes, command);

            /* DEBUG */
            switch (WTERMSIG(status)) {
            /*
             * If a child dumped core, shut everything down.
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
                log_notice_q("Killing (SIGTERM) process group");
                exit_status = 3;
                (void) kill(0, SIGTERM);
                break;
            }
        }
        else
#endif /*WIFSIGNALED*/
#if defined(WIFEXITED)
        if (WIFEXITED(status)) {
            cps_remove(wpid);       // Upstream LDM processes
            lcf_freeExec(wpid);     // EXEC processes
#if WANT_MULTICAST
            (void)msm_remove(wpid); // Multicast LDM senders
#endif
            int         exitStatus = WEXITSTATUS(status);
            log_level_t level = exitStatus ? LOG_LEVEL_NOTICE : LOG_LEVEL_INFO;
            log_log_q(level,
                    nbytes <= 0
                        ? "child %ld exited with status %d"
                        : "child %ld exited with status %d: %*s",
                    (long)wpid, exitStatus, nbytes, command);
        }
#endif /*WIFEXITED*/
    }

    return wpid;
}

/*
 * Called at exit.
 * This callback routine registered by atexit().
 */
static void cleanup(
        void)
{
    const char* const pqfname = getQueuePath();

    log_notice_q("Exiting");

    lcf_savePreviousProdInfo();

    free_remote_clss();

    clr_pip_5(); // Release COMINGSOON-reserved space in product-queue.
    down6_destroy();
#if WANT_MULTICAST
    up7_destroy();
#endif

    /*
     * Close product-queue.
     */
    if (pq) {
        (void) pq_close(pq);
        pq = NULL;
    }

    /*
     * Ensure that this process has no entry in the upstream LDM database and
     * that the database is closed.
     */
    (void) uldb_remove(getpid());
    (void) uldb_close();
    log_clear();

    if (getpid() == getpgrp()) {
        /*
         * This process is the process group leader (i.e., the top-level
         * LDM server).
         */
        if (portIsMapped) {
            int vers;

            /*
             * Superuser privileges might be required to unmap the
             * port on which the LDM is listening.
             */
            rootpriv();
                for (vers = MIN_LDM_VERSION; vers <= MAX_LDM_VERSION; vers++) {
                    if (!pmap_unset(LDMPROG, vers))
                        log_error_q("pmap_unset(LDMPROG %lu, LDMVERS %lu) "
                                "failed", LDMPROG, vers);
                    else
                        portIsMapped = 0;
                }
            unpriv();
        }

        /*
         * Terminate all child processes.
         */
        {
            /*
             * Ignore the signal I'm about to send my process group.
             */
            struct sigaction sigact;

            (void) sigemptyset(&sigact.sa_mask);
            sigact.sa_flags = 0;
            sigact.sa_handler = SIG_IGN;
            (void) sigaction(SIGTERM, &sigact, NULL );
        }

        /*
         * Signal my process group.
         */
        log_notice_q("Terminating process group");
        (void) kill(0, SIGTERM);

        while (reap(-1, 0) > 0)
            ; /*empty*/

        /*
         * Delete the upstream LDM database.
         */
        (void) uldb_delete(NULL);
    }

    /*
     * Free access-control-list resources.
     */
    lcf_free(); // eventually calls msm_destroy()

#if WANT_MULTICAST
    if (umm_clear())
        log_error_q("Couldn't clear multicast LDM sender manager");
    d7mgr_free();  // Clears multicast receiver manager
#endif

    /*
     * Close registry.
     */
    if (reg_close())
        log_flush_error();

    /*
     * Terminate logging.
     */
    log_fini();
}

/*
 * called upon receipt of signals
 */
static void signal_handler(
        int sig)
{
#ifdef SVR3SIGNALS
    /*
     * Some systems reset handler to SIG_DFL upon entry to handler.
     * In that case, we reregister our handler.
     */
    (void) signal(sig, signal_handler);
#endif
    switch (sig) {
    case SIGINT:
        log_notice_q("SIGINT received");
        exit(exit_status);
        /*NOTREACHED*/
    case SIGTERM:
        log_notice_q("SIGTERM received");
        done = 1;
        up6_close();
        req6_close();
#if WANT_MULTICAST
        down7_halt();
#endif
        return;
    case SIGUSR1:
        log_info_q("SIGUSR1 received");
        log_refresh();
        return;
    case SIGUSR2:
        log_info_q("SIGUSR2 received");
        log_roll_level();
        return;
    case SIGPIPE:
        log_notice("SIGPIPE received");
        return;
    case SIGCHLD:
        log_notice("SIGCHLD received");
        return;
    case SIGALRM:
        log_debug("SIGALRM received");
        return;
    }
}

/*
 * register the signal_handler
 */
static void set_sigactions(
        void)
{
    struct sigaction sigact;

    (void) sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Ignore these */
    sigact.sa_handler = SIG_IGN;
    (void) sigaction(SIGPIPE, &sigact, NULL );
    (void) sigaction(SIGCONT, &sigact, NULL );

    /* Handle these */
#ifdef SA_RESTART       /* SVR4, 4.3+ BSD */
    /* usually, restart system calls */
    sigact.sa_flags |= SA_RESTART;
#endif
    sigact.sa_handler = signal_handler;
    (void) sigaction(SIGUSR1, &sigact, NULL );
    (void) sigaction(SIGUSR2, &sigact, NULL );
    (void) sigaction(SIGCHLD, &sigact, NULL );

    /* Don't restart after alarms, interrupts, or termination */
    sigact.sa_flags = 0;
#ifdef SA_INTERRUPT     /* SunOS 4.x */
    sigact.sa_flags |= SA_INTERRUPT;
#endif
    (void) sigaction(SIGALRM, &sigact, NULL );
    (void) sigaction(SIGINT, &sigact, NULL );
    (void) sigaction(SIGTERM, &sigact, NULL );

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGPIPE);
    (void)sigaddset(&sigset, SIGCONT);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGCHLD);
    (void)sigaddset(&sigset, SIGALRM);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

static void usage(
        char *av0) /*  id string */
{
    const char* log_dest = log_get_default_daemon_destination();
    const char* config_path = getLdmdConfigPath();
    const char* pq_path = getDefaultQueuePath();
    (void) fprintf(stderr,
            "Usage: %s [options] [conf_filename]\n"
"\t(default conf_filename is \"%s\")\n"
"Options:\n"
"\t-I IP_addr      Use network interface associated with given IP \n"
"\t                address (default is all interfaces)\n"
"\t-P port         The port number for LDM connections (default is \n"
"\t                %d)\n"
"\t-v              Verbose logging mode: log each match (SIGUSR2\n"
"\t                cycles)\n"
"\t-x              Debug logging mode (SIGUSR2 cycles)\n"
"\t-l dest         Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"\t                (standard error), or file `dest`. If standard error is\n"
"\t                specified, then process will stay interactive. Default is\n"
"\t                \"%s\".\n"
"\t-M maxnum       Maximum number of clients (default is %u)\n"
"\t-q pqfname      Product-queue pathname (default is\n"
"\t                \"%s\")\n"
"\t-o offset       The \"from\" time of data-product requests will be\n"
"\t                no earlier than \"offset\" seconds ago (default is\n"
"\t                \"max_latency\", below)\n"
"\t-m max_latency  The maximum acceptable data-product latency in\n"
"\t                seconds (default is %d)\n"
"\t-n              Do nothing other than check the configuration-file\n"
"\t-t rpctimeo     Set LDM-5 RPC timeout to \"rpctimeo\" seconds\n"
"\t                (default is %d)\n",
            av0, config_path, LDM_PORT, log_dest, maxClients, pq_path,
            DEFAULT_OLDEST, DEFAULT_RPCTIMEO);
    exit(1);
}

/*
 * Create a TCP socket, bind it to a port, call 'listen' (we are a
 * server), and inform the portmap (rpcbind) service.  Does _not_ create
 * an RPC SVCXPRT or do the xprt_register() or svc_fds() stuff.
 *
 * Arguments:
 *      sockp           Pointer to socket (file) descriptor.  Set on and only on
 *                      success.
 *      localIpAddr     The IP address, in network byte order, of the 
 *                      local interface to use.  May be htonl(INADDR_ANY).
 *      localPort       The number of the local port on which the LDM server
 *                      should listen.
 * Returns:
 *      0               Success.
 *      EACCES          The process doesn't have appropriate privileges.
 *      EADDRINUSE      The local address is already in use.
 *      EADDRNOTAVAIL   The specified address is not available from the local
 *                      machine.
 *      EAFNOSUPPORT    The system doesn't support IP.
 *      EMFILE          No more file descriptors are available for this process.
 *      ENFILE          No more file descriptors are available for the system.
 *      ENOBUFS         Insufficient resources were available in the system.
 *      ENOMEM          Insufficient memory was available.
 *      ENOSR           There were insufficient STREAMS resources available.
 *      EOPNOTSUPP      The socket protocol does not support listen().
 *      EPROTONOSUPPORT The system doesn't support TCP.
 */
static int create_ldm_tcp_svc(
        int* sockp,
        in_addr_t localIpAddr,
        unsigned localPort)
{
    int error = 0; /* success */
    int sock;

    /*
     * Get a TCP socket.
     */
    log_debug("create_ldm_tcp_svc(): Getting TCP socket");
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        error = errno;

        log_syserr_q("Couldn't get socket for server");
    }
    else {
        (void)ensure_close_on_exec(sock);
        unsigned short port = (unsigned short) localPort;
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);

        /*
         * Eliminate problem with EADDRINUSE for reserved socket.
         * We get this if an upstream data source hasn't tried to
         * write on the other end and we are in FIN_WAIT_2
         */
        log_debug("create_ldm_tcp_svc(): Eliminating EADDRINUSE problem.");
        {
            int on = 1;

            (void) setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &on,
                    sizeof(on));
        }

        (void) memset(&addr, 0, len);
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = localIpAddr;
        addr.sin_port = (short) htons((short) port);

        /*
         * If privilege available, set it so we can bind to the port for LDM
         * services.  Also needed for the pmap_set() call.
         */
        log_debug("create_ldm_tcp_svc(): Getting root privs");
        rootpriv();
            log_debug("create_ldm_tcp_svc(): Binding socket");
            if (bind(sock, (struct sockaddr *) &addr, len) < 0) {
                error = errno;

                log_syserr_q("Couldn't obtain local address %s:%u for server",
                        inet_ntoa(addr.sin_addr), (unsigned) port);

                if (error == EACCES) {
                    error = 0;
                    addr.sin_port = 0; /* let system assign port */

                    if (bind(sock, (struct sockaddr *) &addr, len) < 0) {
                        error = errno;

                        log_syserr_q("Couldn't obtain local address %s for "
                                "server", inet_ntoa(addr.sin_addr));
                    }
                } /* requested port is reserved */
            } /* couldn't bind to requested port */

            if (!error) {
                /*
                 * Get the local address associated with the bound socket.
                 */
                log_debug("create_ldm_tcp_svc(): Calling getsockname()");
                if (getsockname(sock, (struct sockaddr *) &addr, &len) < 0) {
                    error = errno;

                    log_syserr_q("Couldn't get local address of server's socket");
                }
                else {
                    port = (short) ntohs((short) addr.sin_port);

                    log_notice_q("Using local address %s:%u",
                            inet_ntoa(addr.sin_addr), (unsigned) port);

                    log_debug("create_ldm_tcp_svc(): Calling listen()");
                    if (listen(sock, 32) != 0) {
                        error = errno;

                        log_syserr_q("Couldn't listen() on server's socket");
                    }
                    else {
#if 0
                        /*
                         * Register with the portmapper if it's running.  The
                         * check to see if it's running is made because on a
                         * FreeBSD 4.7-STABLE system, a pmap_set() call takes
                         * one minute even if the portmapper isn't running.
                         */
                        log_debug("create_ldm_tcp_svc(): Checking portmapper");
                        if (local_portmapper_running()) {
                            log_debug("create_ldm_tcp_svc(): Registering");

                            if (pmap_set(LDMPROG, 6, IPPROTO_TCP, port) == 0) {
                                log_add("Can't register TCP service %lu on "
                                        "port %u", LDMPROG, (unsigned) port);
                                log_add("Downstream LDMs won't be able to "
                                        "connect via the RPC portmapper daemon "
                                        "(rpcbind(8), portmap(8), etc.)");
                                log_flush_warning();
                            }
                            else {
                                portIsMapped = 1;

                                (void) pmap_set(LDMPROG, 5, IPPROTO_TCP, port);
                            }
                        } /* a local portmapper is running */
#endif

                        *sockp = sock;
                    } /* listen() success */
                } /* getsockname() success */
            } /* "sock" is bound to local address */
        /*
         * Done with the need for privilege.
         */
        log_debug("create_ldm_tcp_svc(): Releasing root privs");
        unpriv();

        if (error)
            (void) close(sock);
    } /* "sock" is open */

    return error;
}

/*
 * Handles an incoming RPC connection on a socket.  This method will fork(2)
 * a copy of this program, if appropriate, for handling incoming RPC messages.
 *
 * sock           The socket with the incoming RPC connection.
 */
static void handle_connection(
        int sock)
{
    struct sockaddr_in raddr;
    socklen_t len;
    int xp_sock;
    pid_t pid;
    SVCXPRT *xprt;
    int status = 1; /* EXIT_FAILURE assumed unless one_svc_run() success */
    peer_info* remote = get_remote();

    again: len = sizeof(raddr);
    (void) memset(&raddr, 0, len);

    xp_sock = accept(sock, (struct sockaddr *) &raddr, &len);

    (void) exitIfDone(exit_status);

    if (xp_sock < 0) {
        if (errno == EINTR) {
            errno = 0;
            goto again;
        }
        /* else */
        log_syserr_q("accept() failure");
        return;
    }

    (void)ensure_close_on_exec(xp_sock);

    /*
     * Don't bother continuing if no more clients are allowed.
     */
    if (cps_count() >= maxClients) {
        setremote(&raddr, xp_sock);
        log_notice_q("Denying connection from [%s] because too many clients",
                remote->astr);
        (void) close(xp_sock);
        return;
    }

    pid = ldmfork();
    if (pid == -1) {
        log_error_q("Couldn't fork process to handle incoming connection");
        /* TODO: try again?*/
        (void) close(xp_sock);
        return;
    }

    if (pid > 0) {
        /* parent */
        /* LOG_NOTICE("child %d", pid); */
        (void) close(xp_sock);

        if (cps_add(pid))
            log_syserr_q("Couldn't add child PID to set");

        return;
    }
    /* else child */

    setremote(&raddr, xp_sock);

    /* Access control */
    if (!lcf_isHostOk(remote)) {
        ensureRemoteName(&raddr);
        if (!lcf_isHostOk(remote)) {
            if (remote->printname == remote->astr) {
                log_notice_q("Denying connection from [%s] because not "
                        "allowed", remote->astr);
            }
            else {
                log_notice_q("Denying connection from \"%s\" because not "
                        "allowed", remote_name());
            }

            /*
             * Try to tell the other guy.
             * TODO: Why doesn't this work?
             */
            xprt = svcfd_create(xp_sock, remote->sendsz, remote->recvsz);
            if (xprt != NULL ) {
                xprt->xp_raddr = raddr;
                xprt->xp_addrlen = (int) len;
                svcerr_weakauth(xprt);
                svc_destroy(xprt);
            }

            goto unwind_sock;
        }
    }
    /* else */

    portIsMapped = 0; /* don't call pmap_unset() from child */

    (void) close(sock);

    /* Set the ulog identifier, optional. */
    log_set_id(remote_name());

    log_info_q("Connection from %s", remote_name());

    xprt = svcfd_create(xp_sock, remote->sendsz, remote->recvsz);
    if (xprt == NULL ) {
        log_error_q("Can't create fd service.");
        goto unwind_sock;
    }
    /* hook up the remote address to the xprt. */
    /* xprt->xp_raddr = raddr; */
    xprt->xp_raddr = raddr;
    xprt->xp_addrlen = (int) len;

    if (!svc_register(xprt, LDMPROG, 4, ldmprog_4, 0)) {
        log_error_q("unable to register LDM-4 service.");
        svc_destroy(xprt);
        goto unwind_sock;
    }

    if (!svc_register(xprt, LDMPROG, FIVE, ldmprog_5, 0)) {
        log_error_q("unable to register LDM-5 service.");
        svc_destroy(xprt);
        goto unwind_sock;
    }

    if (!svc_register(xprt, LDMPROG, SIX, ldmprog_6, 0)) {
        log_error_q("unable to register LDM-6 service.");
        svc_destroy(xprt);
        goto unwind_sock;
    }

#if WANT_MULTICAST
    if (!svc_register(xprt, LDMPROG, SEVEN, ldmprog_7, 0)) {
        log_error_q("unable to register LDM-7 service.");
        svc_destroy(xprt);
        goto unwind_sock;
    }
#endif

    /*
     *  handle rpc requests
     */
    {
        const unsigned  TIMEOUT = 2*interval;
        status = one_svc_run(xp_sock, TIMEOUT);
        if (status == 0) {
            log_info_q("Done");
        }
        else if (status == ETIMEDOUT) {
            log_notice_q("Connection from client LDM silent for %u seconds",
                    TIMEOUT);
        }
        else { /* connection to client lost */
            log_info_q("Connection with client LDM closed");
            status = 0; /* EXIT_SUCCESS */
        }
    }

    /* svc_destroy(xprt);  done by svc_getreqset() */

    unwind_sock: (void) close(xp_sock);

    exit(status); // `cleanup()` will release acquired resources
}

static void sock_svc(
        const int  sock)
{
    const int width = sock + 1;

    while (exitIfDone(exit_status)) {
        int ready;
        fd_set readfds;
        struct timeval stimeo;

        stimeo.tv_sec = LDM_SELECT_TIMEO;
        stimeo.tv_usec = 0;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        ready = select(width, &readfds, 0, 0, &stimeo);

        if (ready < 0) {
            /*
             * Handle EINTR as a special case.
             */
            if (errno != EINTR) {
                log_syserr_q("sock select");
                done = 1;
                exit(1);
            }
        }
        else if (ready > 0) {
            /*
             * Do some work.
             */
            handle_connection(sock);
        }

        /*
         * Wait on any children which may have died
         */
        while (reap(-1, WNOHANG) > 0)
            /* empty */;
    }
}

int main(
        int ac,
        char* av[])
{
    int         status;
    int         doSomething = 1;
    in_addr_t   ldmIpAddr = (in_addr_t) htonl(INADDR_ANY );
    unsigned    ldmPort = LDM_PORT;
    unsigned    logOpts = 0;
    bool        becomeDaemon = true; // default

    (void)log_init(av[0]);
    ensureDumpable();
    const char* pqfname = getQueuePath();

    /*
     * deal with the command line, set options
     */
    {
        extern int optind;
        extern int opterr;
        extern char *optarg;
        int ch;

        opterr = 1;

        while ((ch = getopt(ac, av, "I:vxl:nq:o:P:M:m:t:")) != EOF) {
            switch (ch) {
            case 'I': {
                in_addr_t ipAddr = inet_addr(optarg);

                if ((in_addr_t) -1 == ipAddr) {
                    (void) fprintf(stderr, "Interface specification \"%s\" "
                            "isn't an IP address\n", optarg);
                    exit(1);
                }

                ldmIpAddr = ipAddr;

                break;
            }
            case 'v':
                if (!log_is_enabled_info)
                    (void)log_set_level(LOG_LEVEL_INFO);
                break;
            case 'x':
                if (!log_is_enabled_debug)
                    (void)log_set_level(LOG_LEVEL_DEBUG);
                break;
            case 'l':
                (void)log_set_destination(optarg);
                becomeDaemon = strcmp(optarg, "-") != 0;
                break;
            case 'q':
                pqfname = optarg;
                break;
            case 'o':
                toffset = atoi(optarg);
                if (toffset == 0 && *optarg != '0') {
                    (void) fprintf(stderr, "%s: invalid offset %s\n", av[0],
                            optarg);
                    usage(av[0]);
                }
                break;
            case 'P': {
                unsigned port;
                int      nbytes;
                if (sscanf(optarg, "%5u %n", &port, &nbytes) != 1 ||
                        0 != optarg[nbytes] || port > 0xffff) {
                    (void)fprintf(stderr, "%s: invalid port number: %s\n",
                            av[0], optarg);
                    usage(av[0]);
                }
                ldmPort = port;
                break;
            }
            case 'M': {
                int max = atoi(optarg);
                if (max < 0) {
                    (void) fprintf(stderr,
                            "%s: invalid maximum number of clients %s\n", av[0],
                            optarg);
                    usage(av[0]);
                }
                maxClients = max;
                break;
            }
            case 'm':
                max_latency = atoi(optarg);
                if (max_latency <= 0) {
                    (void) fprintf(stderr, "%s: invalid max_latency %s\n",
                            av[0], optarg);
                    usage(av[0]);
                }
                break;
            case 'n':
                doSomething = 0;
                break;
            case 't':
                rpctimeo = (unsigned) atoi(optarg);
                if (rpctimeo == 0 || rpctimeo > 32767) {
                    (void) fprintf(stderr, "%s: invalid timeout %s", av[0],
                            optarg);
                    usage(av[0]);
                }
                break;
            case '?':
                usage(av[0]);
                break;
            } /* "switch" statement */
        } /* argument loop */

        if (ac - optind == 1)
            setLdmdConfigPath(av[optind]);

        if (toffset != TOFFSET_NONE && toffset > max_latency) {
            (void) fprintf(stderr,
                    "%s: invalid toffset (%d) > max_latency (%d)\n", av[0],
                    toffset, max_latency);
            usage(av[0]);
        }
    } /* command-line argument decoding */

    setQueuePath(pqfname);

    /*
     * Vet the configuration file.
     */
    log_debug("main(): Vetting configuration-file");
    if (read_conf(getLdmdConfigPath(), 0, ldmIpAddr, ldmPort) != 0) {
        log_flush_error();
        exit(1);
    }
    if (!lcf_haveSomethingToDo()) {
        log_error_q("The LDM configuration-file \"%s\" is effectively empty",
                getLdmdConfigPath());
        exit(1);
    }
    // lcf_free() not called because lcf_isServerNeeded() will be

    if (!becomeDaemon) {
        /*
         * Make this process a process group leader so that all child processes
         * (e.g., upstream LDM, downstream LDM, pqact(1)s) will be signaled by
         * `cleanup()`.
         */
        (void)setpgid(0, 0); // can't fail
    }
#ifndef DONTFORK
    else {
        /*
         * Make this process a daemon.
         */
        pid_t pid;
        pid = ldmfork();
        if (pid == -1) {
            log_error_q("Couldn't fork LDM daemon");
            exit(2);
        }

        if (pid > 0) {
            /* parent */
            (void)printf("%ld\n", (long)pid);
            exit(0);
        }

        /*
         * Make the child a session leader so that it is no longer affected by
         * the parent's process group.
         */
        (void)setsid(); // also makes this process a process group leader
        log_avoid_stderr(); // Because this process is now a daemon
        (void)close(STDERR_FILENO);
        (void)open_on_dev_null_if_closed(STDERR_FILENO, O_RDWR);
    }
#endif
    /* Set up fd 0,1 */
    (void)close(STDIN_FILENO);
    (void)open_on_dev_null_if_closed(STDIN_FILENO, O_RDONLY);
    (void)close(STDOUT_FILENO);
    (void)open_on_dev_null_if_closed(STDOUT_FILENO, O_WRONLY);

    logfname = log_get_destination();

    log_notice_q("Starting Up (version: %s; built: %s %s)", PACKAGE_VERSION,
            __DATE__, __TIME__);

    /*
     * register exit handler
     */
    if (atexit(cleanup) != 0) {
        log_syserr_q("atexit");
        log_notice_q("Exiting");
        exit(1);
    }

    /*
     * set up signal handlers
     */
    set_sigactions();

    if (doSomething) {
        int sock = -1;

        if (lcf_isServerNeeded()) {
            /*
             * Create a service portal. This should be done before anything is
             * created because this is the function that relinquishes superuser
             * privileges.
             */
            log_debug("main(): Creating service portal");
            if (create_ldm_tcp_svc(&sock, ldmIpAddr, ldmPort) != ENOERR) {
                /* error reports are emitted from create_ldm_tcp_svc() */
                exit(1);
            }
            log_debug("tcp sock: %d", sock);
        }

        /*
         * Verify that the product-queue can be open for writing.
         */
        log_debug("main(): Opening product-queue");
        if ((status = pq_open(pqfname, PQ_DEFAULT, &pq))) {
            if (PQ_CORRUPT == status) {
                log_error_q("The product-queue \"%s\" is inconsistent", pqfname);
            }
            else {
                log_error_q("pq_open failed: %s: %s", pqfname, strerror(status));
            }
            exit(1);
        }
        (void) pq_close(pq);
        pq = NULL;

        /*
         * Create the sharable database of upstream LDM metadata.
         */
        log_debug("main(): Creating shared upstream LDM database");
        if ((status = uldb_delete(NULL))) {
            if (ULDB_EXIST == status) {
                log_clear();
            }
            else {
                log_error_q(
                        "Couldn't delete existing shared upstream LDM database");
                exit(1);
            }
        }
        if (uldb_create(NULL, maxClients * 1024)) {
            log_error_q("Couldn't create shared upstream LDM database");
            exit(1);
        }

        /*
         * Initialize the multicast sender map.
         */
#if WANT_MULTICAST
        if (msm_init()) {
            log_error_q("Couldn't initialize multicast LDM sender map");
            exit(1);
        }
#endif

        /*
         * Re-read (and execute) the configuration file (downstream LDM-s are
         * started).
         */
        lcf_free(); // Prevent duplicates
        log_debug("main(): Reading configuration-file");
        if (read_conf(getLdmdConfigPath(), 1, ldmIpAddr, ldmPort) != 0) {
            log_flush_error();
            exit(1);
        }

        if (lcf_isServerNeeded()) {
            /*
             * Serve
             */
            log_debug("main(): Serving socket");
            sock_svc(sock);
        }
        else {
            /*
             * Wait until all child processes have terminated.
             */
            while (reap(-1, 0) > 0)
                /* empty */;
        }
    }   // configuration-file will be executed

    return (exit_status);
}
