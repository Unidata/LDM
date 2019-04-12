/**
 * Ingests a NOAAPORT data stream into an LDM product-queue.
 *
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_ingest.c
 * @author: Steven R. Emmerson
 */

#include "config.h"

#include "globals.h"
#include "log.h"
#include "nbs_application.h"
#include "nbs_link.h"
#include "nbs_stack.h"
#include "noaaport_socket.h"
#include "pq.h"
#include "Thread.h"

#include <errno.h>
#include <inttypes.h>
#include <libgen.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <timestamp.h>
#include <unistd.h>

/**
 * Values for signaling the statistics thread.
 */
typedef enum {
    STATS_THREAD_WAIT = 0,
    STATS_THREAD_PRINT,
    STATS_THREAD_TERMINATE
} stats_thread_flag_t;

static char*               progname;         ///< Name of program
static nbsa_t*             nbsa;             ///< NBS application-layer
static nbsl_t*             nbsl;             ///< NBS link-layer
static nbss_t*             nbss;             ///< NBS protocol stack
static pthread_mutex_t     mutex;            ///< For statistics thread
static pthread_cond_t      cond =
        PTHREAD_COND_INITIALIZER;            ///< For statistics thread
static stats_thread_flag_t stats_thread_flag;///< Statistics thread flag
static pthread_t           stats_thread;     ///< Statistics thread
static const char*         mcast_ip_addr;    ///< NBS multicast IP address
static const char*         iface_ip_addr;    ///< NBS interface
static int                 sock = -1;        ///< NBS socket

/**
 * Decodes this program's command-line.
 *
 * @param[in] ac  Number of arguments
 * @param[in] av  Arguments
 * @retval true   Success
 * @retval false  Failure. log_add() called.
 */
static bool decode_command_line(
        int   argc,
        char* argv[])
{
    extern int   optind;
    extern int   opterr;
    extern char* optarg;
    int          ch;
    bool         success = true;
    const char*  pqfname = getQueuePath();

    // Error messages are being explicitly handled
    opterr = 0;

    while ((ch = getopt(argc, argv, ":f:l:q:vx")) != -1) {
        switch (ch) {
        case 'I':
            iface_ip_addr = optarg;
            break;
        case 'l':
            (void)log_set_destination(optarg);
            break;
        case 'q':
            pqfname = optarg;
            break;
        case 'v':
            if (!log_is_enabled_info)
                (void)log_set_level(LOG_LEVEL_INFO);
            break;
        case 'x':
            (void)log_set_level(LOG_LEVEL_DEBUG);
            break;
        case ':': {
            log_add("Option \"-%c\" requires an argument", optopt);
            success = false;
            break;
        }
        default:
            log_add("Unknown option: \"%c\"", optopt);
            success = false;
        }
    }

    if (success) {
        if (optind == argc) {
            log_add("Multicast group IP address not specified");
            success = false;
        }
        else {
            mcast_ip_addr = argv[optind++];
            if (optind < argc) {
                log_add("Extraneous operand \"%s\"", argv[optind]);
                success = false;
            }
            else {
                setQueuePath(pqfname);
            }
        }
    }

    return success;
}

/**
 * Prints a usage message.
 */
static void print_usage(void)
{
    log_level_t level = log_get_level();
    (void)log_set_level(LOG_LEVEL_INFO);
    log_info_q(
"Usage: %s [options] mcast_ip_addr\n"
"Options:\n"
"    -l dest        Log to <dest>. One of: \"\" (system logging daemon),\n"
"                   \"-\" (standard error), or file <dest>. Default is\n"
"                   \"%s\".\n"
"    -I iface       Receive NBS packets on interface whose IP address is\n"
"                   <iface>. Default is all interfaces.\n"
"    -q queue       Use <queue> as product-queue. Default is\n"
"                   \"%s\".\n"
"    -v             Verbose logging level: log each product.\n"
"    -x             Debug logging level.\n"
"Operands:\n"
"    mcast_ip_addr  IP address of NBS multicast group",
            progname, log_get_default_destination(), getQueuePath());
    (void)log_set_level(level);
}

/**
 * Prints input statistics.
 *
 * @pre `*nbsl` is valid.
 */
static void print_stats(void)
{
    log_level_t level = log_get_level();
    (void)log_set_level(LOG_LEVEL_INFO);
    nbsl_log_stats(nbsl, LOG_LEVEL_INFO);
    (void)log_set_level(level);
}

/**
 * Start function for the statistics thread. Waits on condition variable `cond`.
 * Action taken depends on value of `stats_thread_flag`.
 *
 * @param[in] arg  Ignored
 */
