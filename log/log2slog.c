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

/******************************************************************************
 * Private API:
 ******************************************************************************/

/**
 * Functions for accessing the destination for log messages.
 */
typedef struct {
    void (*init)(void);
    void (*log)(
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg);
    void (*fini)(void);
} dest_funcs_t;

/**
 * The logging stream.
 */
static FILE*         stream_file;
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
            logl_internal(LOG_LEVEL_WARNING,
                    "Couldn't get pathname of LDM log file from registry. "
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
    return logl_vet_level(level) ? strings[level] : "UNKNOWN";
}

/**
 * Initializes access to the system logging daemon.
 */
static void syslog_init(void)
{
    openlog(ident, syslog_options, syslog_facility);
}

/**
 * Writes a single log message to the system logging daemon.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
static void syslog_log(
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    syslog(logl_level_to_priority(level), "%s:%s():%d %s",
            logl_basename(loc->file), loc->func, loc->line, msg);
}

/**
 * Finalizes access to the system logging daemon.
 */
static void syslog_fini(void)
{
    closelog();
}

static dest_funcs_t syslog_funcs = {syslog_init, syslog_log, syslog_fini};

/**
 * Writes a single log message to the stream.
 *
 * @param[in] level  The Logging level.
 * @param[in] loc    The location where the message was created.
 * @param[in] msg    The message.
 */
static void stream_log(
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
            logl_basename(loc->file), loc->func, loc->line,
            msg);
}

/**
 * Initializes access to the standard error stream.
 */
static void stderr_init(void)
{
    stream_file = stderr;
}

/**
 * Writes a single log message to the standard error stream.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
static void stderr_log(
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    stream_log(level, loc, msg);
}

/**
 * Finalizes access to the standard error stream.
 */
static void stderr_fini(void)
{
    stream_file = NULL;
}

static dest_funcs_t stderr_funcs = {stderr_init, stderr_log, stderr_fini};

/**
 * Initializes access to the log file. Does nothing in case the destination log
 * file doesn't yet exist and another log file will be specified.
 */
static inline void file_init(void)
{
}

/**
 * Opens the log file.
 *
 * @retval  0  Success
 * @retval -1  Failure
 */
static int file_open(void)
{
    int status;
    int file_fd = open(log_dest, O_WRONLY|O_APPEND|O_CREAT,
            S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH);
    if (file_fd < 0) {
        /*
         * Can't log message because logl_internal() -> logi_log() ->
         * file_log() -> file_open()
         */
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
 * Writes a single message to the log file. Opens the log file if necessary.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
static void file_log(
        const log_level_t         level,
        const log_loc_t* restrict loc,
        const char* restrict      msg)
{
    if (stream_file == NULL && file_open())
        abort();
    stream_log(level, loc, msg);
}

/**
 * Finalizes access to the log file.
 */
static void file_fini(void)
{
    if (stream_file != NULL) {
        (void)fclose(stream_file);
        stream_file = NULL;
    }
}

static dest_funcs_t file_funcs = {file_init, file_log, file_fini};

/******************************************************************************
 * Package-Private Implementation API:
 ******************************************************************************/

/**
 * Sets the logging destination.
 *
 * @pre                Module is locked
 * @retval  0          Success
 * @retval -1          Failure
 */
int logi_set_destination(void)
{
    dest_funcs.fini();
    dest_funcs = LOG_IS_SYSLOG_SPEC(log_dest)
        ? syslog_funcs
        : LOG_IS_STDERR_SPEC(log_dest)
            ? stderr_funcs
            : file_funcs;
    dest_funcs.init();
    return 0;
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
        dest_funcs.log(LOG_LEVEL_ERROR, &location, "vsnprintf() failure");
    }
    else {
        buf[sizeof(buf)-1] = 0;
        dest_funcs.log(level, loc, buf);
    }
    va_end(args);
}

/**
 * Initializes the logging module. Should be called before any other function.
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
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0; // entire object
        unlock = lock;
        unlock.l_type = F_UNLCK;
        dest_funcs = stderr_funcs;
        syslog_options = LOG_PID | LOG_NDELAY;
        syslog_facility = LOG_LDM;

        strncpy(ident, logl_basename(id), sizeof(ident))[sizeof(ident)-1] = 0;
        status = logi_set_destination();
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
    return 0;
}

/**
 * Finalizes the logging module.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int logi_fini(void)
{
    dest_funcs.fini();
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
    dest_funcs.log(level, loc, string);
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
    if (!logl_vet_level(level)) {
        status = -1;
    }
    else {
        logl_lock();
        log_level = level;
        logl_unlock();
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
    logl_lock();
    log_level_t level = log_level;
    logl_unlock();
    return level;
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
    bool sign_local0 = facility > LOG_LOCAL0;
    bool sign_local7 = facility > LOG_LOCAL7;
    if (sign_local0 == sign_local7 && facility != LOG_USER) {
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
        dest_funcs.init();
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
        logl_lock();
        strncpy(ident, id, sizeof(ident))[sizeof(ident)-1] = 0;
        /*
         * The destination is re-initialized in case it's the system logging
         * daemon.
         */
        dest_funcs.init();
        logl_unlock();
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
    dest_funcs.init();
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
