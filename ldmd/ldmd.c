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
    #include "up7.h"
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
#include "down6.h"              /* down6_destroy() */
#include "globals.h"
#include "child_process_set.h"
#include "inetutil.h"
#include "LdmConfFile.h"                // LDM configuration-file
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

static int      doSomething = 1; ///< Do something or just check config-file?
static unsigned maxClients = 256;
static int      exit_status = 0;

#define MAXFD 64

static void
Signal(int sig, void(*action)(int))
{
    struct sigaction sigact;

    (void)sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = action;
    (void)sigaction(SIGHUP, &sigact, NULL);
}

/**
 * Converts the current process into a daemon. Adapted from section 13.4 of
 * "Unix Network Programming" Volume 1, Third Edition, by Richard Stevens.
 *
 * @retval 0      Success
 * @retval 1      fork(2) failure. `log_add()` called.
 * @retval 2      setsid(2) failure. `log_add()` called.
 */
static int
daemonize()
{
    pid_t pid = fork();
    if (pid < 0) {
        log_add_syserr("fork() failure");
        return 1;
    }

    if (pid > 0) {
        // Parent
        exit(0);
    }

    // Child 1 continues...

    // Become the session leader
    if (setsid() < 0) {
        log_add_syserr("setsid() failure");
        return 2;
    }

    /*
     * Ignore the SIGHUP that the second child process will receive when the
     * session leader terminates.
     */
    Signal(SIGHUP, SIG_IGN);

    /*
     * Fork a second time to guarantee that the daemon isn't a session leader
     * and, consequently, cannot acquire a controlling terminal by opening a
     * terminal device in the future.
     *
     * "Paranoia strikes deep. Into your life it will creep."
     */
    if ((pid = fork()) < 0) {
        log_add_syserr("fork() failure");
        return 1;
    }

    if (pid) {
        // Parent
        (void)printf("%ld\n", (long)pid);
        exit(0); // Child 1 terminates
    }

    // Child 2 continues...

    // Close the file descriptors of the standard I/O streams
    for (int i = 0; i < 3; ++i)
        close(i);

    /*
     * Open the standard input, output, and error streams on "/dev/null" so that
     * their accidental use -- by a library function, for example -- doesn't
     * cause an error. Assume the associated file-descriptors are closed.
     */
    (void)open("/dev/null", O_RDONLY); // File-descriptor 0 (stdin)
    (void)open("/dev/null", O_RDWR);   // File-descriptor 1 (stdout)
    (void)open("/dev/null", O_RDWR);   // File-descriptor 2 (stderr)

    return 0;
}

/**
 * Sends a SIGTERM to the process group as root.
 */
static void killProcGroup()
{
    // Become root to ensure that even setuid processes owned by root are signaled
    rootpriv();
    (void)kill(0, SIGTERM); // Sends SIGTERM to process group
    unpriv();               // Relinquish root privileges
}

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
            log_syserr("waitpid");
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
            log_add(
                    nbytes <= 0
                        ? "child %ld stopped by signal %d"
                        : "child %ld stopped by signal %d: %*s",
                    (long)wpid, WSTOPSIG(status), nbytes, command);
            log_flush_notice();
        }
        else
