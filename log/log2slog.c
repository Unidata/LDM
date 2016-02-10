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

#include "mutex.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>

#ifndef _XOPEN_PATH_MAX
    #define _XOPEN_PATH_MAX 1024
#endif

/**
 * Functions for accessing the destination for log messages.
 */
typedef struct {
    int  (*init)(void);
    void (*print)(
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg);
    int  (*fini)(void);
} dest_funcs_t;

/**
 * The mapping from `log` logging levels to system logging daemon priorities:
 * Accessed by macros in `log_private.h`.
 */
int                  log_syslog_priorities[] = {
        LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING, LOG_ERR
};
/**
 *  Logging level.
 */
static log_level_t   logging_level = LOG_LEVEL_NOTICE;
/**
 * Specification of the destination for log messages.
 */
static char          dest_spec[_XOPEN_PATH_MAX];
/**
 * The logging stream.
 */
static FILE*         stream_file;
/**
 * The file descriptor of the log file.
 */
static int           file_fd = -1;
/**
 * Locking structure for concurrent access to the log file.
 */
static struct flock  lock;
/**
 * Unlocking structure for concurrent access to the log file.
 */
static struct flock  unlock;
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
 * Functions for accessing the destination of log messages:
 */
static dest_funcs_t  dest_funcs;

/**
 * Returns the pathname of the LDM log file.
 *
 * @return The pathname of the LDM log file
 */
static const char* get_ldm_logfile_pathname(void)
{
    static char pathname[_XOPEN_PATH_MAX];
    if (pathname[0] == 0) {
        int   reg_getString(const char* path, char** value);
        char* path;
        if (reg_getString("/log/file", &path)) {
            (void)snprintf(pathname, sizeof(pathname), "%s/ldmd.log",
                    LDM_LOG_DIR);
            pathname[sizeof(pathname)-1] = 0;
            log_warning("Couldn't get pathname of LDM log file from registry. "
                    "Using default: \"%s\".", pathname);
        }
        else {
            (void)strncpy(pathname, path, sizeof(pathname));
            pathname[sizeof(pathname)-1] = 0;
            free(path);
        }
    }
    return pathname;
}

/**
 * Returns the system logging priority associated with a logging level.
 *
 * @param[in] level    The logging level
 * @retval    LOG_ERR  `level` is invalid
 * @return             The system logging priority associated with `level`
 */
static int level_to_priority(
        const log_level_t level)
{
    return log_vet_level(level) ? log_syslog_priorities[level] : LOG_ERR;
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
            "ERROR", "CRIT", "ALERT", "FATAL"};
    return log_vet_level(level) ? strings[level] : "UNKNOWN";
}

/**
 * Opens access to the system logging daemon.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int syslog_init(void)
{
    openlog(ident, syslog_options, syslog_facility);
    return 0;
}

/**
 * Writes a single log message to the system logging daemon.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
static void syslog_print(
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    syslog(level_to_priority(level), "%s:%s():%d %s",
            log_basename(loc->file), loc->func, loc->line, msg);
}

/**
 * Closes access to the system logging daemon.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int syslog_fini(void)
{
    closelog();
    return 0;
}

static dest_funcs_t syslog_funcs = {syslog_init, syslog_print, syslog_fini};

/**
 * Writes a single log message to the stream.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
static void stream_print(
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    struct timeval now;
    (void)gettimeofday(&now, NULL);
    struct tm tm;
    (void)gmtime_r(&now.tv_sec, &tm);
    (void)fprintf(stream_file,
            "%04d%02d%02dT%02d%02d%02d.%06ldZ %s[%d] %s %s:%s():%d %s\n",
            tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min,
            tm.tm_sec, (long)now.tv_usec,
            ident, getpid(),
            level_to_string(level),
            log_basename(loc->file), loc->func, loc->line,
            msg);
}

/**
 * Opens access to the standard error stream.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int stderr_init(void)
{
    stream_file = stderr;
    return 0;
}

/**
 * Writes a single log message to the standard error stream.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
static void stderr_print(
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    stream_print(level, loc, msg);
}

/**
 * Closes access to the standard error stream.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int stderr_fini(void)
{
    stream_file = NULL;
    return 0;
}

static dest_funcs_t stderr_funcs = {stderr_init, stderr_print, stderr_fini};

/**
 * "Opens" access to the log file. Does nothing in case the destination log
 * file doesn't yet exist and another log file will be specified.
 *
 * @retval  0  Success
 */
static int file_init(void)
{
    return 0;
}

/**
 * Opens access to the log file.
 *
 * @retval  0  Success
 * @retval -1  Failure
 */
