/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mylog.h
 * @author: Steven R. Emmerson
 *
 * This file defines the API of the logging system.
 */

#ifndef ULOG_MYLOG_H_
#define ULOG_MYLOG_H_

#include <stdarg.h>

#ifndef MYLOG_LOGGER_ID
    #define MYLOG_LOGGER_ID "root"
#endif

typedef enum {
    MYLOG_LEVEL_DEBUG = 0,
    MYLOG_LEVEL_INFO,
    MYLOG_LEVEL_NOTICE,
    MYLOG_LEVEL_WARNING,
    MYLOG_LEVEL_ERROR,
    MYLOG_LEVEL_COUNT
} mylog_level_t;

#define MYLOG_STRINGIFY(x)  #x
#define MYLOG_QUOTE(x)      MYLOG_STRINGIFY(x)
#define MYLOG_FORMAT_PREFIX "[" __FILE__  ":" MYLOG_QUOTE(__LINE__) "] "
#define MYLOG_ERROR(...)    mylog_error(MYLOG_LOGGER_ID, MYLOG_FORMAT_PREFIX \
    __VA_ARGS__)
#define MYLOG_WARNING(...)  mylog_warning(MYLOG_LOGGER_ID, MYLOG_FORMAT_PREFIX \
    __VA_ARGS__)
#define MYLOG_NOTICE(...)   mylog_notice(MYLOG_LOGGER_ID, MYLOG_FORMAT_PREFIX \
    __VA_ARGS__)
#define MYLOG_INFO(...)     mylog_info(MYLOG_LOGGER_ID, MYLOG_FORMAT_PREFIX \
    __VA_ARGS__)
#define MYLOG_DEBUG(...)    mylog_debug(MYLOG_LOGGER_ID, MYLOG_FORMAT_PREFIX \
    __VA_ARGS__)

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Enables logging down to a given level. May be called at any time -- including
 * before `mylog_init()`.
 *
 * @param[in] level  The lowest level through which logging should occur. The
 *                   levels are ordered: MYLOG_LEVEL_ERROR > MYLOG_LEVEL_WARNING
 *                   > MYLOG_LEVEL_NOTICE > MYLOG_LEVEL_INFO >
 *                   MYLOG_LEVEL_DEBUG.
 */
void mylog_set_level(
        const mylog_level_t level);

/**
 * Returns the current logging level. May be called at any time -- including
 * before `mylog_init()`.
 *
 * @return The lowest level through which logging will occur. The levels are
 *         ordered: MYLOG_LEVEL_ERROR > MYLOG_LEVEL_WARNING > MYLOG_LEVEL_NOTICE
 *         > MYLOG_LEVEL_INFO > MYLOG_LEVEL_DEBUG.
 */
mylog_level_t mylog_get_level(void);

/**
 * Lowers the logging threshold by one. Wraps at the bottom.
 */
void mylog_roll_level(void);

/**
 * Sets the logging identifier. May be called at any time -- including before
 * `mylog_init()`.
 *
 * @param[in] id     The logging identifier. Caller may free.
 * @retval    0      Success.
 */
int mylog_set_id(
        const char* const id);

/**
 * Returns the logging identifier. May be called at any time -- including before
 * `mylog_init()`.
 *
 * @return The logging identifier.
 */
const char* mylog_get_id(void);

/**
 * Sets the logging options. May be called at any time -- including before
 * `mylog_init()`.
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
        const unsigned options);

/**
 * Returns the logging options. May be called at any time -- including before
 * `mylog_init()`.
 *
 * @return The logging options. Bitwise or of
 *                         LOG_NOTIME      Don't add timestamp
 *                         LOG_PID         Add process-identifier.
 *                         LOG_IDENT       Add logging identifier.
 *                         LOG_MICROSEC    Use microsecond resolution.
 *                         LOG_ISO_8601    Use timestamp format
 *                             <YYYY><MM><DD>T<hh><mm><ss>[.<uuuuuu>]<zone>
 */
unsigned mylog_get_options(void);

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
        const char* const output);

/**
 * Returns the logging output. May be called at any time -- including before
 * `mylog_init()`.
 *
 * @return       The logging output. One of
 *                   ""      Output is to the system logging daemon.
 *                   "-"     Output is to the standard error stream.
 *                   else    The pathname of the log file.
 */
const char* mylog_get_output(void);

/**
 * Initializes the logging module. Should be called before any of the functions
 * that perform logging (e.g., `mylog_error()`).
 *
 * @retval    0        Success
 * @retval    -1       Error.
 */
int mylog_init(void);

/**
 * Finalizes the logging module.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int mylog_fini(void);

/**
 * Logs an error message. Should be called after `mylog_init()`.
 *
 * @param[in] id        Identifier. Use is implementation-defined.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_error(
        const char* const id,
        const char* const format,
        ...);

/**
 * Logs a warning message. Should be called after `mylog_init()`.
 *
 * @param[in] id        Identifier. Use is implementation-defined.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_warning(
        const char* const id,
        const char* const format,
        ...);

/**
 * Logs a notice. Should be called after `mylog_init()`.
 *
 * @param[in] id        Identifier. Use is implementation-defined.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_notice(
        const char* const id,
        const char* const format,
        ...);

/**
 * Logs an informational message. Should be called after `mylog_init()`.
 *
 * @param[in] id        Identifier. Use is implementation-defined.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_info(
        const char* const id,
        const char* const format,
        ...);

/**
 * Logs a debug message. Should be called after `mylog_init()`.
 *
 * @param[in] id        Identifier. Use is implementation-defined.
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 */
void mylog_debug(
        const char* const id,
        const char* const format,
        ...);

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
        va_list             args);

#ifdef __cplusplus
    }
#endif

#endif /* ULOG_MYLOG_H_ */
