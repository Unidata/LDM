/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mylog2ulog.c
 * @author: Steven R. Emmerson
 *
 * This file implements the `mylog.h` API using the Log4C library.
 */


#include "config.h"

#include "mylog.h"

#include <limits.h>
#include <log4c.h>
#include <stdlib.h>
#include <string.h>

#ifndef _XOPEN_NAME_MAX
    #define _XOPEN_NAME_MAX 255 // not always defined
#endif

/**
 *  Logging level. The initial value must be consonant with the initial value of
 *  `logMask` in `ulog.c`.
 */
static mylog_level_t loggingLevel = MYLOG_LEVEL_DEBUG;
/**
 * The Log4C category of the current logger:
 * <progname> [. (feeder|notifier) . <hostname>]
 */
static char catId[_XOPEN_NAME_MAX + 1 + 8 + 1 + _POSIX_HOST_NAME_MAX + 1] =
        "root";

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
    int               status;
    #define           MAX_CATEGORIES 512
    log4c_category_t* categories[MAX_CATEGORIES];
    const int         ncats = log4c_category_list(categories, MAX_CATEGORIES);
    if (ncats < 0 || ncats > MAX_CATEGORIES) {
        mylog_warning(catId, "Couldn't get all logging categories: ncats=%d",
                ncats);
        status = -1;
    }
    else {
        static int priorities[] = {LOG4C_PRIORITY_DEBUG, LOG4C_PRIORITY_INFO,
                LOG4C_PRIORITY_NOTICE, LOG4C_PRIORITY_WARN,
                LOG4C_PRIORITY_ERROR};
        int priority = priorities[level];
        for (int i = 0; i < ncats; i++)
            (void)log4c_set_priority(categories[i], priority);
        loggingLevel = level;
        status = 0;
    }
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
 * Modifies the logging identifier. Should be called after `mylog_init()`.
 *
 * @param[in] hostId    The identifier of the remote host. Caller may free.
 * @param[in] isFeeder  Whether or not the process is sending data-products or
 *                      just notifications.
 * @retval    0         Success.
 */
int mylog_modify_id(
        const char* const hostId,
        const bool        isFeeder)
{
    // progname + "." + type + "." + hostname
    (void)snprintf(catId, sizeof(catId), "%s.%s.%s", getulogident(),
            isFeeder ? "feeder" : "notifier", hostId);
    catId[sizeof(catId)-1] = 0;
    return 0;
}

/**
 * Returns the logging identifier.
 *
 * @return The logging identifier. The initial value is "ulog".
 */
const char* mylog_get_id(void)
{
    return catId;
}

/**
 * Sets the logging options.
 *
 * @param[in] options  The logging options. Ignored.
 */
void mylog_set_options(
        const unsigned options)
{
}

/**
 * Returns the logging options.
 *
 * @retval 0   Always.
 */
unsigned mylog_get_options(void)
{
    return 0;
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
 * Initializes the logging module.  The logging options will be the return value
 * of `mylog_get_options()` and the logging facility will be the value of the
 * macro `LOG_LDM` if output is to the system logging daemon.
 *
 * @param[in] id       The logging identifier. Caller may free.
 * @param[in] output   The logging output specification. One of
 *                         ""      Log according to the relevant `log4crc`
 *                                 configuration-file. Caller may free.
 *                         "-"     Log to the standard error stream. Caller may
 *                                 free.
 *                         else    Log to the file `output`. Caller may free.
 * @retval    0        Success.
 * @retval    -1       Failure.
 */
int mylog_init(
        const char* const id,
        const char* const output)
{
    int status = log4c_init();
    if (status == 0 && strcmp("", output)) {
        if (strcmp("", output) == 0) {

        }
        else if (strcmp("-", output) == 0) {

        }
    }
    return status == 0 ? 0 : -1;
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