static int file_open(void)
{
    int status;
    file_fd = open(dest_spec, O_WRONLY|O_APPEND|O_CREAT,
            S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
    if (file_fd < 0) {
        log_internal(LOG_LEVEL_ERROR, "Couldn't open log file \"%s\": %s",
                dest_spec, strerror(errno));
        status = -1;
    }
    else {
        int flags = fcntl(file_fd, F_GETFD);
        (void)fcntl(file_fd, F_SETFD, flags | FD_CLOEXEC);
        stream_file = fdopen(file_fd, "a");
        (void)setvbuf(stream_file, NULL, _IOLBF, BUFSIZ); // Line buffering
        status = 0;
    }
    return status;
}

/**
 * Writes a single log message to the log file. Opens the log file if necessary.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
static void file_print(
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    if (stream_file == NULL && file_open())
        abort();
    stream_print(level, loc, msg);
}

/**
 * Closes access to the log file.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int file_fini(void)
{
    int status;
    if (stream_file) {
        status = fclose(stream_file);
        if (status == 0) {
            stream_file = NULL;
            file_fd = -1;
        }
    }
    return status ? -1 : 0;
}

static dest_funcs_t file_funcs = {file_init, file_print, file_fini};

/******************************************************************************
 * Package API:
 ******************************************************************************/

/**
 * Returns the logging destination. Should be called between log_init() and
 * log_fini().
 *
 * @pre          Module is locked
 * @return       The logging destination. One of <dl>
 *                   <dt>""      <dd>The system logging daemon.
 *                   <dt>"-"     <dd>The standard error stream.
 *                   <dt>else    <dd>The pathname of the log file.
 *               </dl>
 */
const char* log_get_destination_impl(void)
{
    return dest_spec;
}

/**
 * Sets the logging destination.
 *
 * @pre                Module is locked
 * @param[in] dest     The logging destination. Caller may free. One of <dl>
 *                         <dt>""   <dd>The system logging daemon.
 *                         <dt>"-"  <dd>The standard error stream.
 *                         <dt>else <dd>The file whose pathname is `dest`.
 *                     </dl>
 * @retval    0        Success.
 * @retval    -1       Failure.
 */
int log_set_destination_impl(
        const char* const dest)
{
    int status = dest_funcs.fini();
    if (status == 0) {
        /*
         * Handle potential overlap because `log_get_destination()` returns
         * `dest_spec`
         */
        size_t nbytes = strlen(dest);
        if (nbytes > sizeof(dest_spec) - 1)
            nbytes = sizeof(dest_spec) - 1;
        ((char*)memmove(dest_spec, dest, nbytes))[nbytes] = 0;

        dest_funcs = LOG_IS_SYSLOG_SPEC(dest)
            ? syslog_funcs
            : LOG_IS_STDERR_SPEC(dest)
                ? stderr_funcs
                : file_funcs;

        status = dest_funcs.init();
    }
    return status;
}

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] level  Logging level.
 * @param[in] loc    Location where the message was generated.
 * @param[in] ...    Message arguments -- starting with the format.
 */
void log_internal_located(
        const log_level_t      level,
        const log_loc_t* const loc,
                               ...)
{
    va_list args;
    va_start(args, loc);
    const char* fmt = va_arg(args, const char*);
    char        buf[_POSIX2_LINE_MAX];
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    dest_funcs.print(level, loc, buf);
}

/**
 * Initializes the logging module. Should be called before any other function.
 * - `log_get_id()`       will return the filename component of `id`
 * - `log_get_facility()` will return `LOG_LDM`
 * - `log_get_level()`    will return `LOG_LEVEL_NOTICE`
 * - `log_get_options()`  will return `LOG_PID | LOG_NDELAY`
 * - `log_get_destination()`   will return
 *                          - The pathname of the LDM log file if the process
 *                            is a daemon
 *                          - "-" otherwise
 *
 * @param[in] id       The pathname of the program (e.g., `argv[0]`). Caller may
 *                     free.
 * @retval    0        Success.
 * @retval    -1       Error. Logging module is in an unspecified state.
 */
int log_init_impl(
        const char* id)
{
    int status;
    if (id == NULL) {
        status = -1;
    }
    else {
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0; // entire object
        unlock = lock;
        unlock.l_type = F_UNLCK;
        dest_funcs = stderr_funcs;
        logging_level = LOG_LEVEL_NOTICE;
        syslog_options = LOG_PID | LOG_NDELAY;
        syslog_facility = LOG_LDM;

        strncpy(ident, log_basename(id), sizeof(ident))[sizeof(ident)-1] = 0;

        const char* dest = log_get_default_destination();
        status = log_set_destination_impl(dest);
    }
    return status;
}

