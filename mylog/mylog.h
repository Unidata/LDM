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
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>

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

#define MYLOG_ERROR(...)    mylog_error  (MYLOG_FORMAT_PREFIX __VA_ARGS__)
#define MYLOG_WARNING(...)  mylog_warning(MYLOG_FORMAT_PREFIX __VA_ARGS__)
#define MYLOG_NOTICE(...)   mylog_notice (MYLOG_FORMAT_PREFIX __VA_ARGS__)
#define MYLOG_INFO(...)     mylog_info   (MYLOG_FORMAT_PREFIX __VA_ARGS__)
#define MYLOG_DEBUG(...)    mylog_debug  (MYLOG_FORMAT_PREFIX __VA_ARGS__)

#define MYLOG_ASSERT(x)     mylog_assert((x), #x)

#ifdef __cplusplus
    extern "C" {
#endif

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
        const char* const id);

/**
 * Finalizes the logging module. Should be called eventually after
 * `mylog_init()`.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int mylog_fini(void);

/**
 * Enables logging down to a given level. Should be called between
 * `mylog_init()` and `mylog_fini()`.
 *
 * @param[in] level  The lowest level through which logging should occur. The
 *                   levels are ordered: MYLOG_LEVEL_ERROR > MYLOG_LEVEL_WARNING
 *                   > MYLOG_LEVEL_NOTICE > MYLOG_LEVEL_INFO >
 *                   MYLOG_LEVEL_DEBUG.
 * @retval    0      Success.
 * @retval    -1     Failure.
 */
int mylog_set_level(
        const mylog_level_t level);

/**
 * Returns the current logging level. Should be called between `mylog_init()`
 * and `mylog_fini()`.
 *
 * @return The lowest level through which logging will occur. The levels are
 *         ordered: MYLOG_LEVEL_ERROR > MYLOG_LEVEL_WARNING > MYLOG_LEVEL_NOTICE
 *         > MYLOG_LEVEL_INFO > MYLOG_LEVEL_DEBUG.
 */
mylog_level_t mylog_get_level(void);

/**
 * Lowers the logging threshold by one. Wraps at the bottom. Should be called
 * between `mylog_init()` and `mylog_fini()`.
 */
void mylog_roll_level(void);

/**
 * Modifies the logging identifier. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] hostId    The identifier of the remote host. Caller may free.
 * @param[in] isFeeder  Whether or not the process is sending data-products or
 *                      just notifications.
 * @retval    0         Success.
 */
int mylog_modify_id(
        const char* const hostId,
        const bool        isFeeder);

/**
 * Returns the logging identifier. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @return The logging identifier.
 */
const char* mylog_get_id(void);

/**
 * Sets the implementation-defined logging options. Should be called between
 * `mylog_init()` and `mylog_fini()`.
 *
 * @param[in] options  The implementation-defined logging options.
 */
void mylog_set_options(
        const unsigned options);

/**
 * Returns the implementation-defined logging options. Should be called between
 * `mylog_init()` and `mylog_fini()`.
 *
 * @return The implementation-defined logging options.
 */
unsigned mylog_get_options(void);

/**
 * Sets the facility that might be used (e.g., `LOG_LOCAL0`) when logging to the
 * system logging daemon. Should be called between `mylog_init()` and
 * `mylog_fini()`. May do nothing.
 *
 * @param[in] facility  The facility that might be used when logging to the
 *                      system logging daemon.
 * @retval    0         Success.
 * @retval    -1        Error.
 */
int mylog_set_facility(
        const int facility);

/**
 * Returns the facility that might be used (e.g., `LOG_LOCAL0`) when logging to
 * the system logging daemon. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] facility  The facility that might be used when logging to the
 *                      system logging daemon.
 */
int mylog_get_facility(void);

/**
 * Sets the logging output. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] output   The logging output. One of
 *                         ""      Log according to the implementation-defined
 *                                 default. Caller may free.
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
 * Returns the logging output. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @return       The logging output. One of
 *                   ""      Output is to the system logging daemon.
 *                   "-"     Output is to the standard error stream.
 *                   else    The pathname of the log file.
 */
const char* mylog_get_output(void);

/**
 * Logs an error message. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_error(
        const char* const format,
        ...);

/**
 * Logs a warning message. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_warning(
        const char* const format,
        ...);

/**
 * Logs a notice. Should be called between `mylog_init()` and `mylog_fini()`.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_notice(
        const char* const format,
        ...);

/**
 * Logs an informational message. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_info(
        const char* const format,
        ...);

/**
 * Logs a debug message. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] format    Format of log message in the style of `sprintf()`.
 * @param[in] ...       Optional arguments of log message.
 * @retval    0       Success.
 * @retval    -1      Failure.
 */
int mylog_debug(
        const char* const format,
        ...);

/**
 * Logs a message with an argument list. Should be called between `mylog_init()`
 * and `mylog_fini()`.
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
        va_list             args);

/**
 * Aborts the process if an expression isn't true. The macro `MYLOG_ASSERT()`
 * should be used instead of this function.
 *
 * @param[in] expr  The expression.
 * @param[in] str   Formatted representation of the expression.
 */
static inline void mylog_assert(
        const bool        expr,
        const char* const str)
{
    if (!expr) {
        MYLOG_ERROR("Assertion failure: %s", str);
        abort();
    }
}

#ifdef __cplusplus
    }
#endif

#endif /* ULOG_MYLOG_H_ */
