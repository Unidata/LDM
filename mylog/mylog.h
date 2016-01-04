/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file mylog.h
 * @author Steven R. Emmerson
 *
 * This file defines the API for LDM logging.
 *
 */

#ifndef ULOG_MYLOG_H_
#define ULOG_MYLOG_H_

#include "config.h"

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>

/// Use localtime. default is GMT
#define MYLOG_LOCALTIME 0x100u
/// Don't put on the timestamp
#define MYLOG_NOTIME    0x200u
/// Add the facility identifier
#define MYLOG_IDENT     0x400u
/// Use ISO 8601 standard timestamp
#define MYLOG_ISO_8601  0x800u
/// Use microsecond-resolution timestamp
#define MYLOG_MICROSEC  0x1000u

/// Logging levels
typedef enum {
    MYLOG_LEVEL_DEBUG,    ///< Debug messages
    MYLOG_LEVEL_INFO,     ///< Informational messages
    MYLOG_LEVEL_NOTICE,   ///< Notices
    MYLOG_LEVEL_WARNING,  ///< Warnings
    MYLOG_LEVEL_ERROR,    ///< Error messages
    MYLOG_LEVEL_ALERT,    ///< Unused
    MYLOG_LEVEL_CRIT,     ///< Unused
    MYLOG_LEVEL_EMERG,    ///< Unused
    MYLOG_LEVEL_COUNT     ///< Number of levels
} mylog_level_t;

#include "mylog_internal.h"

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Initializes the logging module. Should be called before any other function.
 * <dl>
 *     <dt>`mylog_get_output()` <dd>will return "".
 *     <dt>`mylog_get_facility()` <dd>will return `LOG_LDM`.
 *     <dt>`mylog_get_level()` <dd>will return `MYLOG_LEVEL_NOTICE`.
 * </dl>
 *
 * @param[in] id       The pathname of the program (e.g., `argv[0]`). Caller may
 *                     free.
 * @retval    0        Success.
 * @retval    -1       Error.
 */
int mylog_init(
        const char* const id);

/**
 * Finalizes the logging module. Should be called eventually after
 * `mylog_init()`, after which no more logging should occur.
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
 *                   levels are ordered: MYLOG_LEVEL_DEBUG < MYLOG_LEVEL_INFO <
 *                   MYLOG_LEVEL_NOTICE  < MYLOG_LEVEL_WARNING <
 *                   MYLOG_LEVEL_ERROR.
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
 * Sets the logging identifier. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] id        The new identifier. Caller may free.
 * @retval    0         Success.
 * @retval    -1        Failure.
 */
int mylog_set_id(
        const char* const id);

/**
 * Modifies the logging identifier. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] hostId    The identifier of the remote host. Caller may free.
 * @param[in] isFeeder  Whether or not the process is sending data-products or
 *                      just notifications.
 * @retval    0         Success.
 */
int mylog_set_upstream_id(
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
 * @param[in] facility  The facility that will be used when logging to the
 *                      system logging daemon.
 * @retval    0         Success.
 * @retval    -1        Error.
 */
int mylog_set_facility(
        const int facility);

/**
 * Returns the facility that will be used (e.g., `LOG_LOCAL0`) when logging to
 * the system logging daemon. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 */
int mylog_get_facility(void);

/**
 * Sets the logging output. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @param[in] output   The logging output. Caller may free. One of <dl>
 *                         <dt>""   <dd>Log according to the implementation-defined
 *                                  default.
 *                         <dt>"-"  <dd>Log to the standard error stream.
 *                         <dt>else <dd>Log to the file whose pathname is `output`.
 *                     </dl>
 * @retval    0        Success.
 * @retval    -1       Failure.
 */
int mylog_set_output(
        const char* const output);

/**
 * Returns the logging output. Should be called between `mylog_init()` and
 * `mylog_fini()`.
 *
 * @return       The logging output. One of <dl>
 *                   <dt>""      <dd>Output is to the system logging daemon.
 *                   <dt>"-"     <dd>Output is to the standard error stream.
 *                   <dt>else    <dd>The pathname of the log file.
 *               </dl>
 */
const char* mylog_get_output(void);

/**
 * Clears the message-list of the current thread.
 */
void mylog_clear(void);

/**
 * Frees the log-message resources of the current thread. Should only be called
 * when no more logging by the current thread will occur.
 */
void mylog_free(void);

/**
 * Indicates if a log message of WARNING level will be written. Useful if a
 * format argument of a message is expensive to evaluate.
 */
#define mylog_is_enabled_warning  mylog_is_level_enabled(MYLOG_LEVEL_WARNING)
/**
 * Indicates if a log message of NOTICE level will be written. Useful if a
 * format argument of a message is expensive to evaluate.
 */
#define mylog_is_enabled_notice   mylog_is_level_enabled(MYLOG_LEVEL_NOTICE)
/**
 * Indicates if a log message of INFO level will be written. Useful if a
 * format argument of a message is expensive to evaluate.
 */
#define mylog_is_enabled_info     mylog_is_level_enabled(MYLOG_LEVEL_INFO)
/**
 * Indicates if a log message of DEBUG level will be written. Useful if a
 * format argument of a message is expensive to evaluate.
 */
#define mylog_is_enabled_debug    mylog_is_level_enabled(MYLOG_LEVEL_DEBUG)

/**
 * Adds a message to the current thread's list of error messages:
 *
 * @param[in] ...  Optional arguments of the message -- starting with the format
 *                 of the message.
 */
#define mylog_add(...) do { \
    MYLOG_LOC_DECL(loc); \
    mylog_add_located(&loc, __VA_ARGS__);\
} while (false)

