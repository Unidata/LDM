/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: log2ulog.c
 * @author: Steven R. Emmerson
 *
 * This file provides a simple implementation of the `log.h` API.
 */

#include "config.h"

#include "Thread.h"
#include "ldmfork.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#ifndef _XOPEN_PATH_MAX
    #define _XOPEN_PATH_MAX 1024
#endif

#ifndef MAX
#define MAX(a,b) ((a) >= (b) ? (a) : (b))
#endif

/******************************************************************************
 * Private API:
 ******************************************************************************/

/**
 * Destination object for log messages.
 */
typedef struct dest {
    void (*log)(
        struct dest*              dest,
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg);
    void (*flush)(struct dest*);
    int  (*get_fd)(const struct dest*);
    void (*fini)(struct dest*);
    int  (*lock)(struct dest*);
    int  (*unlock)(struct dest*);
    FILE*  stream; // Unused for system logging
} dest_t;
/**
 * The identifier for log messages
 */
static char          ident[_XOPEN_PATH_MAX];
/**
 * System logging daemon options
 */
static int           syslog_options = LOG_PID | LOG_NDELAY;
/**
 * System logging facility
 */
static int           syslog_facility = LOG_LDM;
/**
 * The destination of log messages:
 */
static dest_t        dest;

static void blockSigs(sigset_t* const prevSigs)
{
    sigset_t sigs;

    sigfillset(&sigs);

    (void)pthread_sigmask(SIG_BLOCK, &sigs, prevSigs);
}

static void unblockSigs(sigset_t* const prevSigs)
{
    (void)pthread_sigmask(SIG_SETMASK, prevSigs, NULL);
}

/**
 * Returns the pathname of the LDM log file.
 *
 * @return The pathname of the LDM log file
 */
static const char* get_ldm_logfile_pathname(void)
{
    static char pathname[_XOPEN_PATH_MAX];
    if (pathname[0] == 0) {
        int   reg_getString(const char* key, char** value);
        char* value;
        if (reg_getString("/log/file", &value)) {
            // No entry in registry
            (void)snprintf(pathname, sizeof(pathname), "%s/ldmd.log",
                    LDM_LOG_DIR);
            pathname[sizeof(pathname)-1] = 0;
            logl_internal(LOG_LEVEL_WARNING,
                    "Couldn't get pathname of LDM log file from registry. "
                    "Using default: \"%s\".", pathname);
        }
        else {
            (void)strncpy(pathname, value, sizeof(pathname));
            pathname[sizeof(pathname)-1] = 0;
            free(value);
        }
    }
    return pathname;
}

/**
 * Returns the string associated with a logging level.
 *
 * @param[in] level      The logging level
 * @retval    "UNKNOWN"  `level` is invalid
 * @return               The string associated with `level`
 */
static const char* level_to_string(
        const log_level_t level)
{
    static const char* strings[] = {"DEBUG", "INFO", "NOTE", "WARN",
            "ERROR", "ALERT", "CRIT", "EMERG"};
    return logl_vet_level(level) ? strings[level] : "UNKNOWN";
}

/******************************************************************************
 * Logging destination is the system logging daemon:
 ******************************************************************************/

/**
 * Writes a single log message to the system logging daemon.
 *
 * @param[in,out] dest  Destination object
 * @param[in]     level Logging level
 * @param[in]     loc   Message location
 * @param[in]     msg   Message
 */
static void syslog_log(
        dest_t* const             dest,
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    syslog(logl_level_to_priority(level), "%s:%d:%s() %s",
            logl_basename(loc->file), loc->line, loc->func, msg);
}

/**
 * Flushes logging to the system logging daemon.
 *
 * @param[in,out] dest  Destination object
 */
static void syslog_flush(
        dest_t* const dest)
{
    // Does nothing
}

