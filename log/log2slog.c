/**
 * Copyright 2019 University Corporation for Atmospheric Research. All rights
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
#include "log.h"

#include <assert.h>
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
 * The persistent destination specification.
 */
char log_dest[_XOPEN_PATH_MAX] = "-"; // Standard error stream by default

/**
 * Destination object for log messages.
 */
typedef struct dest {
    /**
     * Writes a single log message. The message might be pending until `flush()`
     * is called.
     *
     * @param[in,out] dest   Destination object
     * @param[in]     level  Logging level.
     * @param[in]     loc    Location where the message was created.
     * @param[in]     msg    Message.
     * @retval        0      Success
     * @threadsafety         Safe
     * @asyncsignalsafety    Unsafe
     */
    int (*log)(
        struct dest*              dest,
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg);
    /**
     * Flushes all pending log messages.
     *
     * @param[in,out] dest  Destination object
     * @retval        0     Success
     * @retval        -1    Failure
     * @threadsafety        Safe
     * @asyncsignalsafety   Unsafe
     */
    int  (*flush)(struct dest*);
    /**
     * Returns the file descriptor associated with logging.
     *
     * @param[in,out] dest  Destination object
     * @retval -1           No knowable file descriptor is used (e.g., syslog)
     * @return              The file descriptor that will be used for logging
     * @threadsafety        Safe
     * @asyncsignalsafety   Unsafe
     */
    int  (*get_fd)(const struct dest*);
    /**
     * Finalizes access to the logging system.
     *
     * @param[in,out] dest  Destination object
     * @retval        0     Success
     * @retval        -1    Failure
     * @threadsafety        Unsafe
     * @asyncsignalsafety   Unsafe
     */
    int  (*fini)(struct dest*);
    /**
     * Locks access to logging as much as possible (i.e., thread and process).
     *
     * @param[in,out] dest     Destination object
     * @retval        0        Success
     * @retval        EINTR    Interrupted by signal
     * @retval        ENOLCK   Creating a locked region would exceed system limit
     * @retval        EDEADLK  Deadlock detected
     * @threadsafety           Safe
     * @asyncsignalsafety      Unsafe
     */
    int  (*lock)(struct dest*);
    /**
     * Unlocks access to logging as much as possible (i.e., thread and process).
     *
     * @param[in,out] dest  Destination object
     * @retval        0     Success
     * @retval    EINTR     Interrupted by signal
     * @retval    EBADF     Invalid file descriptor
     * @threadsafety        Safe
     * @asyncsignalsafety   Unsafe
     */
    int  (*unlock)(struct dest*);
    /**
     * Logging stream. Unused for logging via the system logging daemon
     */
    FILE*  stream;
} dest_t;
/**
 * The identifier for log messages
 */
static char            ident[_XOPEN_PATH_MAX];
/**
 * System logging daemon options
 */
static int             syslog_options = LOG_PID | LOG_NDELAY;
/**
 * System logging facility
 */
static int             syslog_facility = LOG_LDM;
/**
 * The destination of log messages:
 */
static dest_t          dest;

/**
 * Returns the pathname of the LDM log file.
 *
 * @return             The pathname of the LDM log file
 * @asyncsignalsafety  Unsafe
 */
