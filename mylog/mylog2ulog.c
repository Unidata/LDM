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

#include "mylog.h"
#include "ulog.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 *  Logging level. The initial value must be consonant with the initial value of
 *  `logMask` in `ulog.c`.
 */
static mylog_level_t loggingLevel = MYLOG_LEVEL_DEBUG;

/**
 * Initializes the logging module. Should be called before any other function.
 * - `mylog_get_output()` will return "".
 * - `mylog_get_facility()` will return `LOG_LDM`.
 * - `mylog_get_level()` will return `MYLOG_LEVEL_DEBUG`.
 *
 * @param[in] id       The logging identifier. Caller may free.
 * @retval    0        Success.
 * @retval    -1       Error.
 */
int mylog_init(
        const char* const id)
{
    const unsigned    options = mylog_get_options();
    int               status = openulog(id, options, LOG_LDM, "");
    return status == -1 ? -1 : 0;
}

/**
 * Finalizes the logging module.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int mylog_fini(void)
{
    return closeulog();
}

/**
 * Enables logging down to a given level.
 *
 * @param[in] level  The lowest level through which logging should occur.
 * @retval    0      Success.
 */
int mylog_set_level(
        const mylog_level_t level)
{
    static int ulogUpTos[MYLOG_LEVEL_COUNT] = {LOG_UPTO(LOG_DEBUG),
            LOG_UPTO(LOG_INFO), LOG_UPTO(LOG_NOTICE), LOG_UPTO(LOG_WARNING),
            LOG_UPTO(LOG_ERR)};
    (void)setulogmask(ulogUpTos[level]);
    loggingLevel = level;
    return 0;
}

/**
 * Returns the current logging level.
 *
 * @return The lowest level through which logging will occur. The initial value
 *         is `MYLOG_LEVEL_DEBUG`.
 */
mylog_level_t mylog_get_level(void)
{
    return loggingLevel;
}

/**
 * Lowers the logging threshold by one. Wraps at the bottom.
 */
void mylog_roll_level(void)
{
    mylog_level_t level = mylog_get_level();
    level = (level == MYLOG_LEVEL_DEBUG) ? MYLOG_LEVEL_ERROR : level - 1;
    mylog_set_level(level);
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
    const char* const id = mylog_get_id();
    const unsigned    options = mylog_get_options();
    const char* const output = mylog_get_output();
    int               status = openulog(id, options, facility, output);
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
    return getulogfacility();
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
int mylog_modify_id(
        const char* const hostId,
        const bool        isFeeder)
{
    char id[_POSIX_HOST_NAME_MAX + 6 + 1]; // hostname + "(type)" + 0
    (void)snprintf(id, sizeof(id), "%s(%s)", hostId,
            isFeeder ? "feed" : "noti");
    id[sizeof(id)-1] = 0;
    setulogident(id);
    return 0;
}

/**
 * Returns the logging identifier.
 *
 * @return The logging identifier. The initial value is "ulog".
 */
const char* mylog_get_id(void)
{
    return getulogident();
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
    ulog_set_options(~0u, options);
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
    return ulog_get_options();
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
    const char* const id = mylog_get_id();
    const unsigned    options = mylog_get_options();
    int               status = openulog(id, options, LOG_LDM, output);
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
    const char* path = getulogpath();
    return path == NULL ? "" : path;
}

/**
 * Logs an error message.
 *
 * @param[in] format  Format of log message in the style of `sprintf()`.
 * @param[in] ...     Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_error(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    int status = vulog(LOG_ERR, format, args);
    va_end(args);
    return status == -1 ? -1 : 0;
}

/**
 * Logs a warning message.
 *
 * @param[in] format  Format of log message in the style of `sprintf()`.
 * @param[in] ...     Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_warning(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    int status = vulog(LOG_WARNING, format, args);
    va_end(args);
    return status == -1 ? -1 : 0;
}

/**
 * Logs a notice.
 *
 * @param[in] format  Format of log message in the style of `sprintf()`.
 * @param[in] ...     Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_notice(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    int status = vulog(LOG_NOTICE, format, args);
    va_end(args);
    return status == -1 ? -1 : 0;
}

/**
 * Logs an informational message.
 *
 * @param[in] format  Format of log message in the style of `sprintf()`.
 * @param[in] ...     Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_info(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    int status = vulog(LOG_INFO, format, args);
    va_end(args);
    return status == -1 ? -1 : 0;
}

/**
 * Logs a debug message.
 *
 * @param[in] format  Format of log message in the style of `sprintf()`.
 * @param[in] ...     Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_debug(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    int status = vulog(LOG_DEBUG, format, args);
    va_end(args);
    return status == -1 ? -1 : 0;
}

/**
 * Logs a message with an argument list.
 *
 * @param[in] level   Logging level: MYLOG_LEVEL_DEBUG, MYLOG_LEVEL_INFO,
 *                    MYLOG_LEVEL_NOTICE, MYLOG_LEVEL_WARNING, or
 *                    MYLOG_LEVEL_ERROR.
 * @param[in] format  Format of the message in the style of `sprintf()`.
 * @param[in] args    List of optional arguments.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_vlog(
        const mylog_level_t level,
        const char* const   format,
        va_list             args)
{
    static int ulogPriorities[] = {LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING,
            LOG_ERR};
    int status = vulog(ulogPriorities[level], format, args);
    return status == -1 ? -1 : 0;
}