/**
 * Returns the file descriptor that will be used for logging.
 *
 * @param[in,out] dest  Destination object
 * @retval -1           No file descriptor will be used
 * @return              The file descriptor that will be used for logging
 */
static int syslog_get_fd(
        const dest_t* const dest)
{
    return -1;
}

/**
 * Finalizes access to the system logging daemon.
 *
 * @param[in,out] dest  Destination object
 */
static void syslog_fini(
        dest_t* const             dest)
{
    closelog();
}

/**
 * Initializes access to the system logging daemon.
 *
 * @retval 0  Success (always)
 */
static int syslog_init(
        dest_t* const dest)
{
    openlog(ident, syslog_options, syslog_facility);
    dest->log = syslog_log;
    dest->flush = syslog_flush;
    dest->get_fd = syslog_get_fd;
    dest->fini = syslog_fini;
    return 0;
}

/******************************************************************************
 * Logging destination is an output stream (regular file or standard error
 * stream):
 ******************************************************************************/

/**
 * Returns the file descriptor of the logging stream.
 *
 * @param[in,out] dest  Destination object
 * @retval -1           No knowable file descriptor will be used (e.g., syslog)
 * @return              The file descriptor that will be used for logging
 */
static inline int stream_get_fd(
        const dest_t* const dest)
{
    return fileno(dest->stream);
}

/**
 * Writes a single log message to a stream.
 *
 * @param[in,out] dest   Destination object
 * @param[in]     level  Logging level.
 * @param[in]     loc    Location where the message was created.
 * @param[in]     msg    Message.
 */
static void stream_log(
        dest_t* const             dest,
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    struct timespec now;
    (void)clock_gettime(CLOCK_REALTIME, &now);

    struct tm tm;
    (void)gmtime_r(&now.tv_sec, &tm);

    const int         year = tm.tm_year + 1900;
    const int         month = tm.tm_mon + 1;
    const long        microseconds = now.tv_nsec/1000;
    const int         pid = getpid();
    const char* const basename = logl_basename(loc->file);
    const char* const levelId = level_to_string(level);

    (void)dest->lock(dest);
        sigset_t prevSigs;
        blockSigs(&prevSigs);
            while (*msg) {
                const char* const newline = strchr(msg, '\n');
                const size_t      msglen =
                        newline ? newline - msg : strlen(msg);

                // Timestamp
                (void)fprintf(dest->stream,
                        "%04d%02d%02dT%02d%02d%02d.%06ldZ ",
                        year, month, tm.tm_mday, tm.tm_hour,
                        tm.tm_min, tm.tm_sec, microseconds);

                // Process
                (void)fprintf(dest->stream, "%s[%d] ", ident, pid);

                #define LEVEL_OFFSET 57
                #define MIN0(x)    ((x) >= 0 ? (x) : 0)

                // Error level
                (void)fprintf(dest->stream, "%-5s ", levelId);

                // File
                int nbytes = fprintf(dest->stream, "%s:", basename);

                // Function
                (void)fprintf(dest->stream, "%*s() ", MIN0(32-nbytes),
                        loc->func);

                // Message
                (void)fprintf(dest->stream, "%s\n", msg);

                if (newline) {
                    msg = newline + 1;
                    continue;
                }
                break;
            } // Output-line loop

            dest->flush(dest);
        unblockSigs(&prevSigs);
    (void)dest->unlock(dest);
}

/**
 * Flushes the logging stream.
 *
 * @param[in,out] dest  Destination object
 */
static inline void stream_flush(
        dest_t* const             dest)
{
    (void)fflush(dest->stream);
}

/******************************************************************************
 * Logging is to the standard error stream:
 ******************************************************************************/

static int stderr_lock(dest_t* const dest)
{
    flockfile(dest->stream);
    return 0;
}

static int stderr_unlock(dest_t* const dest)
{
    funlockfile(dest->stream);
    return 0;
}

