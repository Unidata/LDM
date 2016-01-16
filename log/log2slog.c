/**
 * Copyright 2016 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: log2ulog.c
 * @author: Steven R. Emmerson
 *
 * This file implements the `log.h` API using `ulog.c`.
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
 * The mapping from `log` logging levels to system logging daemon priorities:
 */
int                  log_syslog_priorities[] = {
        LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING, LOG_ERR
};
/**
 *  Logging level.
 */
static log_level_t logging_level = LOG_LEVEL_NOTICE;
/**
 * The thread identifier of the thread on which `log_init()` was called.
 */
static pthread_t     init_thread;
/**
 * The mutex that makes this module thread-safe.
 */
static mutex_t       mutex;
/**
 * The log file stream.
 */
static FILE*         output_stream;
/**
 * The identifier for log messages
 */
static char          ident[_XOPEN_PATH_MAX];
/**
 * Specification of the destination for log messages.
 */
static char          output_spec[_XOPEN_PATH_MAX];
/**
 * System logging daemon options
 */
static int           syslog_options = LOG_PID | LOG_NDELAY;
/**
 * System logging facility
 */
static int           syslog_facility = LOG_LDM;

static void get_ldm_logfile_pathname(
        char* const  buf,
        const size_t size)
{
    char* pathname;
    int   reg_getString(const char* path, char** pathname);
    if (reg_getString("/log/file", &pathname)) {
        (void)snprintf(buf, size, "%s/ldmd.log", LDM_LOG_DIR);
        buf[size-1] = 0;
        log_internal("Couldn't get pathname of LDM log file from registry. "
                "Using default \"%s\".", buf);
    }
    else {
        (void)strncpy(buf, pathname, size);
        buf[size-1] = 0;
        free(pathname);
    }
}

static inline void lock(void)
{
    if (mutex_lock(&mutex))
        abort();
}

static inline void unlock(void)
{
    if (mutex_unlock(&mutex))
        abort();
}

/**
 * Opens an output stream on a file for logging.
 *
 * @param[in] pathname  Pathname of the file
 * @retval    NULL      Failure
 * @return              An output stream
 */
static FILE* open_output_stream(
        const char* const pathname)
{
    FILE* stream;
    if (pathname == NULL) {
        stream = NULL;
    }
    else {
        stream = fopen(pathname, "a");
        if (stream == NULL) {
            log_internal("Couldn't open log file \"%s\"", pathname);
        }
        else {
            (void)setvbuf(stream, NULL, _IOLBF, BUFSIZ); // line buffering
        }
    }
    return stream;
}

/*
 * The `stream_` functions keep `output_spec` and `output_stream` consistent
 * with each other.
 */

/**
 * Closes the output stream. Idempotent.
 */
static void stream_close(void)
{
    if (output_stream && output_stream != stderr)
        (void)fclose(output_stream);
    output_stream = NULL;
    output_spec[0] = 0;
}

/**
 * Sets the output stream. Should only be called by other `stream_` functions.
 * Idempotent.
 *
 * @param[in] spec      The new output stream specification.
 * @param[in] stream    The new output stream.
 * @retval    0         Success
 * @retval    -1        Failure
 */
static int stream_set(
        const char* const spec,
        FILE* const       stream)
{
    int status;
    if (spec == NULL || stream == NULL) {
        status = -1;
    }
    else {
        stream_close();
        output_stream = stream;
        size_t nbytes = strlen(spec);
        if (nbytes > sizeof(output_spec) - 1)
            nbytes = sizeof(output_spec) - 1;
        (void)memmove(output_spec, spec, nbytes); // `spec == output_spec`?
        output_spec[nbytes] = 0;
        status = 0;
    }
    return status;
}

/**
 * Sets the output stream to a file. Should only be called by other `stream_`
 * functions. Idempotent.
 *
 * @param[in] pathname  Pathname of the file.
 * @retval    0         Success
 * @retval    -1        Failure
 */
