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

#include <stdlib.h>
#include <string.h>

/// Logging level. Keep consonant with initial value of `logMask` in `ulog.c`.
static mylog_level_t loggingLevel = MYLOG_LEVEL_DEBUG;

/**
 * Enables logging down to a given level.
 *
 * @param[in] level  The lowest level through which logging should occur.
 */
void mylog_set_level(
        const mylog_level_t level)
{
    static int ulogUpTos[MYLOG_LEVEL_COUNT] = {LOG_UPTO(LOG_DEBUG),
            LOG_UPTO(LOG_INFO), LOG_UPTO(LOG_NOTICE), LOG_UPTO(LOG_WARNING),
            LOG_UPTO(LOG_ERR)};
    (void)setulogmask(ulogUpTos[level]);
    loggingLevel = level;
}

/**
 * Returns the current logging level.
 *
 * @return The lowest level through which logging will occur. The default is
 *         `MYLOG_LEVEL_DEBUG`.
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
 * Sets the logging identifier.
 *
 * @param[in] id     The logging identifier. Caller may free.
 * @retval    0      Success.
 */
int mylog_set_id(
        const char* const id)
{
    setulogident(id);
    return 0;
}

/**
 * Returns the logging identifier.
 *
 * @return The logging identifier. The default is "ulog".
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
 *         The default is `0`.
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
 *                   ""      Output is to the system logging daemon. Default.
 *                   "-"     Output is to the standard error stream.
 *                   else    The pathname of the log file.
 */
const char* mylog_get_output(void)
{
    const char* path = getulogpath();
    return path == NULL ? "" : path;
}

/**
 * Initializes the logging module. The logging identifier will be the return
 * value of `mylog_get_id()`, the logging options will be the return value of
 * `mylog_get_options()`, logging output will be to the return value of
 * `mylog_get_output()`, and the logging facility will be the value of the macro
 * `LOG_LDM` if output is to the system logging daemon.
 *
 * @retval    0        Success.
 * @retval    -1       Failure.
 */
int mylog_init(void)
{
    const char* const id = mylog_get_id();
    const unsigned    options = mylog_get_options();
    const char* const output = mylog_get_output();
    int               status = openulog(id, options, LOG_LDM, output);
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
 * Logs an error message.
 *
 * @param[in] id        Identifier. Ignored.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_error(
        const char* const restrict id,
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    (void)vulog(LOG_ERR, format, args);
    va_end(args);
}

/**
 * Logs a warning message.
 *
 * @param[in] id        Identifier. Ignored.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_warning(
        const char* const restrict id,
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    (void)vulog(LOG_WARNING, format, args);
    va_end(args);
}

/**
 * Logs a notice.
 *
 * @param[in] id        Identifier. Ignored.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_notice(
        const char* const restrict id,
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    (void)vulog(LOG_NOTICE, format, args);
    va_end(args);
}

/**
 * Logs an informational message.
 *
 * @param[in] id        Identifier. Ignored.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_info(
        const char* const restrict id,
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    (void)vulog(LOG_INFO, format, args);
    va_end(args);
}

/**
 * Logs a debug message.
 *
 * @param[in] id        Identifier. Ignored.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_debug(
        const char* const restrict id,
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    (void)vulog(LOG_DEBUG, format, args);
    va_end(args);
}

/**
 * Logs a message with an argument list.
 *
 * @param[in] level   Logging level: MYLOG_LEVEL_DEBUG, MYLOG_LEVEL_INFO,
 *                    MYLOG_LEVEL_NOTICE, MYLOG_LEVEL_WARNING, or
 *                    MYLOG_LEVEL_ERROR.
 * @param[in] format  Format of the message in the style of `sprintf()`.
 * @param[in] args    List of optional arguments.
 */
void mylog_vlog(
        const mylog_level_t level,
        const char* const   format,
        va_list             args)
{
    static ulogPriorities[] = {LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING,
            LOG_ERR};
    vulog(ulogPriorities[level], format, args);
}