/**
 * Initializes access to the standard error stream.
 *
 * @param[in,out] dest  Destination object
 * @retval 0            Success (always)
 */
static int stderr_init(
        dest_t* const dest)
{
    dest->log = stream_log;
    dest->flush = stream_flush;
    dest->get_fd = stream_get_fd;
    dest->fini = stream_flush;
    dest->lock = stderr_lock;
    dest->unlock = stderr_unlock;
    dest->stream = stderr;
    return 0;
}

/******************************************************************************
 * Logging is to a regular file:
 ******************************************************************************/

/**
 * Locks a log file against access by another process.
 *
 * @param[in] dest     Logging destination
 * @retval    0        Success
 * @retval    EINTR    Interrupted by signal
 * @retval    ENOLCK   Creating the locked region would exceed system limit
 * @retval    EDEADLK  Deadlock detected
 */
static int file_lock(dest_t* const dest)
{
    int          status;
    struct flock lock;

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    // `l_start = l_len = 0` => entire file
    lock.l_start = 0;
    lock.l_len = 0;

    if (fcntl(stream_get_fd(dest), F_SETLKW, &lock) == -1) {
        log_add_syserr("Couldn't lock log file");
        status = errno;
    }
    else {
        status = 0;
    }

    return status;
}

/**
 * Unlocks a log file for access by another process.
 *
 * @param[in] dest     Logging destination
 * @retval    0        Success
 * @retval    EINTR    Interrupted by signal
 */
static int file_unlock(dest_t* const dest)
{
    int          status;
    struct flock lock;

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    // `l_start = l_len = 0` => entire file
    lock.l_start = 0;
    lock.l_len = 0;

    if (fcntl(stream_get_fd(dest), F_SETLK, &lock) == -1) {
        log_add_syserr("Couldn't unlock log file");
        status = errno;
    }
    else {
        status = 0;
    }

    return status;
}

/**
 * Finalizes access to the log file.
 *
 * @param[in,out] dest  Destination object
 */
static void file_fini(dest_t* const dest)
{
    if (dest->stream != NULL) {
        (void)fclose(dest->stream); // Will flush
        dest->stream = NULL;
    }
}

/**
 * Initializes access to a log file.
 *
 * @param[in,out] dest  Destination object
 * @retval  0           Success
 * @retval -1           Failure
 */
static int file_init(
        dest_t* const dest)
{
    int status;
    int fd = open(log_dest, O_WRONLY|O_APPEND|O_CREAT,
            S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
    if (fd < 0) {
        log_add_syserr("Couldn't open log file \"%s\"", log_dest);
        status = -1;
    }
    else {
        status = ensure_close_on_exec(fd);
        if (status) {
            log_add("Couldn't ensure log file \"%s\" is close-on-exec",
                    log_dest);
        }
        else {
            dest->stream = fdopen(fd, "a");
            if (dest->stream == NULL) {
                log_add_syserr("Couldn't open stream on log file \"%s\"",
                        log_dest);
                status = -1;
            }
            else {
                setbuf(dest->stream, NULL); // No buffering
                dest->log = stream_log;
                dest->flush = stream_flush;
                dest->get_fd = stream_get_fd;
                dest->fini = file_fini;
                dest->lock = file_lock;
                dest->unlock = file_unlock;
            }
        }
        if (status)
            close(fd);
    }
    return status;
}

/**
 * Sets the logging destination object.
 *
 * @pre                 Module is locked
 * @retval  0           Success
 * @retval -1           Failure. Logging destination is unchanged. log_add()
 *                      called.
 */
static int dest_set(void)
{
    dest_t new_dest;
    int    status =
            LOG_IS_SYSLOG_SPEC(log_dest)
                ? syslog_init(&new_dest)
                : LOG_IS_STDERR_SPEC(log_dest)
                  ? stderr_init(&new_dest)
                  : file_init(&new_dest);
    if (status) {
        log_add("Couldn't set logging destination");
    }
    else {
        dest.fini(&dest);
        dest = new_dest;
    }
    return status;
}

/******************************************************************************
 * Package-Private Implementation API:
 ******************************************************************************/

/**
 * Sets the logging destination.
 *
 * @pre                Module is locked
 * @retval  0          Success
 * @retval -1          Failure. Logging destination is unchanged. log_add()
 *                     called.
 */
int logi_set_destination(void)
{
    return dest_set();
}

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] level  Logging level.
 * @param[in] loc    Location where the message was generated.
 * @param[in] ...    Message arguments -- starting with the format.
 */