/**
 * Adds a variadic message to the current thread's list of error messages:
 *
 * @param[in] fmt  The format of the message.
 * @param[in] args The `va_list` arguments of the message.
 */
#define mylog_vadd(fmt, args) do { \
    MYLOG_LOC_DECL(loc); \
    mylog_vadd_located(&loc, fmt, args); \
} while (false)

/**
 * Adds a message based on a system error number (e.g., `errno`) to the current
 * thread's list of error messages:
 *
 * @param[in] n    The system error number (e.g., `errno`).
 * @param[in] ...  Optional arguments of the message -- starting with the format
 *                 of the message.
 */
#define mylog_add_errno(n, ...) do {\
    MYLOG_LOC_DECL(loc); \
    mylog_add_errno_located(&loc, n, __VA_ARGS__); \
} while (false)

/**
 * Adds a message based on the system error code (i.e., `errno`) to the current
 * thread's list of error messages:
 *
 * @param[in] ...  Optional arguments of the message.
 */
#define mylog_add_syserr(...)  mylog_add_errno(errno, __VA_ARGS__)

/**
 * The following macros add a message to the current thread's list of error
 * messages, emit the list, and then clear the list:
 *
 * The argument-list of the variadic macros must comprise at least a
 * `NULL` argument in order to avoid a syntax error.
 */

/**
 * Adds a variadic message to the message-list of the current thread, writes the
 * list at a given level, and then clears the list.
 *
 * @param[in] level  The level at which to write the list.
 * @param[in] fmt    Format of the message.
 * @param[in] args   The `va_list` list of arguments for the format.
 */
#define mylog_vlog(level, fmt, args) do { \
    MYLOG_LOC_DECL(loc); \
    mylog_vlog_located(&loc, level, fmt, args); \
} while (false)

/**
 * Adds a message to the current thread's list of messages based on a system
 * error number (e.g., `errno`), writes the list at the error level, and then
 * clears the list.
 *
 * @param[in] n    The system error number (e.g., `errno`).
 * @param[in] ...  Optional arguments of the message -- starting with the format
 *                 of the message.
 */
#define mylog_errno(n, ...) do {\
    MYLOG_LOC_DECL(loc); \
    mylog_errno_located(&loc, n, __VA_ARGS__); \
} while (false)

/**
 * Adds a message to the current thread's list of messages based on the system
 * error number (i.e., `errno`), writes the list at the error level, and then
 * clears the list.
 *
 * @param[in] ...  Optional arguments of the message -- starting with the format
 *                 of the message.
 */
#define mylog_syserr(...)      mylog_errno(errno, __VA_ARGS__)

/**
 * Adds a message to the current thread's list of messages, writes the list at
 * the ERROR level, and then clears the list.
 *
 * @param[in] ...  Optional arguments of the message -- starting with the format
 *                 of the message.
 */
#define mylog_error(...)       MYLOG_LOG2(MYLOG_LEVEL_ERROR,   __VA_ARGS__)
/**
 * Adds a message to the current thread's list of messages, writes the list at
 * the WARNING level, and then clears the list.
 *
 * @param[in] ...  Optional arguments of the message -- starting with the format
 *                 of the message.
 */