static const char* get_ldm_logfile_pathname(void)
{
    static char pathname[_XOPEN_PATH_MAX];
    char homePath[_XOPEN_PATH_MAX];

    memset((void *)homePath, 0, sizeof(homePath));

    if (pathname[0] == 0) {
        int   reg_getString(const char* key, char** value);
        char* value;

        if (reg_getString("/log/file", &value)) 
	{
            // No entry in registry
	    strncpy(homePath, getenv("HOME"), (sizeof(homePath) - 1));
	    if ((strlen(homePath)) == 0)
	      strcpy(homePath, "/tmp/ldmhome");
            (void)snprintf(pathname, sizeof(pathname), "%s/var/logs/ldmd.log", homePath);
            pathname[sizeof(pathname)-1] = 0;
        }
        else {
            (void)strncpy(pathname, value, sizeof(pathname));
            pathname[sizeof(pathname)-1] = 0;
            free(value); // Async-signal-unsafe
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
            "ERROR", "FATAL"};

    return logl_vet_level(level) ? strings[level] : "UNKNOWN";
}

static void dest_unlock(void* dest)
{
    dest_t* dst = dest;
    int     status = dst->unlock(dst);

    if (status && status != EINTR)
        abort();
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
 * @threadsafety        Safe
 * @asyncsignalsafety   Unsafe
 */
static int syslog_log(
        dest_t* const             dest,
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    syslog(logl_level_to_priority(level), "%s:%d:%s() %s",
            logl_basename(loc->file), loc->line, loc->func, msg);

    return 0;
}

/**
 * Flushes logging to the system logging daemon.
 *
 * @param[in,out] dest  Destination object
 * @retval        0     Always
 * @threadsafety        Safe
 * @asyncsignalsafety   Safe
 */
static int syslog_flush(
        dest_t* const dest)
{
    return 0;
}

/**
 * Returns the file descriptor that will be used for logging.
 *
 * @param[in,out] dest  Destination object
 * @retval -1           No file descriptor will be used
 * @return              The file descriptor that will be used for logging
 * @threadsafety        Safe
 * @asyncsignalsafety   Safe
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
 * @retval    0         Always
 * @threadsafety        Unsafe
 * @asyncsignalsafety   Unsafe
 */
static int syslog_fini(
        dest_t* const             dest)
{
    closelog();
    return 0;
}

/**
 * Initializes access to the system logging daemon.
 *
 * @retval 0           Success (always)
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
static int syslog_init(
        dest_t* const dest)
{
    openlog(ident, syslog_options, syslog_facility);
    dest->log = syslog_log;
    dest->flush = syslog_flush;
    dest->get_fd = syslog_get_fd;
    dest->fini = syslog_fini;
    dest->lock = NULL;
    dest->unlock = NULL;

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
 * @threadsafety        Safe
 * @asyncsignalsafety   Unsafe
 */
static inline int stream_get_fd(
        const dest_t* const dest)
{
    return fileno(dest->stream);
}

/**
 * Writes a single log message to a stream.
 *
 * @param[in,out] dest     Destination object
 * @param[in]     level    Logging level.
 * @param[in]     loc      Location where the message was created.
 * @param[in]     msg      Message.
 * @retval        0        Success
 * @retval        EINTR    Interrupted by signal
 * @retval        ENOLCK   Creating a locked region would exceed system limit
 * @retval        EDEADLK  Deadlock detected
 * @threadsafety           Safe
 * @asyncsignalsafety      Unsafe
 */
static int stream_log(
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
    int               status = dest->lock(dest);

    if (status == 0) {
    	pthread_cleanup_push(dest_unlock, dest);

        while (*msg) {
            // Timestamp
            int pos = fprintf(dest->stream,
                    "%04d%02d%02dT%02d%02d%02d.%06ldZ ",
                    year, month, tm.tm_mday, tm.tm_hour,
                    tm.tm_min, tm.tm_sec, microseconds);

            // Process
            pos += fprintf(dest->stream, "%s[%d] ", ident, pid);

            // Location
            static const int LOC_POS = 52;
            if (pos < LOC_POS)
                pos += fprintf(dest->stream, "%*s", LOC_POS - pos, "");
            pos += fprintf(dest->stream, "%s:%s:%d ", basename, loc->func,
                    loc->line);

            // Error level
            static const int LVL_POS = 88;
            if (pos < LVL_POS)
                pos += fprintf(dest->stream, "%*s", LVL_POS - pos, "");
            pos += fprintf(dest->stream, "%s ", levelId);

            // Message
            static const int MSG_POS = 94;
            if (pos < MSG_POS)
                (void)fprintf(dest->stream, "%*s", MSG_POS - pos, "");
            const char* const newline = strchr(msg, '\n');
            if (newline) {
                (void)fprintf(dest->stream, "%.*s\n", (int)(newline-msg), msg);
                msg = newline + 1;
            }
            else {
                (void)fprintf(dest->stream, "%s\n", msg);
                break;
            }
        } // Output-line loop

        pthread_cleanup_pop(true);
    } // Log stream is locked

    return status;
}

/**
 * Flushes the logging stream.
 *
 * @param[in,out] dest  Destination object
 * @retval        0     Success
 * @retval        -1    Failure
 * @threadsafety        Safe
 * @asyncsignalsafety   Unsafe
 */
static inline int stream_flush(
        dest_t* const             dest)
{
    return fflush(dest->stream)
            ? -1
            : 0;
}

/******************************************************************************
 * Logging is to the standard error stream:
 ******************************************************************************/

/**
 * Locks access to the standard error stream for the current thread only.
 *
 * @param[in,out] dest  Destination object
 * @retval        0     Always
 * @threadsafety        Safe
 * @asyncsignalsafety   Unsafe
 */
static int stderr_lock(dest_t* const dest)
{
    flockfile(dest->stream);
    return 0;
}

/**
 * Unlocks access to the standard error stream.
 *
 * @param[in,out] dest  Destination object
 * @retval        0     Always
 * @threadsafety        Safe
 * @asyncsignalsafety   Unsafe
 */
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
 * @threadsafety        Safe
 * @asyncsignalsafety   Safe
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
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
static int file_lock(dest_t* const dest)
{
    struct flock lock;

    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    // `l_start = l_len = 0` => entire file
    lock.l_start = 0;
    lock.l_len = 0;

    return (fcntl(stream_get_fd(dest), F_SETLKW, &lock) == -1)
        ? errno
        : 0;
}

/**
 * Unlocks a log file for access by another process.
 *
 * @param[in] dest     Logging destination
 * @retval    0        Success
 * @retval    EINTR    Interrupted by signal
 * @retval    EBADF    Invalid file descriptor
 */
static int file_unlock(dest_t* const dest)
{
    struct flock lock;

    lock.l_type = F_UNLCK;
    lock.l_whence = SEEK_SET;
    // `l_start = l_len = 0` => entire file
    lock.l_start = 0;
    lock.l_len = 0;

    return (fcntl(stream_get_fd(dest), F_SETLK, &lock) == -1)
        ? errno
        : 0;
}

/**
 * Finalizes access to the log file.
 *
 * @param[in,out] dest  Destination object
 * @threadsafety        Unsafe
 * @asyncsignalsafety   Unsafe
 */
static int file_fini(dest_t* const dest)
{
    int status;

    if (dest->stream == NULL) {
        status = 0;
    }
    else {
        status = fclose(dest->stream); // Will flush
        dest->stream = NULL;

        if (status)
            status = -1;
    }

    return status;
}

/**
 * Ensures that a file descriptor will close if and when a function of the
 * `exec()` family is called.
 *
 * @param[in] fd       File descriptor
 * @retval  0          Success
 * @retval -1          Failure
 * @threadsafety       Safe
 * @asyncsignalsafety  Safe
 */
static int ensure_close_on_exec(
        const int fd)
{
    int status = fcntl(fd, F_GETFD);

    if (status != -1)
        status = (status & FD_CLOEXEC)
                ? 0
                : fcntl(fd, F_SETFD, status | FD_CLOEXEC);

    return status;
}

/**
 * Initializes access to a log file.
 *
 * @param[in,out] dest  Destination object
 * @retval  0           Success
 * @retval -1           Failure
 * @threadsafety        Safe
 * @asyncsignalsafety   Unsafe
 */
static int file_init(
        dest_t* const dest)
{
    int status;
    int fd = open(log_dest, O_WRONLY|O_APPEND|O_CREAT,
            S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);

    if (fd < 0) {
        status = -1;
    }
    else {
        status = ensure_close_on_exec(fd);

        if (status) {
            close(fd);
        }
        else {
            dest->stream = fdopen(fd, "a");

            if (dest->stream == NULL) {
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
    }

    return status;
}

/**
 * Initializes the logging destination object. Uses `log_dest`.
 *
 * @retval  0           Success
 * @retval -1           Failure. Logging destination is unchanged. `log_add()`
 *                      called.
 * @threadsafety        Unsafe
 * @asyncsignalsafety   Unsafe
 */
static int dest_init()
{
    dest_t new_dest;
    int    status =
            LOG_IS_SYSLOG_SPEC(log_dest)
                ? syslog_init(&new_dest)
                : LOG_IS_STDERR_SPEC(log_dest)
                  ? stderr_init(&new_dest)
                  : file_init(&new_dest);

    if (status == 0) {
        dest.fini(&dest);
        dest = new_dest;
    }

    return status;
}

/******************************************************************************
 * Package-Private Implementation API:
 ******************************************************************************/

int logi_set_destination(const char* const dest)
{
    size_t nchars = strlen(dest);

    if (nchars > sizeof(log_dest) - 1)
        nchars = sizeof(log_dest) - 1;

    ((char*)memmove(log_dest, dest, nchars))[nchars] = 0;

    return dest_init(); // Uses `log_dest`
}

const char*
logi_get_destination(void)
{
    return log_dest;
}

int logi_internal(
        const log_level_t      level,
        const log_loc_t* const loc,
                               ...)
{
    int     status;
    va_list args;

    va_start(args, loc);
        const char* fmt = va_arg(args, const char*);
        char        buf[_POSIX2_LINE_MAX];
        int         nbytes = vsnprintf(buf, sizeof(buf), fmt, args);

        if (nbytes < 0) {
            LOG_LOC_DECL(location);
            dest.log(&dest, LOG_LEVEL_ERROR, &location, "vsnprintf() failure");
            status = -1;
        }
        else {
            buf[sizeof(buf)-1] = 0;
            status = dest.log(&dest, level, loc, buf);

            if (status)
                status = -1;
        }
    va_end(args);

    return status;
}

/**
 * Initializes the logging module.
 * - log_get_id()           will return the filename component of `id`
 * - log_get_facility()     will return `LOG_LDM`
 * - log_get_level()        will return `LOG_LEVEL_NOTICE`
 * - log_get_options()      will return `LOG_PID | LOG_NDELAY`
 * - log_get_destination()  will return the destination for log messages
 *                              - ""   System logging daemon
 *                              - "-"  Standard error stream
 *                              -      Else  pathname of log file
 *
 * @param[in] id       The pathname of the program (e.g., `argv[0]`). Caller may
 *                     free.
 * @retval    0        Success
 * @retval    -1       Failure
 * @threadsafety       Unsafe
 * @asyncsignalsafety  Unsafe
 */
int logi_init(const char* const id)
{
    int status = -1;

    if (id != NULL) {
        (void)strcpy(log_dest, STDERR_SPEC);
        (void)stderr_init(&dest); // Necessary to initialize `dest`

        syslog_options = LOG_PID | LOG_NDELAY;
        syslog_facility = LOG_LDM;

        // Handle potential overlap because log_get_id() returns `ident`
        size_t nbytes = strlen(id);
        if (nbytes > sizeof(ident) - 1)
            nbytes = sizeof(ident) - 1;
        (void)memmove(ident, logl_basename(id), nbytes);
        ident[nbytes] = 0;

        status = dest_init();
    } // Valid argument

    return status;
}

int logi_reinit(void)
{
    return dest_init();
}

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
    return dest_init();
}

int logi_fini(void)
{
    dest.fini(&dest);
    (void)strcpy(log_dest, "-"); // Standard error stream by default

    return 0;
}

int logi_log(
        const log_level_t               level,
        const log_loc_t* const restrict loc,
        const char* const restrict      string)
{
    return dest.log(&dest, level, loc, string)
        ? -1
        : 0;
}

int logi_flush(void)
{
    return dest.flush(&dest);
}

/**
 * Returns the default logging destination when a daemon
 *
 * @return             Default logging destination when a daemon
 * @asyncsignalsafety  Unsafe
 */
const char* logi_get_default_daemon_destination(void)
{
    return get_ldm_logfile_pathname();
}

int logi_set_facility(
        const int facility)
{
    int  status;
    int  diff_local0 = facility - LOG_LOCAL0;
    int  diff_local7 = facility - LOG_LOCAL7;

    if (diff_local0 * diff_local7 > 0 && facility != LOG_USER) {
        status = -1; // `facility` is invalid
    }
    else {
        syslog_facility = facility;
        /*
         * The destination is re-initialized in case it's the system logging
         * daemon.
         */
        status = dest_init();
    }

    return status;
}

int logi_get_facility(void)
{
    return syslog_facility;
}

const char* logi_get_id(void)
{
    return ident;
}

int logi_set_options(const unsigned options)
{
    syslog_options = options;

    // The destination is re-initialized in case it's the system logging daemon.
    return dest_init();
}

unsigned logi_get_options(void)
{
    return syslog_options;
}