void logi_internal(
        const log_level_t      level,
        const log_loc_t* const loc,
                               ...)
{
    va_list args;
    va_start(args, loc);
    const char* fmt = va_arg(args, const char*);
    char        buf[_POSIX2_LINE_MAX];
    int nbytes = vsnprintf(buf, sizeof(buf), fmt, args);
    if (nbytes < 0) {
        LOG_LOC_DECL(location);
        dest.log(&dest, LOG_LEVEL_ERROR, &location, "vsnprintf() failure");
    }
    else {
        buf[sizeof(buf)-1] = 0;
        dest.log(&dest, level, loc, buf);
    }
    va_end(args);
}

/**
 * Initializes the logging module. Should be called before any other function.
 * `log_dest` must be set.
 * - log_get_id()           will return the filename component of `id`
 * - log_get_facility()     will return `LOG_LDM`
 * - log_get_level()        will return `LOG_LEVEL_NOTICE`
 * - log_get_options()      will return `LOG_PID | LOG_NDELAY`
 * - log_get_destination()  will return
 *                            - The pathname of the LDM log file if
 *                              log_avoid_stderr() has been called
 *                            - "-" otherwise
 *
 * @param[in] id       The pathname of the program (e.g., `argv[0]`). Caller may
 *                     free.
 * @retval    0        Success.
 * @retval    -1       Error. Logging module is in an unspecified state.
 */
int logi_init(
        const char* const id)
{
    int status;
    if (id == NULL) {
        status = -1;
    }
    else {
        (void)stderr_init(&dest);
        syslog_options = LOG_PID | LOG_NDELAY;
        syslog_facility = LOG_LDM;

        // Handle potential overlap because log_get_id() returns `ident`
        size_t nbytes = strlen(id);
        if (nbytes > sizeof(ident) - 1)
            nbytes = sizeof(ident) - 1;
        (void)memmove(ident, logl_basename(id), nbytes);
        ident[nbytes] = 0;

        status = logi_set_destination();
        if (status)
            logl_internal(LOG_LEVEL_ERROR, "Couldn't set logging destination");
    }
    return status;
}

/**
 * Re-initializes the logging module based on its state just prior to calling
 * logi_fini(). If logi_fini(), wasn't called, then the result is unspecified.
 *
 * @retval    0        Success.
 */
int logi_reinit(void)
{
    return dest_set();
}

/**
 * Enables logging down to the level given by `log::log_level`. Should be called
 * after logi_init().
 */
void logi_set_level(void)
{
}

/**
 * Sets the logging identifier. Should be called after `logi_init()`.
 *
 * @pre                 `id` isn't NULL
 * @param[in] id        The new identifier. Caller may free.
 * @retval    0         Success.
 * @retval    -1        Failure.
 */
int logi_set_id(
        const char* const id)
{
    // Handle potential overlap because log_get_id() returns `ident`
    size_t nbytes = strlen(id);
    if (nbytes > sizeof(ident) - 1)
        nbytes = sizeof(ident) - 1;
    (void)memmove(ident, id, nbytes);
    ident[nbytes] = 0;
    /*
     * The destination is re-initialized in case it's the system logging
     * daemon.
     */
    return dest_set();
}

