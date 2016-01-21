/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
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
#include "ulog.h"

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef _XOPEN_PATH_MAX
    #define _XOPEN_PATH_MAX 1024
#endif

/**
 * The mapping from `log` logging levels to `ulog` priorities:
 */
int                  log_syslog_priorities[] = {
        LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING, LOG_ERR
};

/**
 *  Logging level. The initial value must be consonant with the initial value of
 *  `logMask` in `ulog.c`.
 */
static log_level_t loggingLevel = LOG_LEVEL_DEBUG;

/**
 * The thread identifier of the thread on which `log_impl_init()` was called.
 */
static pthread_t initThread;

/**
 * The mutex that makes this module thread-safe.
 */
static mutex_t  mutex;

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
 * Enables logging down to a given level.
 *
 * @param[in] level  The lowest level through which logging should occur.
 * @retval    0      Success.
 * @retval    -1     Failure.
 */
static int set_level(
        const log_level_t level)
{
    int status;
    if (!log_vet_level(level)) {
        status = -1;
    }
    else {
        static int ulogUpTos[LOG_LEVEL_COUNT] = {LOG_UPTO(LOG_DEBUG),
                LOG_UPTO(LOG_INFO), LOG_UPTO(LOG_NOTICE), LOG_UPTO(LOG_WARNING),
                LOG_UPTO(LOG_ERR)};
        (void)setulogmask(ulogUpTos[level]);
        loggingLevel = level;
        status = 0;
    }
    return status;
}

/**
 * Returns the default destination for log messages. If the current process is a
 * daemon, then the default destination will be the system logging daemon;
 * otherwise, the default destination will be to the standard error stream.
 *
 * @retval ""   Log to the system logging daemon
 * @retval "-"  Log to the standard error stream
 */
static const char* get_default_output(void)
{
    int         status;
    const char* output;
    int         ttyFd = open("/dev/tty", O_RDONLY);
    if (-1 == ttyFd) {
        // No controlling terminal => daemon => use system logging daemon
        output = "";
    }
    else {
        // Controlling terminal exists => interactive => log to `stderr`
        (void)close(ttyFd);
        output = "-";
    }
    return output;
}

/**
 * Initializes the logging module -- overwriting any previous initialization --
 * except for the mutual-exclusion lock. Should be called before any other
 * function.
 *
 * @param[in] id        The pathname of the program (e.g., `argv[0]`). Caller
 *                      may free.
 * @param[in] options   `openulog()` options.
 * @param[in] facility  Facility to use if using the system logging daemon.
 * @param[in] output    The logging destination:
 *                        - ""  system logging daemon
 *                        - "-" standard error stream
 *                        - else file whose pathname is `output`
 * @param[in] level     Logging level.
 * @retval    0         Success.
 *                        - `log_get_destination()` will return
 *                          - ""  if the process is a daemon
 *                          - "-" otherwise
 *                        - `log_get_facility()` will return `facility`.
 *                        - `log_get_level()` will return `level`.
 * @retval    -1        Error.
 */