#endif /*WIFSTOPPED*/
#if defined(WIFSIGNALED)
        if (WIFSIGNALED(status)) {
            cps_remove(wpid);       // Upstream LDM processes
            lcf_freeExec(wpid);     // EXEC processes
#if WANT_MULTICAST
            if (umm_remove(wpid) == LDM7_NOENT) // Multicast LDM senders
                log_clear();
#endif

            log_add(
                    nbytes <= 0
                        ? "child %ld terminated by signal %d"
                        : "child %ld terminated by signal %d: %*s",
                    (long)wpid, WTERMSIG(status), nbytes, command);
            log_flush_notice();

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
                log_add("Killing (SIGTERM) process group");
                log_flush_notice();
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
            if (umm_remove(wpid) == LDM7_NOENT) // Multicast LDM senders
                log_clear();
#endif
            int exitStatus = WEXITSTATUS(status);
			if (nbytes <= 0) {
				log_add("Child %ld exited with status %d",
						(long)wpid, exitStatus);
			}
			else {
			    log_add("Child %ld exited with status %d: %*s",
                    (long)wpid, exitStatus, nbytes, command);
			}
			log_flush(exitStatus ? LOG_LEVEL_NOTICE : LOG_LEVEL_INFO);
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
    log_add("Exiting");
    log_flush_notice();

    lcf_savePreviousProdInfo();

    free_remote_clss();

    clr_pip_5(); // Release COMINGSOON-reserved space in product-queue.
    down6_destroy();
#if WANT_MULTICAST
    up7_destroy();
    if (down7_isInit()) {
    	down7_halt();
		down7_destroy();
    }
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
    log_clear();
    (void) uldb_close();
    log_clear();

    const bool isTopProc = getpid() == getpgrp();

    if (isTopProc) {
        /*
         * This process is the process group leader (i.e., the top-level
         * LDM server).
         */

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
            (void) sigaction(SIGTERM, &sigact, NULL);
        }

        /*
         * Signal my process group.
         */
        log_add("Terminating process group");
        log_flush_notice();
        killProcGroup();

        while (reap(-1, 0) > 0)
            ; /*empty*/

        /*
         * Delete the upstream LDM database.
         */
       if (doSomething)
		   (void) uldb_delete(NULL);

        /*
         * Destroy the LDM configuration-file module.
         */
        lcf_destroy(true);
    }

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
    switch (sig) {
    case SIGINT:
        exit(exit_status);
        /*NOTREACHED*/
    case SIGTERM:
        done = 1;
        up6_close();
        req6_close();
#if WANT_MULTICAST
        down7_halt();
#endif
        return;
    case SIGHUP:
        return;
    case SIGUSR1:
        log_refresh(); // Will close/open on next log message; not before
        return;
    case SIGUSR2:
        log_roll_level();
        return;
    case SIGPIPE:
        return;
    case SIGCHLD:
        return;
    case SIGALRM:
        return;
    default:
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

    // Ignore these
    sigact.sa_handler = SIG_IGN;
    (void)sigaction(SIGPIPE, &sigact, NULL);
    (void)sigaction(SIGCONT, &sigact, NULL);

    // Catch everything else
    sigact.sa_handler = signal_handler;

    // Don't restart after catching these
    (void)sigaction(SIGALRM, &sigact, NULL);
    (void)sigaction(SIGINT, &sigact, NULL);
    (void)sigaction(SIGTERM, &sigact, NULL);
    (void)sigaction(SIGHUP, &sigact, NULL);

    // Restart after catching these
    sigact.sa_flags |= SA_RESTART;
    (void)sigaction(SIGUSR1, &sigact, NULL);
    (void)sigaction(SIGUSR2, &sigact, NULL);
    (void)sigaction(SIGCHLD, &sigact, NULL);

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
    (void)sigaddset(&sigset, SIGHUP);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

static void usage(
        char*     av0,
        const int exitStatus) /*  id string */
{
    const char* log_dest = log_get_default_daemon_destination();
    const char* config_path = getLdmdConfigPath();
    const char* pq_path = getDefaultQueuePath();
    (void) fprintf(stderr,
            "Usage:\n"
            "    %s -h\n"
            "    %s [options] [-v|-x] [conf_filename]\n"
"Options:\n"
"    -h              Print this usage message and then exit\n"
"    -I IP_addr      Use network interface associated with given IP address.\n"
"                    Default is all interfaces.\n"
"    -P port         The port number for LDM connections. Default is %d.\n"
"    -l dest         Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"                    (standard error), or file `dest`. If standard error is\n"
"                    specified, then process will stay interactive. Default is\n"
"                    \"%s\".\n"
"    -M maxnum       Maximum number of clients. Default is %u.\n"
"    -q pqfname      Product-queue pathname. Default is\n"
"                    \"%s\".\n"
"    -o offset       The \"from\" time of data-product requests will be no earlier\n"
"                    than \"offset\" seconds ago. Default is \"max_latency\",\n"
"                    below.\n"
"    -m max_latency  The maximum acceptable data-product latency in seconds\n"
"                    Default is %d.\n"
"    -n              Do nothing other than check the configuration-file\n"
"    -t rpctimeo     Set LDM-5 RPC timeout to \"rpctimeo\" seconds. Default is %d.\n"
"    -v              Verbose logging mode: log each match (SIGUSR2 cycles)\n"
"    -x              Debug logging mode (SIGUSR2 cycles)\n"
"\n"
"conf_filename   Pathname of configuration-file. Default is\n"
"                \"%s\"\n",
            av0, av0, LDM_PORT, log_dest, maxClients, pq_path,
            DEFAULT_OLDEST, DEFAULT_RPCTIMEO, config_path);
    exit(exitStatus);
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
    log_debug("Getting TCP socket");
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0) {
        error = errno;

        log_syserr("Couldn't get socket for server");
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
        log_debug("Eliminating EADDRINUSE problem.");
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
        log_debug("Getting root privs");
        rootpriv(); // Become root
            log_debug("Binding socket");
            if (bind(sock, (struct sockaddr *) &addr, len) < 0) {
                error = errno;

                log_syserr("Couldn't obtain local address %s:%u for server",
                        inet_ntoa(addr.sin_addr), (unsigned)port);
            } /* couldn't bind to requested port */
            else {
                /*
                 * Get the local address associated with the bound socket.
                 */
                log_debug("Calling getsockname()");
                if (getsockname(sock, (struct sockaddr *) &addr, &len) < 0) {
                    error = errno;

                    log_syserr("Couldn't get local address of server's socket");
                }
                else {
                    port = (short) ntohs((short) addr.sin_port);

                    log_add("Using local address %s:%u",
                            inet_ntoa(addr.sin_addr), (unsigned) port);
                    log_flush_notice();

                    log_debug("Calling listen()");
                    if (listen(sock, 1024) != 0) {
                        error = errno;

                        log_syserr("Couldn't listen() on server's socket");
                    }
                    else {
                        *sockp = sock;
                    } /* listen() success */
                } /* getsockname() success */
            } /* "sock" is bound to local address */
        /*
         * Done with the need for privilege.
         */
        log_debug("Releasing root privs");
        unpriv(); // Become LDM user

        if (error)
            (void) close(sock);
    } /* "sock" is open */

    return error;
}

