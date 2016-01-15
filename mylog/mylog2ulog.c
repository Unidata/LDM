/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mylog2ulog.c
 * @author: Steven R. Emmerson
 *
 * This file implements the `mylog.h` API using `ulog.c`.
 */


#include "config.h"

#include "mutex.h"
#include "mylog.h"
#include "ulog.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _XOPEN_PATH_MAX
    #define _XOPEN_PATH_MAX 1024
#endif

/**
 * The mapping from `mylog` logging levels to `ulog` priorities:
 */
int                  mylog_syslog_priorities[] = {
        LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING, LOG_ERR
};

/**
 *  Logging level. The initial value must be consonant with the initial value of
 *  `logMask` in `ulog.c`.
 */
static mylog_level_t loggingLevel = MYLOG_LEVEL_DEBUG;

/**
 * The thread identifier of the thread on which `mylog_init()` was called.
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
int set_level(
        const mylog_level_t level)
{
    int status;
    if (!mylog_vet_level(level)) {
        status = -1;
    }
    else {
        static int ulogUpTos[MYLOG_LEVEL_COUNT] = {LOG_UPTO(LOG_DEBUG),
                LOG_UPTO(LOG_INFO), LOG_UPTO(LOG_NOTICE), LOG_UPTO(LOG_WARNING),
                LOG_UPTO(LOG_ERR)};
        (void)setulogmask(ulogUpTos[level]);
        loggingLevel = level;
    }
    return status;
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
 * @param[in] output    Output:
 *                        ""      Output is to the system logging daemon.
 *                        "-"     Output is to the standard error stream.
 *                        else    The pathname of the log file.
 * @param[in] level    Logging level.
 * @retval    0        Success.
 *                       - `mylog_get_output()` will return `output`.
 *                       - `mylog_get_facility()` will return `facility`.
 *                       - `mylog_get_level()` will return `level`.
 * @retval    -1       Error.
 */
int init(
        const char* restrict       id,
        const int                  options,
        const int                  facility,
        const char* const restrict output,
        const mylog_level_t        level)
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
void mylog_write_one(
        const mylog_level_t    level,
        const Message* const   msg)
{
    (void)ulog(mylog_get_priority(level), "%s:%d %s", msg->loc.file,
            msg->loc.line, msg->string);
}

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] fmt  Format of the message.
 * @param[in] ...  Format arguments.
 */
void mylog_internal(
        const char* const fmt,
                          ...)
{
    va_list args;
    va_start(args, fmt);
    (void)vulog(mylog_get_priority(MYLOG_LEVEL_ERROR), fmt, args);
    va_end(args);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Initializes the logging module. Should be called before any other function.
 * - `mylog_get_output()` will return "".
 * - `mylog_get_facility()` will return `LOG_LDM`.
 * - `mylog_get_level()` will return `MYLOG_LEVEL_NOTICE`.
 *
 * @param[in] id       The pathname of the program (e.g., `argv[0]`). Caller may
 *                     free.
 * @retval    0        Success.
 * @retval    -1       Error. Logging module is in an unspecified state.
 */
int mylog_init(
        const char* id)
{
    int status = init(id, LOG_PID, LOG_LDM, "", MYLOG_LEVEL_NOTICE);
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
int mylog_refresh(void)
{
    lock();
    int status = init(getulogident(), ulog_get_options(), getulogfacility(),
            getulogpath(), loggingLevel);
    unlock();
    return status;
}

/**
 * Finalizes the logging module. Frees resources specific to the current thread.
 * Frees all resources if the current thread is the one on which `mylog_init()`
 * was called.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int mylog_fini(void)
{
    lock();
    mylog_free();
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
int mylog_set_level(
        const mylog_level_t level)
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
 *         is `MYLOG_LEVEL_DEBUG`.
 */
mylog_level_t mylog_get_level(void)
{
    lock();
    mylog_level_t level = loggingLevel;
    unlock();
    return level;
}

/**
 * Lowers the logging threshold by one. Wraps at the bottom.
 */
void mylog_roll_level(void)
{
    lock();
    mylog_level_t level = mylog_get_level();
    level = (level == MYLOG_LEVEL_DEBUG) ? MYLOG_LEVEL_ERROR : level - 1;
    mylog_set_level(level);
    unlock();
}

/**
 * Sets the facility that will be used (e.g., `LOG_LOCAL0`) when logging to the
 * system logging daemon. Should be called after `mylog_init()`.
 *
 * @param[in] facility  The facility that will be used when logging to the
 *                      system logging daemon.
 */
int mylog_set_facility(
        const int facility)
{
    lock();
    const char* const id = mylog_get_id();
    const unsigned    options = mylog_get_options();
    const char* const output = mylog_get_output();
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
int mylog_get_facility(void)
{
    lock();
    int facility = getulogfacility();
    unlock();
    return facility;
}

/**
 * Sets the logging identifier. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] id        The new identifier. Caller may free.
 * @retval    0         Success.
 * @retval    -1        Failure.
 */
int mylog_set_id(
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
int mylog_set_upstream_id(
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
const char* mylog_get_id(void)
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
void mylog_set_options(
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
unsigned mylog_get_options(void)
{
    lock();
    const unsigned opts = ulog_get_options();
    unlock();
    return opts;
}

/**
 * Sets the logging output. May be called at any time -- including before
 * `mylog_init()`.
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
int mylog_set_output(
        const char* const output)
{
    lock();
    const char* const id = mylog_get_id();
    const unsigned    options = mylog_get_options();
    int               status = openulog(id, options, LOG_LDM, output);
    unlock();
    return status == -1 ? -1 : 0;
}

/**
 * Returns the logging output. May be called at any time -- including before
 * `mylog_init()`.
 *
 * @return       The logging output. One of
 *                   ""      Output is to the system logging daemon. Initial
 *                           value.
 *                   "-"     Output is to the standard error stream.
 *                   else    The pathname of the log file.
 */
const char* mylog_get_output(void)
{
    lock();
    const char* path = getulogpath();
    unlock();
    return path == NULL ? "" : path;
}
