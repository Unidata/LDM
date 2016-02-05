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

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
    int  (*open)(void);
    void (*write)(
        const log_level_t    level,
        const Message* const msg);
    int  (*close)(void);
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
static int           file_fd;
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
            log_internal("Couldn't get pathname of LDM log file from registry. "
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
static int syslog_open(void)
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
static void syslog_write(
        const log_level_t    level,
        const Message* const msg)
{
    syslog(level_to_priority(level), "%s:%d %s",
            log_basename(msg->loc.file), msg->loc.line, msg->string);
}

/**
 * Closes access to the system logging daemon.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int syslog_close(void)
{
    closelog();
    return 0;
}

static dest_funcs_t syslog_funcs = {syslog_open, syslog_write, syslog_close};

/**
 * Writes a single log message to the stream.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
static void stream_write(
        const log_level_t    level,
        const Message* const msg)
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
            log_basename(msg->loc.file), msg->loc.func, msg->loc.line,
            msg->string);
}

/**
 * Opens access to the standard error stream.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int stderr_open(void)
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
static void stderr_write(
        const log_level_t    level,
        const Message* const msg)
{
    stream_write(level, msg);
}

/**
 * Closes access to the system logging daemon.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int stderr_close(void)
{
    stream_file = NULL;
    return 0;
}

static dest_funcs_t stderr_funcs = {stderr_open, stderr_write, stderr_close};

/**
 * Locks the log file.
 */
static void file_lock(void)
{
    int status = fcntl(file_fd, F_SETLKW, &lock);
    log_assert(status == 0);
}

/**
 * Unlocks the log file.
 */
static void file_unlock(void)
{
    int status = fcntl(file_fd, F_SETLKW, &unlock);
    log_assert(status == 0);
}

/**
 * Opens access to the log file.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int file_open(void)
{
    int status;
    stream_file = fopen(dest_spec, "a");
    if (stream_file == NULL) {
        log_internal("Couldn't open log file \"%s\"", dest_spec);
        status = -1;
    }
    else {
        (void)setvbuf(stream_file, NULL, _IOLBF, BUFSIZ); // line buffering
        file_fd = fileno(stream_file);
        status = 0;
    }
    return status;
}

/**
 * Writes a single log message to the log file.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
static void file_write(
        const log_level_t    level,
        const Message* const msg)
{
    file_lock(); // To prevent concurrent access
    stream_write(level, msg);
    file_unlock();
}

/**
 * Closes access to the log file.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
static int file_close(void)
{
    int status = fclose(stream_file);
    if (status == 0) {
        stream_file = NULL;
        file_fd = -1;
    }
    return status ? -1 : 0;
}

static dest_funcs_t file_funcs = {file_open, file_write, file_close};

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
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] fmt  Format of the message.
 * @param[in] ...  Format arguments.
 */
void log_internal(
        const char* const fmt,
                          ...)
{
    char    buf[_POSIX2_LINE_MAX];
    Message msg;
    msg.next = NULL;
    msg.loc.file = __FILE__;
    msg.loc.line = __LINE__;
    msg.loc.func = __func__;
    msg.string = buf;
    msg.size = sizeof(buf);
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    dest_funcs.write(LOG_LEVEL_ERROR, &msg);
}

/******************************************************************************
 * Package API:
 ******************************************************************************/

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
int log_impl_init(
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
        status = log_set_destination(dest);
    }
    return status;
}

/**
 * Finalizes the logging module.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int log_impl_fini(void)
{
    log_lock();
    dest_funcs.close();
    log_unlock();
    return 0;
}

/**
 * Emits a single log message.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
void log_msg_write(
        const log_level_t    level,
        const Message* const msg)
{
    dest_funcs.write(level, msg);
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
        status = log_set_destination(dest_spec);
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
        status = log_set_destination(dest_spec);
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
    (void)log_set_destination(dest_spec);
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
    int status = dest_funcs.close();
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

        status = dest_funcs.open();
    }
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
    const char* path = dest_spec;
    log_unlock();
    return path;
}

int slog_is_priority_enabled(
        const log_level_t level)
{
    log_lock();
    int enabled = log_vet_level(level) && level >= logging_level;
    log_unlock();
    return enabled;
}