/**
 * Finalizes the logging module.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int logi_fini(void)
{
    dest.fini(&dest);
    return 0;
}

/**
 * Emits a single log message.
 *
 * @param[in] level  Logging level.
 * @param[in] loc    The location where the message was generated.
 * @param[in] string The message.
 */
void logi_log(
        const log_level_t               level,
        const log_loc_t* const restrict loc,
        const char* const restrict      string)
{
    dest.log(&dest, level, loc, string);
}

/**
 * Flushes logging.
 */
void logi_flush(void)
{
    dest.flush(&dest);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Returns the default destination for log messages if the process is a daemon.
 *
 * @retval ""   The system logging daemon
 * @return      The pathname of the standard LDM log file
 */
const char* log_get_default_daemon_destination(void)
{
    /*
     * Locking is unnecessary because the pathname of the LDM log file is
     * immutable
     */
    return get_ldm_logfile_pathname();
}

/**
 * Sets the facility that will be used (e.g., `LOG_LOCAL0`) when logging to the
 * system logging daemon. Should be called after `log_init()`.
 *
 * @param[in] facility  The facility that will be used when logging to the
 *                      system logging daemon.
 * @retval    0         Success
 * @retval    -1        Failure
 */
int log_set_facility(
        const int facility)
{
    int  status;
    int  diff_local0 = facility - LOG_LOCAL0;
    int  diff_local7 = facility - LOG_LOCAL7;
    if (diff_local0 * diff_local7 > 0 && facility != LOG_USER) {
        logl_internal(LOG_LEVEL_ERROR, "Invalid system logging facility: %d",
                facility);
        status = -1; // `facility` is invalid
    }
    else {
        logl_lock();
            syslog_facility = facility;
            /*
             * The destination is re-initialized in case it's the system logging
             * daemon.
             */
            status = dest_set();
        logl_unlock();
    }
    return status;
}

/**
 * Returns the facility that will be used when logging to the system logging
 * daemon (e.g., `LOG_LOCAL0`). Should be called after log_init().
 *
 * @return The facility that will be used when logging to the system logging
 *         daemon (e.g., `LOG_LOCAL0`).
 */
int log_get_facility(void)
{
    logl_lock();
        int facility = syslog_facility;
    logl_unlock();
    return facility;
}

/**
 * Returns the logging identifier. Should be called after log_init().
 *
 * @return The logging identifier.
 */
const char* log_get_id(void)
{
    logl_lock(); // For visibility of changes
        const char* const id = ident;
    logl_unlock();
    return id;
}

/**
 * Sets the logging options for the system logging daemon. Should be called
 * after log_init().
 *
 * @param[in] options  The logging options. Bitwise or of
 *                         LOG_PID     Log the pid with each message (default)
 *                         LOG_CONS    Log on the console if errors in sending
 *                         LOG_ODELAY  Delay open until first syslog()
 *                         LOG_NDELAY  Don't delay open (default)
 *                         LOG_NOWAIT  Don't wait for console forks: DEPRECATED
 *                         LOG_PERROR  Log to stderr as well
 */
void log_set_options(
        const unsigned options)
{
    logl_lock();
        syslog_options = options;
        // The destination is re-initialized in case it's the system logging daemon.
        int status = dest_set();
        logl_assert(status == 0);
    logl_unlock();
}

/**
 * Returns the logging options for the system logging daemon. Should be called
 * after log_init().
 *
 * @return The logging options. Bitwise or of
 *             LOG_PID     Log the pid with each message (default)
 *             LOG_CONS    Log on the console if errors in sending
 *             LOG_ODELAY  Delay open until first syslog()
 *             LOG_NDELAY  Don't delay open (default)
 *             LOG_NOWAIT  Don't wait for console forks: DEPRECATED
 *             LOG_PERROR  Log to stderr as well
 */
unsigned log_get_options(void)
{
    logl_lock(); // For visibility of changes
        const int opts = syslog_options;
    logl_unlock();
    return opts;
}
