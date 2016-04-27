/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: nbs_ingest.c
 * @author: Steven R. Emmerson
 *
 * This program reads a NOAAPORT data stream, creates LDM data-products from the
 * stream, and inserts the data-products into an LDM product-queue.
 */

#include "config.h"

#include "globals.h"
#include "log.h"
#include "mutex.h"
#include "nbs_application.h"
#include "nbs_link.h"
#include "nbs_stack.h"
#include "noaaport_socket.h"
#include "pq.h"

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

    // Error messages are being explicitly handled
    opterr = 0;

    while ((ch = getopt(argc, argv, ":f:l:q:vx:")) != -1) {
        switch (ch) {
        case 'I':
            iface_ip_addr = optarg;
            break;
        case 'l':
            (void)log_set_destination(optarg);
            break;
        case 'q':
            setQueuePath(optarg);
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
    log_info(
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
 */
static void print_stats(void)
{
    nbsl_stats_t stats;
    char         msg[512];

    nbsl_get_stats(nbsl, &stats);
    if (stats.total_frames == 0) {
        // Format no-observation statistics
        snprintf(msg, sizeof(msg),
                "Input Statistics:\n"
                "    Times:\n"
                "        First I/O: N/A\n"
                "        Last I/O:  N/A\n"
                "        Duration:  N/A\n"
                "    Frames:\n"
                "        Count:     0\n"
                "        Sizes in Bytes:\n"
                "            Smallest: N/A\n"
                "            Mean:     N/A\n"
                "            Largest:  N/A\n"
                "            S.D.:     N/A\n"
                "        Rate:      N/A\n"
                "    Bytes:\n"
                "        Count:     0\n"
                "        Rate:      N/A");
        msg[sizeof(msg)-1] = 0;
    }
    else {
        struct timeval first;
        (void)timeval_init_from_timespec(&first, &stats.first_io);
        struct timeval duration;
        char           first_string[TIMEVAL_FORMAT_TIME];
        timeval_format_time(first_string, &first);
        char           duration_string[TIMEVAL_FORMAT_DURATION];

        if (stats.total_frames == 1) {
            // Format single-observation statistics
            (void)timeval_init_from_difference(&duration, &first, &first);
            snprintf(msg, sizeof(msg),
                    "Input Statistics:\n"
                    "    Times:\n"
                    "        First I/O: %s\n"
                    "        Last I/O:  %s\n"
                    "        Duration:  %s\n"
                    "    Frames:\n"
                    "        Count:     1\n"
                    "        Sizes in Bytes:\n"
                    "            Smallest: %5u\n"
                    "            Mean:     %7.1f\n"
                    "            Largest:  %5u\n"
                    "            S.D.:     N/A\n"
                    "        Rate:      N/A\n"
                    "    Bytes:\n"
                    "        Count:     %"PRIuLEAST64"\n"
                    "        Rate:      N/A",
                    first_string,
                    first_string,
                    timeval_format_duration(duration_string, &duration),
                    stats.smallest_frame,
                    (double)stats.smallest_frame,
                    stats.smallest_frame,
                    stats.total_bytes);
            msg[sizeof(msg)-1] = 0;
        }
        else {
            // Format multiple-observation statistics
            struct timeval last;
            (void)timeval_init_from_timespec(&last, &stats.last_io);
            (void)timeval_init_from_difference(&duration, &last, &first);
            char           last_string[TIMEVAL_FORMAT_TIME];
            double         mean_frame_size = (double)stats.total_bytes /
                    stats.total_frames;
            double         variance_frame_size = (stats.sum_sqr_dev -
                    (stats.sum_dev*stats.sum_dev)/stats.total_frames) /
                            (stats.total_frames-1);
            double         stddev_frame_size = sqrt(variance_frame_size);
            double         stddev_mean_frame_size = sqrt(variance_frame_size /
                    stats.total_frames);
            double         seconds_duration = timeval_as_seconds(&duration);
            snprintf(msg, sizeof(msg),
                    "Input Statistics:\n"
                    "    Times:\n"
                    "        First I/O: %s\n"
                    "        Last I/O:  %s\n"
                    "        Duration:  %s\n"
                    "    Frames:\n"
                    "        Count:     %"PRIuLEAST64"\n"
                    "        Sizes in Bytes:\n"
                    "            Smallest: %5u\n"
                    "            Mean:     %7.1f(%.1f)\n"
                    "            Largest:  %5u\n"
                    "            S.D.:     %7.1f\n"
                    "        Rate:      %g/s\n"
                    "    Bytes:\n"
                    "        Count:     %"PRIuLEAST64"\n"
                    "        Rate:      %g/s",
                    first_string,
                    timeval_format_time(last_string, &last),
                    timeval_format_duration(duration_string, &duration),
                    stats.total_frames,
                    stats.smallest_frame,
                    mean_frame_size,
                    stddev_mean_frame_size,
                    stats.largest_frame,
                    stddev_frame_size,
                    stats.total_frames / seconds_duration,
                    stats.total_bytes,
                    stats.total_bytes / seconds_duration);
            msg[sizeof(msg)-1] = 0;
        }
    }

    log_level_t level = log_get_level();
    (void)log_set_level(LOG_LEVEL_INFO);
    log_info(msg);
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
 * Signals the statistics thread to print statistics.
 *
 * @param[in] sig  `SIGUSR1`. Ignored.
 */
static void signal_print_stats(
        const int sig)
{
    log_assert(sig == SIGUSR1);
    signal_stats_thread(STATS_THREAD_PRINT);
}

/**
 * Signals the program to terminate cleanly.
 *
 * @param[in] sig  `SIGTERM` or `SIGINT`. Ignored.
 */
static void signal_terminate(
        const int sig)
{
    log_assert(sig == SIGTERM || sig == SIGINT);
    signal_stats_thread(STATS_THREAD_TERMINATE);
    (void)close(sock); // Closes input
}

/**
 * Installs the signal handlers for this program.
 */
static void install_signal_handlers(void)
{
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_RESTART;

    sigact.sa_handler = signal_print_stats;
    int status = sigaction(SIGUSR1, &sigact, NULL);
    log_assert(status == 0);

    sigact.sa_handler = signal_terminate;
    status = sigaction(SIGTERM, &sigact, NULL);
    log_assert(status == 0);
    status = sigaction(SIGINT, &sigact, NULL);
    log_assert(status == 0);
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
    int status = mutex_init(&mutex, false, true);
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
            (void)mutex_fini(&mutex);
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
    (void)mutex_fini(&mutex);
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
 * A `SIGUSR1` causes this program to log input statistics.
 *
 * @retval 0 if successful.
 * @retval 1 if an error occurred. At least one error-message will be logged.
 */
int main(
        int   ac,
        char* av[])
{
    // Done first in case something happens that needs to be reported.
    progname = basename(av[0]);
    (void)log_init(progname);
    log_notice("Starting up");

    int status = EXIT_FAILURE;
    if (!decode_command_line(ac, av)) {
        log_error("Couldn't decode command-line");
        print_usage();
    }
    else if (!init()) {
        log_error("Couldn't initialize program");
    }
    else {
        if (!execute()) {
            log_error("Couldn't execute program");
        }
        else {
            print_stats();
            status = EXIT_SUCCESS;
        }
        fini();
    }

    log_notice("Exiting");
    log_fini();
    return status;
}