static int stream_set_file(
        const char* const pathname)
{
    FILE* stream = open_output_stream(pathname);
    return stream_set(pathname, stream);
}

/**
 * Opens the output stream. Idempotent.
 *
 * @param[in] spec  Specification of the output:
 *                      - "-"  standard error stream
 *                      - else file whose pathname is `spec`
 * @retval    0     Success
 * @retval    -1    Failure
 */
static int stream_open(
        const char* const spec)
{
    int status;
    if (strcmp(spec, output_spec) == 0 && output_stream) {
        status = 0; // already open
    }
    else {
        // New output stream
        status = strcmp(spec, "-")
                ? stream_set_file(spec)
                : stream_set("-", stderr);
    }
    return status;
}

/**
 * Sets the destination for log messages. Idempotent.
 *
 * @param[in] output  Specification of the destination for log messages:
 *                        - ""   The system logging daemon
 *                        - "-"  The standard error stream
 *                        - else The file whose pathname is `output`
 * @retval    0       Success
 * @retval    -1      Failure
 */
static int set_output(
        const char* const output)
{
    int status;
    if (strcmp(output, "")) {
        status = stream_open(output);
        if (status == 0)
            closelog();
    }
    else {
        stream_close();
        openlog(ident, syslog_options, syslog_facility);
        status = 0;
    }
    return status;
}

/**
 * Initializes the destination for log messages. If the current process is a
 * daemon, then logging will be to the LDM log file; otherwise, logging will be
 * to the standard error stream.
 *
 * @retval 0   Success
 * @retval -1  Failure
 */
static int init_output(void)
{
    int  status;
    char output[_XOPEN_PATH_MAX];
    int  ttyFd = open("/dev/tty", O_RDONLY);
    if (-1 == ttyFd) {
        // No controlling terminal => daemon => use LDM log file
        get_ldm_logfile_pathname(output, sizeof(output));
    }
    else {
        // Controlling terminal exists => interactive => log to `stderr`
        (void)close(ttyFd);
        (void)strcpy(output, "-");
    }
    return set_output(output);
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
 * Emits a single log message.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
void log_write_one(
        const log_level_t    level,
        const Message* const   msg)
{
    if (output_stream) {
        struct timeval now;
        (void)gettimeofday(&now, NULL);
        struct tm tm;
        (void)gmtime_r(&now.tv_sec, &tm);
        (void)fprintf(output_stream,
                "%04d%02d%02dT%02d%02d%02d.%06ldZ %s[%d] %s %s:%d %s\n",
                tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min,
                tm.tm_sec, (long)now.tv_usec,
                ident, getpid(),
                level_to_string(level),
                log_basename(msg->loc.file), msg->loc.line,
                msg->string);
    }
    else {
        syslog(level_to_priority(level), "%s:%d %s",
                log_basename(msg->loc.file), msg->loc.line, msg->string);
    }
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
    msg.string = buf;
    msg.size = sizeof(buf);
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log_write_one(LOG_LEVEL_ERROR, &msg);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Initializes the logging module. Should be called before any other function.
 * - `log_get_id()`       will return the filename component of `id`
 * - `log_get_facility()` will return `LOG_LDM`
 * - `log_get_level()`    will return `LOG_LEVEL_NOTICE`
 * - `log_get_options()`  will return `LOG_PID | LOG_NDELAY`
 * - `log_get_output()`   will return the pathname of the LDM logfile
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
        logging_level = LOG_LEVEL_NOTICE;
        syslog_options = LOG_PID | LOG_NDELAY;
        syslog_facility = LOG_LDM;

        strncpy(ident, log_basename(id), sizeof(ident))[sizeof(ident)-1] = 0;

        status = init_output();
        if (status == 0) {
            status = mutex_init(&mutex, true, true);
            if (status == 0)
                init_thread = pthread_self();
        }
    }
    return status ? -1 : 0;
}

/**
 * Refreshes the logging module. In particular, if logging is to a file, then
 * the file is closed and re-opened; thus enabling log file rotation. Should be
 * called after log_init().
 *
 * @retval  0  Success.
 * @retval -1  Failure.
 */