static void* stats_thread_start(
        void* arg)
{
    /*
     * The only task on this thread is listening to the condition variable,
     * examining `stats_thread_flag`, and taking the signaled action.
     */
    sigset_t mask;
    sigfillset(&mask);
    int status = pthread_sigmask(SIG_BLOCK, &mask, NULL);
    log_assert(status == 0);

    status = mutex_lock(&mutex);
    log_assert(status == 0);
    for (;;) {
        while (stats_thread_flag == STATS_THREAD_WAIT) {
            status = pthread_cond_wait(&cond, &mutex);
            log_assert(status == 0);
        }
        if (stats_thread_flag == STATS_THREAD_PRINT) {
            stats_thread_flag = STATS_THREAD_WAIT;
            print_stats();
            continue;
        }
        break;
    }
    status = mutex_unlock(&mutex);
    log_assert(status == 0);

    log_free();
    return NULL;
}

/**
 * Signals the statistics thread.
 *
 * This function is async-signal-safe.
 *
 * @param[in] value  Signal value. One of
 *                     - STATS_THREAD_WAIT
 *                     - STATS_THREAD_PRINT
 *                     - STATS_THREAD_TERMINATE
 */
static void signal_stats_thread(
        const stats_thread_flag_t value)
{
    int status = mutex_lock(&mutex);
    log_assert(status == 0);
    stats_thread_flag = value;
    (void)pthread_cond_signal(&cond);
    status = mutex_unlock(&mutex);
    log_assert(status == 0);
}

/**
 * Handles signals.
 *
 * @param sig  Signal to handle
 */
static void signal_handler(
        const int sig)
{
    switch (sig) {
        case SIGUSR1:
            log_refresh();
            signal_stats_thread(STATS_THREAD_PRINT);
            break;
        case SIGINT:
            // FALLTHROUGH
        case SIGTERM:
            signal_stats_thread(STATS_THREAD_TERMINATE);
            (void)close(sock); // Closes input
            break;
    }
}

/**
 * Installs the signal handler for this program.
 */
static void install_signal_handlers(void)
{
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_RESTART;
    sigact.sa_handler = signal_handler;

    (void)sigaction(SIGINT, &sigact, NULL);
    (void)sigaction(SIGTERM, &sigact, NULL);
    (void)sigaction(SIGUSR1, &sigact, NULL);

    sigset_t sigset;
    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGINT);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

/**
 * Opens the LDM product-queue for writing.
 *
 * @param[out] pq        LDM product-queue
 * @param[in]  pathname  Pathname of the product-queue
 * @return
 */
static int open_pq(
        pqueue** const restrict    pq,
        const char* const restrict pathname)
{
    int status = pq_open(pathname, 0, pq);
    if (status == EACCES) {
        log_add_errno(status, NULL);
    }
    else if (status) {
        log_add("Product-queue \"%s\" is corrupt or doesn't exist", pathname);
    }
    return status;
}

/**
 * Initializes an NBS protocol-stack for receiving products.
 *
 * @param[in] pq             LDM product-queue
 * @param[in] fd             File descriptor from which to receive NBS packets
 * @retval 0                 Success
 * @retval NBS_STATUS_NOMEM  Out of memory. log_add() called.
 */
static int init_receiving_nbs_stack(
        pqueue* const restrict pq,
        const int              fd)
{
    log_assert(pq && fd >= 0);
    int status = nbsa_new(&nbsa);
    if (status) {
        log_add("Couldn't create NBS application-layer");
    }
    else {
        status = nbsa_set_pq(nbsa, pq);
        if (status) {
            log_add("Couldn't set product-queue in NBS application-layer");
        }
        else {
            status = nbsl_new(&nbsl);
            if (status) {
                log_add("Couldn't create NBS link-layer");
            }
            else {
                status = nbsl_set_recv_file_descriptor(nbsl, fd);
                if (status) {
                    log_add("Couldn't set input file descriptor in NBS "
                            "link-layer");
                }
                else {
                    status = nbss_recv_new(&nbss, nbsa, nbsl);
                    if (status)
                        log_add("Couldn't create receiving NBS protocol stack");
                }
                if (status)
                    nbsl_free(nbsl);
            } // `nbsl` created
        }
        if (status)
            nbsa_free(nbsa);
    } // `nbsa` created
    return status;
}

/**
 * Finalizes an NBS protocol-stack.
 */
static void fini_receiving_nbs_stack(void)
{
    nbss_free(nbss); nbss = NULL;
    nbsl_free(nbsl); nbsl = NULL;
    nbsa_free(nbsa); nbsa = NULL;
}

/**
 * Initializes this program:
 *
 * @retval true   Success
 * @retval false  Failure. log_add() called.
 */
