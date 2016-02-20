/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: log2ulog.c
 * @author: Steven R. Emmerson
 *
 * This file interfaces the `log.h` API to the `ulog.c` module.
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

/******************************************************************************
 * Private API:
 ******************************************************************************/

/******************************************************************************
 * Package-Private Implementation API:
 ******************************************************************************/

/**
 * Initializes the logging module. Should be called before any other function.
 * - `log_get_destination()` will return
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
int logi_init(
        const char* const id)
{
    const char* const progname = logl_basename(id);
    int status = openulog(progname, LOG_PID, LOG_LDM, log_dest);
    if (status != -1)
        status = log_set_level(LOG_LEVEL_NOTICE);
    return status ? -1 : 0;
}

/**
 * Finalizes the logging module.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int logi_fini(void)
{
    int status = closeulog();
    return status ? -1 : 0;
}

/**
 * Re-initializes the logging module based on its state just prior to calling
 * logi_fini(). If logi_fini(), wasn't called, then the result is
 * unspecified.
 *
 * @retval    0        Success.
 */
int logi_reinit(void)
{
    const char* const id = getulogident();
    const unsigned    options = ulog_get_options();
    const int         facility = getulogfacility();
    int               status = openulog(id, options, facility, log_dest);
    return status < 0 ? -1 : 0;
}

/**
 * Sets the logging destination. Should be called between log_init() and
 * log_fini().
 */
int logi_set_destination(void)
{
    const char* const id = getulogident();
    const unsigned    options = ulog_get_options();
    int               status = openulog(id, options, LOG_LDM, log_dest);
    return status == -1 ? -1 : 0;
}

/**
 * Enables logging down to a given level.
 *
 * @pre              `log_level` is valid
 * @param[in] level  The lowest level through which logging should occur.
 * @retval    0      Success.
 * @retval    -1     Failure.
 */
void logi_set_level(void)
{
    static int ulogUpTos[LOG_LEVEL_COUNT] = {LOG_UPTO(LOG_DEBUG),
            LOG_UPTO(LOG_INFO), LOG_UPTO(LOG_NOTICE), LOG_UPTO(LOG_WARNING),
            LOG_UPTO(LOG_ERR)};
    (void)setulogmask(ulogUpTos[log_level]);
}

/**
 * Sets the logging identifier. Should be called between `logi_init()` and
 * `logi_fini()`.
 *
 * @pre                 `id` isn't NULL
 * @param[in] id        The new identifier. Caller may free.
 * @retval    0         Success (always).
 */
int logi_set_id(
        const char* const id)
{
    setulogident(id);
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
        const log_level_t level,
        const log_loc_t*  loc,
        const char*       string)
{
    (void)ulog(logl_level_to_priority(level), "%s:%s():%d %s",
            logl_basename(loc->file), loc->func, loc->line, string);
}

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] level  Logging level.
 * @param[in] loc    The location where the message was generated. Unused.
 * @param[in] ...    Format and format arguments.
 */
void logi_internal(
        const log_level_t level,
        const log_loc_t*  loc,
                          ...)
{
    va_list args;
    va_start(args, loc);
    const char* const fmt = va_arg(args, const char*);
    (void)vulog(logl_level_to_priority(level), fmt, args);
    va_end(args);
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
    return "";
}

/**
 * Sets the facility that will be used (e.g., `LOG_LOCAL0`) when logging to the
 * system logging daemon. Should be called after `logi_init()`.
 *
 * @param[in] facility  The facility that will be used when logging to the
 *                      system logging daemon.
 */
int log_set_facility(
        const int facility)
{
    logl_lock();
    const char* const id = getulogident();
    const unsigned    options = ulog_get_options();
    int               status = openulog(id, options, facility, log_dest);
    logl_unlock();
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
    logl_lock();
    int facility = getulogfacility();
    logl_unlock();
    return facility;
}

/**
 * Returns the logging identifier.
 *
 * @return The logging identifier. The initial value is "ulog".
 */
const char* log_get_id(void)
{
    logl_lock();
    const char* const id = getulogident();
    logl_unlock();
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
    logl_lock();
    ulog_set_options(~0u, options);
    logl_unlock();
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
    logl_lock();
    const unsigned opts = ulog_get_options();
    logl_unlock();
    return opts;
}