/**
 * Runs the upstream LDM server. This function always destroys the server-side
 * RPC transport.
 *
 * @param[in,out] xprt        Server-side RPC transport. Destroyed on return.
 * @param[in]     hostId      Identifier of client
 * @retval        ECONNRESET  The connection to the client LDM was lost.
 *                            `svc_destroy(xprt)` called. `log_add()` called.
 * @retval        ETIMEDOUT   The connection to the client timed-out
 * @retval        EBADF       The socket isn't open. `log_add()` called.
 */
static int
runSvc( SVCXPRT* const restrict    xprt,
        const char* const restrict hostId)
{
    log_debug("Entered");

    log_assert(xprt);
    log_assert(hostId);

    int            status;
    const unsigned TIMEOUT = 4*interval;
    const int      sock = xprt->xp_sock;

    status = one_svc_run(sock, TIMEOUT); // Exits if `done` set

    if (status == ECONNRESET){ // Connection to client lost
        /*
         * one_svc_run() called svc_getreqsock(), which called
         * svc_destroy(xprt), which must only be called once
         */
        log_clear();
        log_info("Connection with client LDM, %s, has been lost", hostId);
    }
    else {
        if (status == ETIMEDOUT)
            log_debug("Client LDM, %s, has been silent for %u seconds",
                    hostId, TIMEOUT);

        svc_destroy(xprt);
    }

    log_debug("Returning");

    return status;
}

/**
 * Executes the child LDM process.
 *
 * @param[in] raddr       Internet socket address of client
 * @param[in] xp_sock     Socket connection with client
 * @retval    0           Connection with client lost. `log_add()` called.
 * @retval    EFAULT      Couldn't register LDM versions with RPC. `log_add()`
 *                        called.
 * @retval    EPERM       Can't create server-side RPC transport. `log_add()`
 *                        called.
 * @retval    ESRCH       Client isn't allowed. `log_add()` called.
 * @retval    ETIMEDOUT   Connection to client timed-out. `log_add()` called.
 */
