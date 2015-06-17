/*
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 */

/* 
 * ldm server mainline program module
 */

#include <config.h>

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

#include "ldm.h"
#include "ldm4.h"
#include "ldmfork.h"
#include "log.h"
#include "ulog.h"
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
    #include "mldm_sender_map.h"
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

static int portIsMapped = 0;
static unsigned maxClients = 256;

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
            serror("waitpid");
        return -1;
    }
    /* else */

    if (wpid != 0) {
        char command[512];

#if !defined(WIFSIGNALED) && !defined(WIFEXITED)
#error "Can't decode wait status"
#endif

#if defined(WIFSTOPPED)
        if (WIFSTOPPED(status)) {
            int n = lcf_getCommandLine(wpid, command, sizeof(command));

            if (n == -1) {
                log_add("Couldn't get command-line of EXEC process "
                        "%ld", wpid);
                log_log(LOG_ERR);
            }

            unotice(
                    n <= 0 ?
                            "child %d stopped by signal %d" :
                            "child %d stopped by signal %d: %*s", wpid,
                    WSTOPSIG(status), n, command);
        }
        else
#endif /*WIFSTOPPED*/
#if defined(WIFSIGNALED)
        if (WIFSIGNALED(status)) {
            int n = lcf_getCommandLine(wpid, command, sizeof(command));

            if (n == -1) {
                log_add("Couldn't get command-line of EXEC process "
                        "%ld", wpid);
                log_log(LOG_ERR);
            }

            cps_remove(wpid); /* upstream LDM processes */

            lcf_freeExec(wpid); /* EXEC processes */

#if WANT_MULTICAST
            (void)msm_removePid(wpid); // multicast LDM senders
#endif

            unotice(
                    n <= 0 ?
                            "child %d terminated by signal %d" :
                            "child %d terminated by signal %d: %*s", wpid,
                    WTERMSIG(status), n, command);

            /* DEBUG */
            switch (WTERMSIG(status)) {
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
                unotice("Killing (SIGTERM) process group");
                (void) kill(0, SIGTERM);
                break;
            }
        }
        else
#endif /*WIFSIGNALED*/
#if defined(WIFEXITED)
        if (WIFEXITED(status)) {
            int exitStatus = WEXITSTATUS(status);
            int n = lcf_getCommandLine(wpid, command, sizeof(command));

            cps_remove(wpid); /* upstream LDM processes */
            lcf_freeExec(wpid); /* EXEC processes */

            if (n == -1) {
                log_add("Couldn't get command-line of EXEC process "
                        "%ld", wpid);
                log_log(LOG_ERR);
            }

            log_start(
                    n <= 0 ?
                            "child %d exited with status %d" :
                            "child %d exited with status %d: %*s", wpid,
                    exitStatus, n, command);
            log_log(exitStatus ? LOG_NOTICE : LOG_INFO);
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

    unotice("Exiting");

    lcf_savePreviousProdInfo();

    free_remote_clss();

    /*
     * Ensure release of COMINGSOON-reserved space in product-queue.
     */
    clr_pip_5();
    down6_destroy();

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
                    uerror("pmap_unset(LDMPROG %lu, LDMVERS %lu) "
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
        unotice("Terminating process group");
        (void) kill(0, SIGTERM);

        while (reap(-1, 0) > 0)
            ; /*empty*/

        /*
         * Delete the upstream LDM database.
         */
        (void) uldb_delete(NULL);

#if WANT_MULTICAST
        /*
         * Destroy the multicast LDM sender map.
         */
        msm_destroy();
#endif
    }

    /*
     * Free access-control-list resources.
     */
    lcf_free();

    /*
     * Close registry.
     */
    if (reg_close())
        log_log(LOG_ERR);

    /*
     * Terminate logging.
     */
    (void) closeulog();
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
    case SIGHUP:
        return;
    case SIGINT:
        exit(0);
        /*NOTREACHED*/
    case SIGTERM:
        up6_close();
        req6_close();
        done = 1;
        return;
    case SIGUSR2:
        rollulogpri();
        return;
    case SIGPIPE:
        return;
    case SIGCHLD:
        return;
    case SIGALRM:
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
    (void) sigaction(SIGHUP, &sigact, NULL );
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
}

static void usage(
        char *av0) /*  id string */
{
    (void) fprintf(stderr,
            "Usage: %s [options] [conf_filename]\n"
                    "\t(default conf_filename is \"%s\")\n"
                    "Options:\n"
                    "\t-I IP_addr      Use network interface associated with given IP \n"
                    "\t                address (default is all interfaces)\n"
                    "\t-P port         The port number for LDM connections (default is \n"
                    "\t                %d)\n"
                    "\t-x              Debug logging mode (SIGUSR2 cycles)\n"
                    "\t-v              Verbose logging mode: log each match (SIGUSR2\n"
                    "\t                cycles)\n"
                    "\t-l logfile      Log to given file (default uses syslogd)\n"
                    "\t-q pqfname      Product-queue pathname (default is\n"
                    "\t                \"%s\")\n"
                    "\t-o offset       The \"from\" time of data-product requests will be\n"
                    "\t                no earlier than \"offset\" seconds ago (default is\n"
                    "\t                \"max_latency\", below)\n"
                    "\t-m max_latency  The maximum acceptable data-product latency in\n"
                    "\t                seconds (default is %d)\n"
                    "\t-n              Do nothing other than check the configuration-file\n"
                    "\t-t rpctimeo     Set LDM-5 RPC timeout to \"rpctimeo\" seconds\n"
                    "\t                (default is %d)\n", av0,
            getLdmdConfigPath(), LDM_PORT, getQueuePath(), DEFAULT_OLDEST,
            DEFAULT_RPCTIMEO);

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
    udebug("create_ldm_tcp_svc(): Getting TCP socket");
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        error = errno;

        serror("Couldn't get socket for server");
    }
    else {
        unsigned short port = (unsigned short) localPort;
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);

        /*
         * Eliminate problem with EADDRINUSE for reserved socket.
         * We get this if an upstream data source hasn't tried to
         * write on the other end and we are in FIN_WAIT_2
         */
        udebug("create_ldm_tcp_svc(): Eliminating EADDRINUSE problem.");
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
        udebug("create_ldm_tcp_svc(): Getting root privs");
        rootpriv();

        udebug("create_ldm_tcp_svc(): Binding socket");
        if (bind(sock, (struct sockaddr *) &addr, len) < 0) {
            error = errno;

            serror("Couldn't obtain local address %s:%u for server",
                    inet_ntoa(addr.sin_addr), (unsigned) port);

            if (error == EACCES) {
                error = 0;
                addr.sin_port = 0; /* let system assign port */

                if (bind(sock, (struct sockaddr *) &addr, len) < 0) {
                    error = errno;

                    serror("Couldn't obtain local address %s:* for server",
                            inet_ntoa(addr.sin_addr));
                }
            } /* requested port is reserved */
        } /* couldn't bind to requested port */

        if (!error) {
            /*
             * Get the local address associated with the bound socket.
             */
            udebug("create_ldm_tcp_svc(): Calling getsockname()");
            if (getsockname(sock, (struct sockaddr *) &addr, &len) < 0) {
                error = errno;

                serror("Couldn't get local address of server's socket");
            }
            else {
                port = (short) ntohs((short) addr.sin_port);

                unotice("Using local address %s:%u", inet_ntoa(addr.sin_addr),
                        (unsigned) port);

                udebug("create_ldm_tcp_svc(): Calling listen()");
                if (listen(sock, 32) != 0) {
                    error = errno;

                    serror("Couldn't listen() on server's socket");
                }
                else {
                    /*
                     * Register with the portmapper if it's running.  The
                     * check to see if it's running is made because on a
                     * FreeBSD 4.7-STABLE system, a pmap_set() call takes
                     * one minute even if the portmapper isn't running.
                     */
                    udebug("create_ldm_tcp_svc(): Checking portmapper");
                    if (local_portmapper_running()) {
                        udebug("create_ldm_tcp_svc(): Registering");

                        if (pmap_set(LDMPROG, 6, IPPROTO_TCP, port) == 0) {
                            uwarn("Can't register TCP service %lu on "
                                    "port %u", LDMPROG, (unsigned) port);
                            uwarn("Downstream LDMs won't be able to "
                                    "connect via the RPC portmapper daemon "
                                    "(rpcbind(8), portmap(8), etc.)");
                        }
                        else {
                            portIsMapped = 1;

                            (void) pmap_set(LDMPROG, 5, IPPROTO_TCP, port);
                        }
                    } /* a local portmapper is running */

                    /*
                     * Done with the need for privilege.
                     */
                    udebug("create_ldm_tcp_svc(): Releasing root privs");
                    unpriv();

                    *sockp = sock;
                } /* listen() success */
            } /* getsockname() success */
        } /* "sock" is bound to local address */

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

    (void) exitIfDone(0);

    if (xp_sock < 0) {
        if (errno == EINTR) {
            errno = 0;
            goto again;
        }
        /* else */
        serror("accept");
        return;
    }

    /*
     * Don't bother continuing if no more clients are allowed.
     */
    if (cps_count() >= maxClients) {
        setremote(&raddr, xp_sock);
        unotice("Denying connection from [%s] because too many clients",
                remote->astr);
        (void) close(xp_sock);
        return;
    }

    pid = ldmfork();
    if (pid == -1) {
        log_add("Couldn't fork process to handle incoming connection");
        log_log(LOG_ERR);
        /* TODO: try again?*/
        (void) close(xp_sock);
        return;
    }

    if (pid > 0) {
        /* parent */
        /* unotice("child %d", pid); */
        (void) close(xp_sock);

        if (cps_add(pid))
            serror("Couldn't add child PID to set");

        return;
    }
    /* else child */

    setremote(&raddr, xp_sock);

    /* Access control */
    if (!lcf_isHostOk(remote)) {
        ensureRemoteName(&raddr);
        if (!lcf_isHostOk(remote)) {
            if (remote->printname == remote->astr) {
                unotice("Denying connection from [%s] because not "
                        "allowed", remote->astr);
            }
            else {
                unotice("Denying connection from \"%s\" because not "
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

    endpriv();
    portIsMapped = 0; /* don't call pmap_unset() from child */

    (void) close(sock);

    /* Set the ulog identifier, optional. */
    set_abbr_ident(remote_name(), NULL );

    uinfo("Connection from %s", remote_name());

    xprt = svcfd_create(xp_sock, remote->sendsz, remote->recvsz);
    if (xprt == NULL ) {
        uerror("Can't create fd service.");
        goto unwind_sock;
    }
    /* hook up the remote address to the xprt. */
    /* xprt->xp_raddr = raddr; */
    xprt->xp_raddr = raddr;
    xprt->xp_addrlen = (int) len;

    if (!svc_register(xprt, LDMPROG, 4, ldmprog_4, 0)) {
        uerror("unable to register LDM-4 service.");
        svc_destroy(xprt);
        goto unwind_sock;
    }

    if (!svc_register(xprt, LDMPROG, FIVE, ldmprog_5, 0)) {
        uerror("unable to register LDM-5 service.");
        svc_destroy(xprt);
        goto unwind_sock;
    }

    if (!svc_register(xprt, LDMPROG, SIX, ldmprog_6, 0)) {
        uerror("unable to register LDM-6 service.");
        svc_destroy(xprt);
        goto unwind_sock;
    }

#if WANT_MULTICAST
    if (!svc_register(xprt, LDMPROG, SIX, ldmprog_7, 0)) {
        uerror("unable to register LDM-7 service.");
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

        (void) exitIfDone(0);

        if (status == 0) {
            log_add("Done");
            log_log(LOG_INFO);
        }
        else if (status == ETIMEDOUT) {
            log_add("Connection from client LDM silent for %u seconds",
                    TIMEOUT);
            log_log(LOG_NOTICE);
        }
        else { /* connection to client lost */
            log_add("Connection with client LDM closed");
            log_log(LOG_INFO);
            status = 0; /* EXIT_SUCCESS */
        }
    }

    /* svc_destroy(xprt);  done by svc_getreqset() */

    unwind_sock: (void) close(xp_sock);

    exit(status);
}

static void sock_svc(
        const int  sock)
{
    const int width = sock + 1;

    while (exitIfDone(0)) {
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
                serror("sock select");
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
    const char* pqfname = getQueuePath();
    int status;
    int doSomething = 1;
    in_addr_t locIpAddr = (in_addr_t) htonl(INADDR_ANY );
    unsigned ldmPort = LDM_PORT;

    ensureDumpable();

    /*
     * deal with the command line, set options
     */
    {
        extern int optind;
        extern int opterr;
        extern char *optarg;
        int ch;
        int logmask = LOG_MASK(LOG_ERR) | LOG_MASK(LOG_WARNING)
                | LOG_MASK(LOG_NOTICE);

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

                locIpAddr = ipAddr;

                break;
            }
            case 'v':
                logmask |= LOG_MASK(LOG_INFO);
                break;
            case 'x':
                logmask |= LOG_MASK(LOG_DEBUG);
                break;
            case 'l':
                logfname = optarg;
                break;
            case 'q':
                pqfname = optarg;
                setQueuePath(optarg);
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
                char* suffix = "";
                long port;

                errno = 0;
                port = strtol(optarg, &suffix, 0);

                if (0 != errno || 0 != *suffix || 0 >= port || 0xffff < port) {

                    (void) fprintf(stderr, "%s: invalid port %s\n", av[0],
                            optarg);
                    usage(av[0]);
                }

                ldmPort = (unsigned) port;

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
        (void) setulogmask(logmask);

        if (toffset != TOFFSET_NONE && toffset > max_latency) {
            (void) fprintf(stderr,
                    "%s: invalid toffset (%d) > max_latency (%d)\n", av[0],
                    toffset, max_latency);
            usage(av[0]);
        }
    } /* command-line argument decoding */

#ifndef DONTFORK
    /* 
     * daemon behavior
     *
     * Background the process unless we are logging to stderr, in which
     * case we assume interactive.
     */
    if (logfname == NULL || *logfname != '-') {
        /* detach */
        pid_t pid;
        pid = ldmfork();
        if (pid == -1) {
            log_add("Couldn't fork LDM daemon");
            log_log(LOG_ERR);
            exit(2);
        }

        if (pid > 0) {
            /* parent */
            (void) printf("%ld\n", (long) pid);
            exit(0);
        }

        /* detach the child from parents process group ?? */
        (void) setsid();
    }
#endif

    /*
     * Initialize logger.
     * (Close fd 2 to remap stderr to the logfile, when
     * appropriate. I know, this is anal.)
     */
    if (logfname == NULL )
        (void) fclose(stderr);
    else if (!(logfname[0] == '-' && logfname[1] == 0))
        (void) close(2);
    (void) openulog(ubasename(av[0]), (LOG_CONS | LOG_PID), LOG_LDM, logfname);
    unotice("Starting Up (version: %s; built: %s %s)", PACKAGE_VERSION,
            __DATE__, __TIME__);

    /*
     * register exit handler
     */
    if (atexit(cleanup) != 0) {
        serror("atexit");
        unotice("Exiting");
        exit(1);
    }

    /*
     * set up signal handlers
     */
    set_sigactions();

    /*
     * Close the standard input and standard output streams because they won't
     * be used (more anality :-)
     */
    (void) fclose(stdout);
    (void) fclose(stdin);

    /*
     * Vet the configuration file.
     */
    udebug("main(): Vetting configuration-file");
    if (read_conf(getLdmdConfigPath(), 0, ldmPort) != 0) {
        log_log(LOG_ERR);
        exit(1);
    }

    if (doSomething) {
        int sock = -1;

        if (lcf_isServerNeeded()) {
            /*
             * Create a service portal. This should be done before anything is
             * created because this is the function that relinquishes superuser
             * privileges.
             */
            udebug("main(): Creating service portal");
            if (create_ldm_tcp_svc(&sock, locIpAddr, ldmPort) != ENOERR) {
                /* error reports are emitted from create_ldm_tcp_svc() */
                exit(1);
            }
            udebug("tcp sock: %d", sock);
        }

        /*
         * Verify that the product-queue can be open for writing.
         */
        udebug("main(): Opening product-queue");
        if ((status = pq_open(pqfname, PQ_DEFAULT, &pq))) {
            if (PQ_CORRUPT == status) {
                uerror("The product-queue \"%s\" is inconsistent", pqfname);
            }
            else {
                uerror("pq_open failed: %s: %s", pqfname, strerror(status));
            }
            exit(1);
        }
        (void) pq_close(pq);
        pq = NULL;

        /*
         * Create the sharable database of upstream LDM metadata.
         */
        udebug("main(): Creating shared upstream LDM database");
        if ((status = uldb_delete(NULL))) {
            if (ULDB_EXIST == status) {
                log_clear();
            }
            else {
                LOG_ADD0(
                        "Couldn't delete existing shared upstream LDM database");
                log_log(LOG_ERR);
                exit(1);
            }
        }
        if (uldb_create(NULL, maxClients * 1024)) {
            LOG_ADD0("Couldn't create shared upstream LDM database");
            log_log(LOG_ERR);
            exit(1);
        }

        /*
         * Initialize the multicast sender map.
         */
#if WANT_MULTICAST
        if (msm_init()) {
            LOG_ADD0("Couldn't initialize multicast LDM sender map");
            log_log(LOG_ERR);
            exit(1);
        }
#endif

        /*
         * Read the configuration file (downstream LDM-s are started).
         */
        lcf_free();
        udebug("main(): Reading configuration-file");
        if (read_conf(getLdmdConfigPath(), 1, ldmPort) != 0) {
            log_log(LOG_ERR);
            exit(1);
        }

        if (lcf_isServerNeeded()) {
            /*
             * Serve
             */
            udebug("main(): Serving socket");
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

    return (0);
}
