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
char log_dest[_XOPEN_PATH_MAX];

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
 * The mutex that makes this module thread-safe.
 */
static pthread_mutex_t mutex;

/**
 * Acquires this module's lock.
 *
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
static void lock(void)
{
    int status = pthread_mutex_lock(&mutex);

    assert(status == 0);
}

/**
 * Releases this module's lock.
 */
static void unlock(void)
{
    int status = pthread_mutex_unlock(&mutex);

    assert(status == 0);
}

static void register_atfork_funcs(void)
{
    int status = pthread_atfork(lock, unlock, unlock);

    assert(status == 0);
}

/**
 * Asserts that the current thread has acquired this module's lock.
 */
static void assertLocked(void)
{
    assert(pthread_mutex_trylock(&mutex));
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
    assertLocked();

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
    assertLocked();

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
        while (*msg) {
            // Timestamp
            (void)fprintf(dest->stream,
                    "%04d%02d%02dT%02d%02d%02d.%06ldZ ",
                    year, month, tm.tm_mday, tm.tm_hour,
                    tm.tm_min, tm.tm_sec, microseconds);

            #define MIN0(x) ((x) >= 0 ? (x) : 0)

            // Process
            int nbytes = snprintf(NULL, 0, "%s[%d]", ident, pid);
            (void)fprintf(dest->stream, "%*s%s[%d] ", MIN0(27-nbytes),
                    "", ident, pid);

            // Location
            nbytes = snprintf(NULL, 0, "%s:%s()", basename, loc->func);
            (void)fprintf(dest->stream, "%*s%s:%s() ", MIN0(32-nbytes),
                    "", basename, loc->func);

            // Error level
            (void)fprintf(dest->stream, "%-5s ", levelId);

            // Message
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

        (void)dest->unlock(dest);
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
    assertLocked();

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
    assertLocked();

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
    assertLocked();

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
    assertLocked();

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
    assertLocked();

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
    assertLocked();

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
    assertLocked();

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
    assertLocked();

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
 * Sets the logging destination object. Uses `log_dest`.
 *
 * @pre                 Module is locked
 * @retval  0           Success
 * @retval -1           Failure. Logging destination is unchanged. log_add()
 *                      called.
 * @threadsafety        Safe
 * @asyncsignalsafety   Unsafe
 */
static int dest_set()
{
    assertLocked();

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

    lock();
        ((char*)memmove(log_dest, dest, nchars))[nchars] = 0;

        int status = dest_set(); // Uses `log_dest`
    unlock();

    return status;
}

const char*
logi_get_destination(void)
{
    lock();
        const char* const dest = log_dest;
    unlock();

    return dest;
}

int logi_internal(
        const log_level_t      level,
        const log_loc_t* const loc,
                               ...)
{
    assertLocked();

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
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
int logi_init(
        const char* const id)
{
    int status;

    if (id == NULL) {
        status = EINVAL;
    }
    else {
        /*
         * The following mutex isn't error-checking or recursive because a
         * glibc-created child process can't release such mutexes because the
         * thread in the child isn't the same as the thread in the parent. See
         * <https://stackoverflow.com/questions/5473368/pthread-atfork-locking-idiom-broken>.
         * As a consequence, failure to lock or unlock the mutex must not result
         * in a call to a logging function that attempts to lock or unlock the
         * mutex.
         */
        status = pthread_mutex_init(&mutex, NULL);

        if (status == 0) {
            lock();
                static pthread_once_t atfork_control = PTHREAD_ONCE_INIT;

                status = pthread_once(&atfork_control, register_atfork_funcs);

                (void)stderr_init(&dest);
                syslog_options = LOG_PID | LOG_NDELAY;
                syslog_facility = LOG_LDM;

                // Handle potential overlap because log_get_id() returns `ident`
                size_t nbytes = strlen(id);
                if (nbytes > sizeof(ident) - 1)
                    nbytes = sizeof(ident) - 1;
                (void)memmove(ident, logl_basename(id), nbytes);
                ident[nbytes] = 0;

                status = dest_set();

                if (status)
                    (void)pthread_mutex_destroy(&mutex);
            unlock();
        } // `mutex` initialized
    } // Valid argument

    return status ? -1 : 0;
}

int logi_reinit(void)
{
    lock();
        int status = dest_set();
    unlock();

    return status;
}

int logi_set_id(
        const char* const id)
{
    lock();
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
        int status = dest_set();
    unlock();

    return status;
}

int logi_fini(void)
{
    lock();
        dest.fini(&dest);
    unlock();

    return pthread_mutex_destroy(&mutex) ? -1 : 0;
}

int logi_log(
        const log_level_t               level,
        const log_loc_t* const restrict loc,
        const char* const restrict      string)
{
    lock();
        int status = dest.log(&dest, level, loc, string)
            ? -1
            : 0;
    unlock();

    return status;
}

int logi_flush(void)
{
    lock();
        int status = dest.flush(&dest);
    unlock();

    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

const char* log_get_default_daemon_destination(void)
{
    /*
     * Locking is unnecessary because the pathname of the LDM log file is
     * immutable
     */
    return get_ldm_logfile_pathname();
}

int log_set_facility(
        const int facility)
{
    int  status;
    int  diff_local0 = facility - LOG_LOCAL0;
    int  diff_local7 = facility - LOG_LOCAL7;

    if (diff_local0 * diff_local7 > 0 && facility != LOG_USER) {
        status = -1; // `facility` is invalid
    }
    else {
        lock();
            syslog_facility = facility;
            /*
             * The destination is re-initialized in case it's the system logging
             * daemon.
             */
            status = dest_set();
        unlock();
    }

    return status;
}

int log_get_facility(void)
{
    lock();
        int facility = syslog_facility;
    unlock();

    return facility;
}

const char* log_get_id(void)
{
    lock(); // For visibility of changes
        const char* const id = ident;
    unlock();

    return id;
}

void log_set_options(
        const unsigned options)
{
    lock();
        syslog_options = options;
        // The destination is re-initialized in case it's the system logging daemon.
        int status = dest_set();
        assert(status == 0);
    unlock();
}

unsigned log_get_options(void)
{
    lock(); // For visibility of changes
        const int opts = syslog_options;
    unlock();

    return opts;
}