static int
runChildLdm(
    const struct sockaddr_in* raddr,
    const int                 xp_sock)
{
    setremote(raddr, xp_sock);

    int        status;
    peer_info* remote = get_remote();
    SVCXPRT*   xprt = svcfd_create(xp_sock, remote->sendsz, remote->recvsz);

    if (xprt == NULL) {
        log_add("Can't create fd service.");
        status = EFAULT;
    }
    else {
        xprt->xp_raddr = *raddr;
        xprt->xp_addrlen = sizeof(*raddr);

        /* Access control */
        if (lcf_isHostOk(remote)) {
            status = 0;
        }
        else {
            ensureRemoteName(raddr);

            if (lcf_isHostOk(remote)) {
                status = 0;
            }
            else {
                log_add("Denying connection from \"%s\" because not allowed",
                        remote_name());

                /*
                 * Try to tell the other guy.
                 * TODO: Why doesn't this work?
                 */
                svcerr_weakauth(xprt);

                status = ESRCH;
            } // Client's hostname is not allowed
        } // Client's IP address is not allowed

        if (status == 0) {
            /* Set the logging identifier, optional. */
            log_set_id(remote_name());
            log_info("Connection from %s", remote_name());

            // Set the client's address in the server-side RPC transport
            xprt->xp_raddr = *raddr;
            xprt->xp_addrlen = sizeof(*raddr);

            if (!svc_register(xprt, LDMPROG, 4, ldmprog_4, 0)) {
                log_add("Unable to register LDM-4 service.");
                status = EFAULT;
            }
            else {
                if (!svc_register(xprt, LDMPROG, FIVE, ldmprog_5, 0)) {
                    log_add("Unable to register LDM-5 service.");
                    status = EFAULT;
                }
                else {
                    if (!svc_register(xprt, LDMPROG, SIX, ldmprog_6, 0)) {
                        log_add("Unable to register LDM-6 service.");
                        status = EFAULT;
                    }
                    else {
                        #if WANT_MULTICAST
                            if (!svc_register(xprt, LDMPROG, SEVEN, ldmprog_7,
                                    0)) {
                                log_add("Unable to register LDM-7 service.");
                                status = EFAULT;
                            }
                        #endif
                    } // LDM6 registered
                } // LDM5 registered
            } // LDM4 registered

            if (status == 0) {
                status = runSvc(xprt, remote->printname); // Destroys `xprt`
                xprt = NULL;

                if (status == ECONNRESET)
                    status = 0;
            } // LDM versions registered with RPC
        } // Client is allowed

        if (xprt)
            // Unregisters LDM versions. Must only be called once.
            svc_destroy(xprt);
    } // Server-side RPC transport created

    return status;
}

/**
 * Handles an incoming RPC connection on a socket.  This method will fork(2)
 * a copy of this program, if appropriate, for handling incoming RPC messages.
 *
 * @param[in] sock  The socket on which to accept incoming connection
 */
static void
handle_connection(const int sock)
{
    struct sockaddr_in raddr;
    socklen_t          len;
    int                xp_sock;
    pid_t              pid;
    peer_info*         remote = get_remote();

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
        log_syserr("accept() failure: sock=%d", sock);
        return;
    }

    (void)ensure_close_on_exec(xp_sock);

    /*
     * Don't bother continuing if no more clients are allowed.
     */
    if (cps_count() >= maxClients) {
        setremote(&raddr, xp_sock);
        log_add("Denying connection from [%s] because too many clients",
                remote->astr);
        log_flush_notice();
        (void) close(xp_sock);
        return;
    }

    pid = ldmfork();
    if (pid == -1) {
        log_add("Couldn't fork process to handle incoming connection");
        log_flush_error();
        /* TODO: try again?*/
        (void) close(xp_sock);
        return;
    }

    if (pid > 0) {
        /* parent */
        /* LOG_NOTICE("child %d", pid); */
        (void) close(xp_sock);

        if (cps_add(pid)) {
            log_add_syserr("Couldn't add child PID to set");
            log_flush_error();
        }

        return;
    }
    /* else child */

    (void)close(sock);

    int status = runChildLdm(&raddr, xp_sock);

    if (status == 0) {
        log_flush(LOG_LEVEL_NOTICE);
    }
    else if (status == ESRCH || status == ETIMEDOUT) {
        log_flush(LOG_LEVEL_WARNING);
    }
    else {
        log_flush(LOG_LEVEL_ERROR);
    }

    exit(status); // `cleanup()` will release acquired resources
}