/**
 * Re-initializes the logging module based on its state just prior to calling
 * log_fini_impl(). If log_fini_impl(), wasn't called, then the result is
 * unspecified.
 *
 * @retval    0        Success.
 */
int log_reinit_impl(void)
{
    return 0;
}

/**
 * Finalizes the logging module.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int log_fini_impl(void)
{
    log_lock();
    dest_funcs.fini();
    log_unlock();
    return 0;
}

/**
 * Emits a single log message.
 *
 * @param[in] level  Logging level.
 * @param[in] loc    The location where the message was generated.
 * @param[in] string The message.
 */
void log_write(
        const log_level_t               level,
        const log_loc_t* const restrict loc,
        const char* const restrict      string)
{
    dest_funcs.print(level, loc, string);
}

int slog_is_priority_enabled(
        const log_level_t level)
{
    log_lock();
    int enabled = log_vet_level(level) && level >= logging_level;
    log_unlock();
    return enabled;
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
    return get_ldm_logfile_pathname();
}

/**
 * Enables logging down to a given level. Should be called after log_init().
 *
 * @param[in] level  The lowest level through which logging should occur.
 * @retval    0      Success.
 * @retval    -1     Failure.
 */
int log_set_level(
        const log_level_t level)
{
    int status;
    if (!log_vet_level(level)) {
        status = -1;
    }
    else {
        log_lock();
        logging_level = level;
        log_unlock();
        status = 0;
    }
    return status;
}

/**
 * Returns the current logging level. Should be called after log_init().
 *
 * @return The lowest level through which logging will occur. The initial value
 *         is `LOG_LEVEL_DEBUG`.
 */
log_level_t log_get_level(void)
{
    log_lock();
    log_level_t level = logging_level;
    log_unlock();
    return level;
}

/**
 * Sets the facility that will be used (e.g., `LOG_LOCAL0`) when logging to the
 * system logging daemon. Should be called after `log_init()`.
 *
 * @param[in] facility  The facility that will be used when logging to the
 *                      system logging daemon.
 * @retval    0         Success (always)
 * @retval    -1        Failure
 */
int log_set_facility(
        const int facility)
{
    int  status;
    bool sign_local0 = facility > LOG_LOCAL0;
    bool sign_local7 = facility > LOG_LOCAL7;
    if (sign_local0 == sign_local7 && facility != LOG_USER) {
        status = -1; // `facility` is invalid
    }
    else {
        log_lock();
        syslog_facility = facility;
        status = log_set_destination_impl(dest_spec);
        log_unlock();
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
    log_lock();
    int facility = syslog_facility;
    log_unlock();
    return facility;
}

/**
 * Sets the logging identifier. Should be called after `log_init()`.
 *
 * @param[in] id        The new identifier. Caller may free.
 * @retval    0         Success.
 * @retval    -1        Failure.
 */
int log_set_id(
        const char* const id)
{
    int status;
    if (id == NULL) {
        status = -1;
    }
    else {
        log_lock();
        strncpy(ident, id, sizeof(ident))[sizeof(ident)-1] = 0;
        status = log_set_destination_impl(dest_spec);
        log_unlock();
    }
    return status;
}

/**
 * Returns the logging identifier. Should be called after log_init().
 *
 * @return The logging identifier.
 */
const char* log_get_id(void)
{
    log_lock();
    const char* const id = ident;
    log_unlock();
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
    log_lock();
    syslog_options = options;
    (void)log_set_destination_impl(dest_spec);
    log_unlock();
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
    log_lock();
    const int opts = syslog_options;
    log_unlock();
    return opts;
}

/**
 * Sets the logging destination. Should be called between log_init() and
 * log_fini().
 *
 * @param[in] dest     The logging destination. Caller may free. One of <dl>
 *                         <dt>""   <dd>The system logging daemon.
 *                         <dt>"-"  <dd>The standard error stream.
 *                         <dt>else <dd>The file whose pathname is `dest`.
 *                     </dl>
 * @retval    0        Success.
 * @retval    -1       Failure.
 */
int log_set_destination(
        const char* const dest)
{
    log_lock();
    int status = log_set_destination_impl(dest);
    log_unlock();
    return status;
}

/**
 * Returns the logging destination. Should be called between log_init() and
 * log_fini().
 *
 * @return       The logging destination. One of <dl>
 *                   <dt>""      <dd>The system logging daemon.
 *                   <dt>"-"     <dd>The standard error stream.
 *                   <dt>else    <dd>The pathname of the log file.
 *               </dl>
 */
const char* log_get_destination(void)
{
    log_lock();
    const char* path = log_get_destination_impl();
    log_unlock();
    return path;
}