static int init(
        const char* restrict       id,
        const int                  options,
        const int                  facility,
        const char* const          output,
        const log_level_t        level)
{
    char progname[_XOPEN_PATH_MAX];
    strncpy(progname, id, sizeof(progname))[sizeof(progname)-1] = 0;
    id = basename(progname);
    int status = openulog(id, options, facility, output);
    if (status != -1)
        status = set_level(level);
    return status ? -1 : 0;
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
    (void)ulog(log_get_priority(level), "%s:%d %s", msg->loc.file,
            msg->loc.line, msg->string);
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
    va_list args;
    va_start(args, fmt);
    (void)vulog(log_get_priority(LOG_LEVEL_ERROR), fmt, args);
    va_end(args);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Initializes the logging module. Should be called before any other function.
 * - `log_get_destination()` will return ""
 *   - ""  if the process is a daemon
 *   - "-" otherwise
 * - `log_get_facility()` will return `LOG_LDM`.
 * - `log_get_level()` will return `LOG_LEVEL_NOTICE`.
 *
 * @param[in] id       The pathname of the program (e.g., `argv[0]`). Caller may
 *                     free.
 * @retval    0        Success.
 * @retval    -1       Error. Logging module is in an unspecified state.
 */
int log_impl_init(
        const char* id)
{
    const char* output = get_default_output();
    int status = init(id, LOG_PID, LOG_LDM, output, LOG_LEVEL_NOTICE);
    if (status == 0) {
        status = mutex_init(&mutex, true, true);
        if (status == 0)
            initThread = pthread_self();
    }
    return status ? -1 : 0;
}

/**
 * Refreshes the logging module. In particular, if logging is to a file, then
 * the file is closed and re-opened; thus allowing for log file rotation.
 *
 * @retval  0  Success.
 * @retval -1  Failure.
 */
int log_refresh(void)
{
    lock();
    int status = init(getulogident(), ulog_get_options(), getulogfacility(),
            getulogpath(), loggingLevel);
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
    lock();
    log_free();
    int status;
    if (!pthread_equal(initThread, pthread_self())) {
        status = 0;
    }
    else {
        status = closeulog();
        if (status == 0) {
            unlock();
            status = mutex_fini(&mutex);
        }
    }
    return status ? -1 : 0;
}

/**
 * Enables logging down to a given level.
 *
 * @param[in] level  The lowest level through which logging should occur.
 * @retval    0      Success.
 * @retval    -1     Failure.
 */
int log_set_level(
        const log_level_t level)
{
    lock();
    int status = set_level(level);
    unlock();
    return status;
}

/**
 * Returns the current logging level.
 *
 * @return The lowest level through which logging will occur. The initial value
 *         is `LOG_LEVEL_DEBUG`.
 */
log_level_t log_get_level(void)
{
    lock();
    log_level_t level = loggingLevel;
    unlock();
    return level;
}

/**
 * Lowers the logging threshold by one. Wraps at the bottom.
 */
void log_roll_level(void)
{
    lock();
    log_level_t level = log_get_level();
    level = (level == LOG_LEVEL_DEBUG) ? LOG_LEVEL_ERROR : level - 1;
    log_set_level(level);
    unlock();
}

/**
 * Sets the facility that will be used (e.g., `LOG_LOCAL0`) when logging to the
 * system logging daemon. Should be called after `log_impl_init()`.
 *
 * @param[in] facility  The facility that will be used when logging to the
 *                      system logging daemon.
 */
int log_set_facility(
        const int facility)
{
    lock();
    const char* const id = log_get_id();
    const unsigned    options = log_get_options();
    const char* const output = log_get_destination();
    int               status = openulog(id, options, facility, output);
    unlock();
    return status == -1 ? -1 : 0;
}

/**
 * Returns the facility that will be used when logging to the system logging
 * daemon (e.g., `LOG_LOCAL0`).
 *
 * @return The facility that will be used when logging to the system logging
 *         daemon (e.g., `LOG_LOCAL0`).
 */
int log_get_facility(void)
{
    lock();
    int facility = getulogfacility();
    unlock();
    return facility;
}

/**
 * Sets the logging identifier. Should be called between `log_impl_init()` and
 * `log_impl_fini()`.
 *
 * @param[in] id        The new identifier. Caller may free.
 * @retval    0         Success.
 * @retval    -1        Failure.
 */
int log_set_id(
        const char* const id)
{
    lock();
    setulogident(id);
    unlock();
    return 0;
}

/**
 * Modifies the logging identifier.
 *
 * @param[in] hostId    The identifier of the remote host. Caller may free.
 * @param[in] isFeeder  Whether or not the process is sending data-products or
 *                      notifications.
 * @param[in] id        The logging identifier. Caller may free.
 * @retval    0         Success.
 */
int log_set_upstream_id(
        const char* const hostId,
        const bool        isFeeder)
{
    lock();
    char id[_POSIX_HOST_NAME_MAX + 6 + 1]; // hostname + "(type)" + 0
    (void)snprintf(id, sizeof(id), "%s(%s)", hostId,
            isFeeder ? "feed" : "noti");
    id[sizeof(id)-1] = 0;
    setulogident(id);
    unlock();
    return 0;
}

/**
 * Returns the logging identifier.
 *
 * @return The logging identifier. The initial value is "ulog".
 */
const char* log_get_id(void)
{
    lock();
    const char* const id = getulogident();
    unlock();
    return id;
}

/**
 * Sets the logging options.
 *
 * @param[in] options  The logging options. Bitwise or of
 *                         LOG_NOTIME      Don't add timestamp
 *                         LOG_PID         Add process-identifier.
 *                         LOG_IDENT       Add logging identifier.
 *                         LOG_MICROSEC    Use microsecond resolution.
 *                         LOG_ISO_8601    Use timestamp format
 *                             <YYYY><MM><DD>T<hh><mm><ss>[.<uuuuuu>]<zone>
 */
void log_set_options(
        const unsigned options)
{
    lock();
    ulog_set_options(~0u, options);
    unlock();
}

/**
 * Returns the logging options.
 *
 * @return The logging options. Bitwise or of
 *             LOG_NOTIME      Don't add timestamp
 *             LOG_PID         Add process-identifier.
 *             LOG_IDENT       Add logging identifier.
 *             LOG_MICROSEC    Use microsecond resolution.
 *             LOG_ISO_8601    Use timestamp format
 *                             <YYYY><MM><DD>T<hh><mm><ss>[.<uuuuuu>]<zone>
 *         The initial value is `0`.
 */
unsigned log_get_options(void)
{
    lock();
    const unsigned opts = ulog_get_options();
    unlock();
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
    lock();
    const char* const id = log_get_id();
    const unsigned    options = log_get_options();
    int               status = openulog(id, options, LOG_LDM, dest);
    unlock();
    return status == -1 ? -1 : 0;
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
    lock();
    const char* path = getulogpath();
    unlock();
    return path == NULL ? "" : path;
}