static void sock_svc(
        const int  sock)
{
    const int width = sock + 1;

    while (exitIfDone(exit_status)) {
        int            ready;
        fd_set         readfds;
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
                log_syserr("select() failure: sock=%d", sock);
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
    unpriv(); // Only become root when necessary

    int         status;
    in_addr_t   ldmIpAddr = (in_addr_t) htonl(INADDR_ANY); // Address of server interface
    unsigned    ldmPort = LDM_PORT;
    bool        becomeDaemon = true; // Default
    const char* pqfname = NULL; // Pathname of product-queue

    if (log_init(av[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }

    ensureDumpable();

    /*
     * Decode the command line, set options
     */
    {
        extern int optind;
        extern int opterr;
        extern char *optarg;
        int ch;

        opterr = 1;

        while ((ch = getopt(ac, av, "hI:vxl:nq:o:P:M:m:t:")) != EOF) {
            switch (ch) {
            case 'h': {
                usage(av[0], 0); // Calls `exit()`
                break; // Silences code scanners
            }
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
                if (log_set_destination(optarg)) {
                    log_syserr("Couldn't set logging destination to \"%s\"",
                            optarg);
                    exit(1);
                }
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
                    usage(av[0], 1);
                }
                break;
            case 'P': {
                unsigned port;
                int      nbytes;
                if (sscanf(optarg, "%5u %n", &port, &nbytes) != 1 ||
                        0 != optarg[nbytes] || port > 0xffff) {
                    (void)fprintf(stderr, "%s: invalid port number: %s\n",
                            av[0], optarg);
                    usage(av[0], 1);
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
                    usage(av[0], 1);
                }
                maxClients = max;
                break;
            }
            case 'm':
                max_latency = atoi(optarg);
                if (max_latency <= 0) {
                    (void) fprintf(stderr, "%s: invalid max_latency %s\n",
                            av[0], optarg);
                    usage(av[0], 1);
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
                    usage(av[0], 1);
                }
                break;
            case '?':
                usage(av[0], 1);
                break;
            } /* "switch" statement */
        } /* argument loop */

        if (ac - optind == 1)
            setLdmdConfigPath(av[optind]);

        if (toffset != TOFFSET_NONE && toffset > max_latency) {
            (void) fprintf(stderr,
                    "%s: invalid toffset (%d) > max_latency (%d)\n", av[0],
                    toffset, max_latency);
            usage(av[0], 1);
        }
    } /* command-line argument decoding */

    if (pqfname) {
        setQueuePath(pqfname);
    }
    else {
        pqfname = getQueuePath();
    }

#ifndef DONTFORK
    if (becomeDaemon) {
        if (reg_close()) {
            log_add("reg_close() failure");
            log_flush_fatal();
            exit(1);
        }

        if (daemonize()) { // Bwa-ha-ha!
            log_add("daemonize() failure");
            log_flush_fatal();
            exit(1);
        }

        log_clear();        // So no queued messages
        log_avoid_stderr(); // Because this process is now a daemon
    }
#endif
    /*
     * Ensure that this process is the process group leader so that all child processes (e.g.,
     * upstream LDM, downstream LDM, pqact(1)s) will receive a signal sent to the process group.
     *
     * Must be done even if a daemon.
     */
    if (getpgid(0) != getpid())
        (void)setpgid(0, 0); // Can't fail

    /*
     * Initialize the configuration file module.
     */
    log_debug("Initializing configuration-file module");
    if (lcf_init(ldmPort, getLdmdConfigPath()) != 0) {
        log_flush_fatal();
        exit(1);
    }
    if (!lcf_haveSomethingToDo()) {
        log_add("The LDM configuration-file \"%s\" is effectively empty",
                getLdmdConfigPath());
        log_flush_fatal();
        exit(1);
    }

    log_notice("Starting Up (version: %s; built: %s %s)", PACKAGE_VERSION,
            __DATE__, __TIME__);

    /*
     * register exit handler
     */
    if (atexit(cleanup) != 0) {
        log_add_syserr("atexit() failure");
        log_flush_fatal();
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
             * Create a service portal.
             */
            log_debug("Creating service portal");
            if (create_ldm_tcp_svc(&sock, ldmIpAddr, ldmPort) != ENOERR) {
                /* error reports are emitted from create_ldm_tcp_svc() */
                exit(1);
            }
            log_debug("tcp sock: %d", sock);
        }

        /*
         * Verify that the product-queue can be open for writing.
         */
        log_debug("Opening product-queue");
        if ((status = pq_open(pqfname, PQ_DEFAULT, &pq))) {
            if (PQ_CORRUPT == status) {
                log_add("The product-queue \"%s\" is inconsistent", pqfname);
                log_flush_fatal();
            }
            else {
                log_add("pq_open failed: %s: %s", pqfname, strerror(status));
                log_flush_fatal();
            }
            exit(1);
        }
        (void) pq_close(pq);
        pq = NULL;

        /*
         * Create the sharable database of upstream LDM metadata.
         *
         * TODO: Create the upstream LDM database and then close it so that this process can't be
         * affected by it. This requires that the corresponding TODO-s in "ldm_server.c" and
         * "forn_5_svc.c" are also done.
         */
        log_debug("Creating shared upstream LDM database");
        if ((status = uldb_delete(NULL))) {
            if (ULDB_EXIST == status) {
                log_clear();
            }
            else {
                log_add("Couldn't delete existing shared upstream LDM database");
                log_flush_fatal();
                exit(1);
            }
        }
        if (uldb_create(NULL, maxClients * 1024)) {
            log_add("Couldn't create shared upstream LDM database");
            log_flush_fatal();
            exit(1);
        }

        /*
         * Execute appropriate entries in the configuration file.
         */
        log_debug("Executing configuration-file");
        if (lcf_execute() != 0) {
            log_flush_fatal();
            exit(1);
        }

        if (lcf_isServerNeeded()) {
            /*
             * Serve
             */
            log_debug("Serving socket");
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

    return exit_status;
}