static bool init(void)
{
    int status = mutex_init(&mutex, PTHREAD_MUTEX_ERRORCHECK, true);
    if (status) {
        log_errno(status, "Couldn't initialize mutex");
    }
    else {
        status = open_pq(&pq, getQueuePath());
        if (status) {
            log_add("Couldn't open product-queue");
        }
        else {
            status = nportSock_init(&sock, mcast_ip_addr, iface_ip_addr);
            if (status) {
                log_add("Couldn't create socket for NBS reception");
            }
            else {
                status = init_receiving_nbs_stack(pq, sock);
                if (status) {
                    log_add("Couldn't initialize receiving NBS protocol stack");
                }
                else {
                    stats_thread_flag = STATS_THREAD_WAIT;
                    install_signal_handlers();
                }
                if (status)
                    (void)close(sock);
            } // `sock` open
            if (status)
                (void)pq_close(pq);
        } // `pq` open
        if (status)
            (void)mutex_destroy(&mutex);
    } // `mutex` initialized

    return status == 0;
}

/**
 * Finalizes this program.
 */
static void fini(void)
{
    fini_receiving_nbs_stack();
    (void)close(sock);  sock = -1;
    (void)pq_close(pq); pq = NULL;
    (void)mutex_destroy(&mutex);
    (void)pthread_cond_destroy(&cond);
}

/**
 * Executes this program. Doesn't return until the input is shut down or an
 * unrecoverable error occurs.
 *
 * @retval true   Success
 * @retval false  Failure. log_add() called.
 */
static bool execute()
{
    int status = pthread_create(&stats_thread, NULL, stats_thread_start, NULL);
    if (status) {
        log_add_errno(status, "Couldn't start statistics thread");
    }
    else {
        status = nbss_receive(nbss);
        // Harmless if already terminated:
        signal_stats_thread(STATS_THREAD_TERMINATE);
        (void)pthread_join(stats_thread, NULL);
    }
    return status == 0;
}

/**
 * Reads a NOAAPORT data stream, creates LDM data-products from the stream, and
 * inserts the data-products into an LDM product-queue.
 *
 * Usage:
 *     nbs_ingest [-l <em>log</em>] [-v|-x] [-q <em>queue</em>] [-I <em>iface</em>] <em>mcast_ip_addr</em>
 *
 * Where:
 * <dl>
 *      <dt>-I <em>iface</em></dt>
 *      <dd>Listen for multicast packets on the interface whose IP address is
 *      <em>iface</em>. Default is to listen on all interfaces.</dd>
 *
 *      <dt>-l <em>log</em></dt>
 *      <dd>Log to file <em>log</em>. The default is to use the standard LDM log file
 *      if the current process is a daemon; otherwise, the standard error
 *      stream is used.</dd>
 *
 *      <dt>-q <em>queue</em></dt>
 *      <dd>Use <em>queue</em> as the pathname of the LDM product-queue. The default
 *      is the default LDM product-queue.</dd>
 *
 *      <dt>-v</dt>
 *      <dd>Log messages of level INFO and higher priority.</dd>
 *
 *      <dt>-x</dt>
 *      <dd>Log messages of level DEBUG and higher priority.</dd>
 *
 *      <dt><em>mcast_ip_addr</em></dt>
 *      <dd>Receive NBS packets from IP multicast group <em>mcast_ip_addr</em>.</dd>
 * </dl>
 *
 * If neither `-v`, nor `-x` is specified, then logging will be restricted to
 * levels ERROR, WARN, and NOTE.
 *
 * A `SIGUSR1` causes this program to refresh logging (if configure(1) was
 * executed without the "--with-ulog" option) and log input statistics.
 *
 * @retval 0 if successful.
 * @retval 1 if an error occurred. At least one error-message will be logged.
 */
int main(
        int   ac,
        char* av[])
{
    int status;

    // Done first in case something happens that needs to be reported.
    progname = basename(av[0]);
    if (log_init(av[0])) {
        log_syserr("Couldn't initialize logging module");
        status = EXIT_FAILURE;
    }
    else {
        log_notice_q("Starting up");

        status = EXIT_FAILURE;
        if (!decode_command_line(ac, av)) {
            log_error_q("Couldn't decode command-line");
            print_usage();
        }
        else if (!init()) {
            log_error_q("Couldn't initialize program");
        }
        else {
            if (!execute()) {
                log_error_q("Couldn't execute program");
            }
            else {
                print_stats();
                status = EXIT_SUCCESS;
            }
            fini();
        }

        log_notice_q("Exiting");
        log_fini();
    }
    return status;
}