#define mylog_warning(...)     MYLOG_LOG2(MYLOG_LEVEL_WARNING, __VA_ARGS__)
/**
 * Adds a message to the current thread's list of messages, writes the list at
 * the NOTICE level, and then clears the list.
 *
 * @param[in] ...  Optional arguments of the message -- starting with the format
 *                 of the message.
 */
#define mylog_notice(...)      MYLOG_LOG2(MYLOG_LEVEL_NOTICE,  __VA_ARGS__)
/**
 * Adds a message to the current thread's list of messages, writes the list at
 * the INFO level, and then clears the list.
 *
 * @param[in] ...  Optional arguments of the message -- starting with the format
 *                 of the message.
 */
#define mylog_info(...)        MYLOG_LOG2(MYLOG_LEVEL_INFO,    __VA_ARGS__)
/**
 * Adds a message to the current thread's list of messages, writes the list at
 * the DEBUG level, and then clears the list.
 *
 * @param[in] ...  Optional arguments of the message -- starting with the format
 *                 of the message.
 */
#define mylog_debug(...)       MYLOG_LOG2(MYLOG_LEVEL_DEBUG,   __VA_ARGS__)
/**
 * Adds a message to the current thread's list of messages, writes the list at
 * the given level, and then clears the list.
 *
 * @param[in] level  `mylog_level_t` logging level.
 * @param[in] ...    Optional arguments of the message -- starting with the
 *                   format of the message.
 */
#define mylog_log(level, ...)  MYLOG_LOG2(level,               __VA_ARGS__)

/**
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-list for the current thread.
 *
 * @param[in] level  The level at which to log the messages. One of
 *                   MYLOG_LEVEL_ERROR, MYLOG_LEVEL_WARNING, MYLOG_LEVEL_NOTICE,
 *                   MYLOG_LEVEL_INFO, or MYLOG_LEVEL_DEBUG; otherwise, the
 *                   behavior is undefined.
 */
void mylog_flush(
        const mylog_level_t    level);

/**
 * Writes the message-list of the current thread at the ERROR level and then
 * clears the list.
 */
#define mylog_flush_error()    mylog_flush(MYLOG_LEVEL_ERROR)
/**
 * Writes the message-list of the current thread at the WARNING level and then
 * clears the list.
 */
#define mylog_flush_warning()  mylog_flush(MYLOG_LEVEL_WARNING)
/**
 * Writes the message-list of the current thread at the NOTICE level and then
 * clears the list.
 */
#define mylog_flush_notice()   mylog_flush(MYLOG_LEVEL_NOTICE)
/**
 * Writes the message-list of the current thread at the INFO level and then
 * clears the list.
 */
#define mylog_flush_info()     mylog_flush(MYLOG_LEVEL_INFO)
/**
 * Writes the message-list of the current thread at the DEBUG level and then
 * clears the list.
 */
#define mylog_flush_debug()    mylog_flush(MYLOG_LEVEL_DEBUG)

/**
 * Allocates memory. Adds an error-message the current thread's list of
 * error-messages if an error occurs.
 *
 * @param[in] nbytes    Number of bytes to allocate.
 * @param[in] msg       Message to print on error. Should complete the sentence
 *                      "Couldn't allocate <n> bytes for ...".
 * @retval    NULL      Out of memory. Log message added.
 * @return              Pointer to the allocated memory.
 */
#define mylog_malloc(nbytes, msg) mylog_malloc_located(__FILE__, __func__, __LINE__, \
        nbytes, msg)

/**
 * Writes an error message and then aborts the current process.
 *
 * @param[in] ...  Optional arguments of the message -- starting with the
 *                 format of the message.
 */
#define mylog_abort(...) do { \
    mylog_error(__VA_ARGS__); \
    abort(); \
} while (false)

#ifdef NDEBUG
    #define mylog_assert(expr)
#else
    /**
     * Tests an assertion. Writes an error-message and then aborts the process
     * if the assertion is false.
     *
     * @param[in] expr  The assertion to be tested.
     */
    #define mylog_assert(expr) do { \
        if (!(expr)) \
            mylog_abort("Assertion failure: %s", #expr); \
    } while (false)
#endif

#ifdef __cplusplus
    }
#endif

#endif /* ULOG_MYLOG_H_ */
