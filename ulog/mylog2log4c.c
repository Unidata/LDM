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
#include <log4c/appender_type_stream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _XOPEN_NAME_MAX
    #define _XOPEN_NAME_MAX 255 // not always defined
#endif
#ifndef _XOPEN_PATH_MAX
    #define _XOPEN_PATH_MAX 1024 // not always defined
#endif

/**
 * Maximum number of bytes in a category specification (includes the
 * terminating NUL).
 */
#define CATEGORY_ID_MAX (_XOPEN_NAME_MAX + 1 + 8 + 1 + _POSIX_HOST_NAME_MAX + 1)

/**
 *  Logging level. The initial value must be consonant with the initial value of
 *  `logMask` in `ulog.c`.
 */
static mylog_level_t loggingLevel = MYLOG_LEVEL_DEBUG;
/**
 * The Log4C category of the current logger.
 */
static log4c_category_t* category;
/**
 * The name of the program.
 */
static char progname[_XOPEN_NAME_MAX + 1];
/**
 * The specification of logging output.
 */
static char output[_XOPEN_PATH_MAX] = ""; // Includes terminating NUL
/**
 * The facility to be used when logging to the system logging daemon.
 */
static int facility = LOG_LDM;
/**
 * The mapping from this module's logging-levels to Log4C priorities.
 */
static int log4cPriorities[] = {LOG4C_PRIORITY_DEBUG, LOG4C_PRIORITY_NOTICE,
        LOG4C_PRIORITY_INFO, LOG4C_PRIORITY_WARN, LOG4C_PRIORITY_ERROR};

/**
 * Initializes the logging module. Should be called before any other function.
 * - `mylog_get_output()`   will return "".
 * - `mylog_get_facility()` will return `LOG_LDM`.
 * - `mylog_get_level()`    will return `MYLOG_LEVEL_DEBUG`.
 *
 * @param[in] id       The logging identifier. Caller may free.
 * @retval    0        Success.
 * @retval    -1       Error.
 */
int mylog_init(
        const char* const id)
{
    int status = log4c_init();
    if (status == 0) {
        char catId[CATEGORY_ID_MAX];
        (void)strncpy(catId, id, sizeof(catId));
        catId[sizeof(catId)-1] = 0;
        log4c_category_t* cat = log4c_category_get(catId);
        if (cat == NULL) {
            status = -1;
        }
        else {
            if (category)
                log4c_category_delete(category);
            category = cat;
            (void)strncpy(progname, id, sizeof(progname));
            progname[sizeof(progname)-1] = 0;
            (void)strcpy(output, "");
            facility = LOG_LDM;
            loggingLevel = MYLOG_LEVEL_DEBUG;
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
    if (category)
        log4c_category_delete(category);
    return 0;
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
    int               status;
    #define           MAX_CATEGORIES 512
    log4c_category_t* categories[MAX_CATEGORIES];
    const int         ncats = log4c_category_list(categories, MAX_CATEGORIES);
    if (ncats < 0 || ncats > MAX_CATEGORIES) {
        mylog_warning("Couldn't get all logging categories: ncats=%d",
                ncats);
        status = -1;
    }
    else {
        int priority = log4cPriorities[level];
        for (int i = 0; i < ncats; i++)
            (void)log4c_category_set_priority(categories[i], priority);
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
 * The identifier will become "<id>.<type>.<host>", where <id> is the identifier
 * given to `mylog_init()`, <type> is the type of upstream LDM ("feeder" or
 * "notifier"), and <host> is the identifier given to this function with all
 * periods replaced with underscores.
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
    int  status;
    char id[CATEGORY_ID_MAX];
    int  nbytes = snprintf(id, sizeof(id), "%s.%s.", progname,
            isFeeder ? "feeder" : "notifier");
    id[sizeof(id)-1] = 0;
    if (nbytes < sizeof(id)) {
        char* cp = id + nbytes;
        (void)strncpy(cp, hostId, sizeof(id)-nbytes);
        id[sizeof(id)-1] = 0;
        for (cp = strchr(cp, '.'); cp != NULL; cp = strchr(cp, '.'))
            *cp = '_';
    }
    log4c_category_t* cat = log4c_category_get(id);
    if (cat == NULL) {
        status = -1;
    }
    else {
        log4c_category_delete(category);
        category = cat;
        status = 0;
    }
    return status;
}

/**
 * Returns the logging identifier.
 *
 * @return The logging identifier. The initial value is "ulog".
 */
const char* mylog_get_id(void)
{
    return log4c_category_get_name(category);
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
 * Sets the logging output. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] out      The logging output. One of
 *                         ""      Log according to the Log4C
 *                                 configuration-file. Caller may free.
 *                         "-"     Log to the standard error stream. Caller may
 *                                 free.
 *                         else    Log to the file whose pathname is `out`.
 *                                 Caller may free.
 * @retval    0        Success.
 * @retval    -1       Failure.
 */
int mylog_set_output(
        const char* const out)
{
    int status;
    if (strcmp("", out) == 0) {
        // Log using the Log4C configuration-file
        (void)mylog_fini();
        status = mylog_init(progname);
    }
    else if (strcmp("-", out) == 0) {
        // Log to the standard error stream.
        log4c_appender_t* appender = log4c_appender_get("stderr");
        if (appender == NULL) {
            status = -1;
        }
        else {
            (void)log4c_category_set_appender(category, appender);
        }
    }
    else {
        // Log to the file `out`
        log4c_appender_t* appender = log4c_appender_get("myappender");
        if (appender == NULL) {
            status = -1;
        }
        else {
            (void)log4c_appender_set_type(appender, &log4c_appender_type_stream);
            (void)log4c_appender_set_udata(appender, freopen(out, "w", stderr));
            (void)log4c_category_set_appender(category, appender);
        }
    }
    if (status == 0) {
        log4c_category_set_priority(category, log4cPriorities[loggingLevel]);
        (void)strncpy(output, out, sizeof(output));
        output[sizeof(output)-1] = 0;
    }
    return status == 0 ? 0 : -1;
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
    return output;
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
    int priority = log4cPriorities[level];
    if (log4c_category_is_priority_enabled(category, priority)) {
        char msg[_POSIX2_LINE_MAX];
        int status = vsnprintf(msg, sizeof(msg), format, args);
        if (status < 0) {
            log4c_category_error(category, "[%s:%d] vsnprint() failure",
                    __FILE__, __LINE__);
        }
        else {
            if (status >= sizeof(msg))
                msg[sizeof(msg)-1] = 0;
            log4c_category_log(category, priority, "%s", msg);
        }
    }
}

/**
 * Logs an error message.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_error(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    mylog_vlog(MYLOG_LEVEL_ERROR, format, args);
    va_end(args);
}

/**
 * Logs a warning message.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_warning(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    mylog_vlog(MYLOG_LEVEL_WARNING, format, args);
    va_end(args);
}

/**
 * Logs a notice.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_notice(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    mylog_vlog(MYLOG_LEVEL_NOTICE, format, args);
    va_end(args);
}

/**
 * Logs an informational message.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_info(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    mylog_vlog(MYLOG_LEVEL_INFO, format, args);
    va_end(args);
}

/**
 * Logs a debug message.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_debug(
        const char* const restrict format,
        ...)
{
    va_list args;
    va_start(args, format);
    mylog_vlog(MYLOG_LEVEL_DEBUG, format, args);
    va_end(args);
}