int log_refresh(void)
{
    lock();
    char output[_XOPEN_PATH_MAX];
    strncpy(output, output_spec, sizeof(output))[sizeof(output)-1] = 0;
    stream_close(); // Enable log file rotation
    int status = set_output(output);
    unlock();
    return status;
}

/**
 * Finalizes the logging module. Frees resources specific to the current thread.
 * Frees all resources if the current thread is the one on which
 * `log_impl_init()` was called.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int log_impl_fini(void)
{
    int status;
    lock();
    log_free();
    if (!pthread_equal(init_thread, pthread_self())) {
        status = 0;
    }
    else {
        stream_close();
        closelog();
        unlock();
        status = mutex_fini(&mutex);
    }
    return status ? -1 : 0;
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
        lock();
        logging_level = level;
        unlock();
        status = 0;
    }
    return status;
}

/**
 * Lowers the logging threshold by one. Wraps at the bottom. Should be called
 * after log_init().
 */
void log_roll_level(void)
{
    lock();
    logging_level = (logging_level == LOG_LEVEL_DEBUG)
            ? LOG_LEVEL_ERROR
            : logging_level - 1;
    unlock();
}

/**
 * Returns the current logging level. Should be called after log_init().
 *
 * @return The lowest level through which logging will occur. The initial value
 *         is `LOG_LEVEL_DEBUG`.
 */
log_level_t log_get_level(void)
{
    lock();
    log_level_t level = logging_level;
    unlock();
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
        lock();
        syslog_facility = facility;
        status = set_output(output_spec);
        unlock();
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
    lock();
    int facility = syslog_facility;
    unlock();
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
        lock();
        strncpy(ident, id, sizeof(ident))[sizeof(ident)-1] = 0;
        status = set_output(output_spec);
        unlock();
    }
    return status;
}

/**
 * Modifies the logging identifier. Should be called after log_init().
 *
 * @param[in] hostId    The identifier of the remote host. Caller may free.
 * @param[in] isFeeder  Whether or not the process is sending data-products or
 *                      notifications.
 * @param[in] id        The logging identifier. Caller may free.
 * @retval    0         Success.
 * @retval    -1        Failure.
 */
int log_set_upstream_id(
        const char* const hostId,
        const bool        isFeeder)
{
    int status;
    if (hostId == NULL) {
        status = -1;
    }
    else {
        lock();
        (void)snprintf(ident, sizeof(ident), "%s(%s)", hostId,
                isFeeder ? "feed" : "noti");
        ident[sizeof(ident)-1] = 0;
        status = set_output(output_spec);
        unlock();
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
    lock();
    const char* const id = ident;
    unlock();
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
    lock();
    syslog_options = options;
    (void)set_output(output_spec);
    unlock();
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
    lock();
    const int opts = syslog_options;
    unlock();
    return opts;
}

/**
 * Sets the logging output. Should be called after `log_init()`.
 *
 * @param[in] output   The logging output. One of
 *                         ""      Log to the system logging daemon. Caller may
 *                                 free.
 *                         "-"     Log to the standard error stream. Caller may
 *                                 free.
 *                         else    Log to the file whose pathname is `output`.
 *                                 Caller may free.
 * @retval    0        Success.
 * @retval    -1       Failure.
 */
int log_set_output(
        const char* const output)
{
    lock();
    int status = set_output(output);
    unlock();
    return status;
}

/**
 * Returns the logging output. Should be called after `log_init()`.
 *
 * @return       The logging output. One of
 *                   ""      Output is to the system logging daemon.
 *                   "-"     Output is to the standard error stream.
 *                   else    The pathname of the log file.
 */
const char* log_get_output(void)
{
    lock();
    const char* path = output_spec;
    unlock();
    return path == NULL ? "" : path;
}

int slog_is_priority_enabled(
        const log_level_t level)
{
    lock();
    int enabled = log_vet_level(level) && level >= logging_level;
    unlock();
    return enabled;
}
