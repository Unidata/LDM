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

#include "config.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>

#if WANT_LOG4C
    #include "log4c.h"
#endif

typedef enum {
    MYLOG_LEVEL_DEBUG = 0,
    MYLOG_LEVEL_INFO,
    MYLOG_LEVEL_NOTICE,
    MYLOG_LEVEL_WARNING,
    MYLOG_LEVEL_ERROR,
    MYLOG_LEVEL_COUNT
} mylog_level_t;

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
int mylog_modify_level(
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

/*
 * NOTE: Do not use any of the following until further notice. They are private
 * details of the implementation and may change.
 */

typedef struct {
    const char* file;
    int         line;
} mylog_loc_t;

/**
 * A log-message.  Such structures accumulate in a thread-specific message-list.
 */
typedef struct message {
    struct message* next;       ///< Pointer to next message
    mylog_loc_t     loc;        ///< Location where the message was created
    char*           string;     ///< Message buffer
    size_t          size;       ///< Size of message buffer
} Message;

#if WANT_LOG4C
    // `log4c` implementation:

    #include <log4c.h>

    extern int               mylog_log4c_priorities[];
    extern log4c_category_t* mylog_category;

    #define mylog_get_category()          mylog_category

    #define mylog_get_priority(level)     mylog_log4c_priorities[level]

    #define mylog_is_level_enabled(level) \
        log4c_category_is_priority_enabled(mylog_get_category(), \
                mylog_get_priority(level))
#else
    // `ulog` implementation:

    #include "ulog.h"
    #include <limits.h>
    #include <stdio.h>

    extern int mylog_syslog_priorities[];

    #define mylog_get_priority(level)     mylog_syslog_priorities[level]

    #define mylog_is_level_enabled(level) \
        ulog_is_priority_enabled(mylog_get_priority(level))
#endif // `ulog` implementation

/**
 * Adds a variadic message to the current thread's list of messages. Emits and
 * then clears the list.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message.
 * @param[in] args    Format arguments.
 */
void mylog_vlog(
        const mylog_loc_t* const loc,
        const mylog_level_t      level,
        const char* const        format,
        va_list                  args);

/**
 * Adds a message to the current thread's list of messages. Emits and then
 * clears the list.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message.
 * @param[in] ...     Format arguments.
 */
void mylog_log(
        const mylog_loc_t* const loc,
        const mylog_level_t      level,
        const char* const        format,
                               ...);

/**
 * Emits a single log message.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
void mylog_emit(
        const mylog_level_t    level,
        const Message* const   msg);

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] fmt  Format of the message.
 * @param[in] ...  Format arguments.
 */
void mylog_error(
        const char* const      fmt,
                               ...);

/**
 * Adds a variadic log-message to the message-list for the current thread.
 *
 * @param[in] loc       Location where the message was created. `loc->file` must
 *                      persist.
 * @param[in] fmt       Formatting string.
 * @param[in] args      Formatting arguments.
 * @retval 0            Success
 * @retval EINVAL       `fmt` or `args` is `NULL`. Error message logged.
 * @retval EINVAL       There are insufficient arguments. Error message logged.
 * @retval EILSEQ       A wide-character code that doesn't correspond to a
 *                      valid character has been detected. Error message logged.
 * @retval ENOMEM       Out-of-memory. Error message logged.
 * @retval EOVERFLOW    The length of the message is greater than {INT_MAX}.
 *                      Error message logged.
 */
int mylog_list_vadd(
        const mylog_loc_t* const loc,
        const char *const        fmt,
        va_list                  args);

/**
 * Adds a log-message for the current thread.
 *
 * @param[in] fmt  Formatting string for the message.
 * @param[in] ...  Arguments for the formatting string.
 * @retval    0    Success.
 */
int mylog_list_add(
        const mylog_loc_t* const loc,
        const char *const        fmt,
                                 ...);

/**
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-list for the current thread.
 *
 * @param[in] level  The level at which to log the messages. One of MYLOG_ERR,
 *                   MYLOG_WARNING, MYLOG_NOTICE, MYLOG_INFO, or MYLOG_DEBUG;
 *                   otherwise, the behavior is undefined.
 */
void mylog_list_emit(
        const mylog_level_t    level);

/**
 * Frees the log-message resources of the current thread. Should only be called
 * when no more logging by the current thread will occur.
 */
void mylog_list_free(void);

#define MYLOG_LOC_DECL(loc) const mylog_loc_t loc = {__FILE__, __LINE__}

#define MYLOG_LOG2(level, ...)       do { \
                                       MYLOG_LOC_DECL(loc); \
                                       mylog_log(&loc, level, __VA_ARGS__); \
                                   } while (false)

/*
 * FURTHER NOTICE: The following may be used. They are part of the public API.
 */

// The following add to the current thread's list of error messages:
#define MYLOG_ADD(...)             do { \
                                       MYLOG_LOC_DECL(loc); \
                                       mylog_list_add(&loc, __VA_ARGS__);\
                                   } while (false)
#define MYLOG_VADD(fmt, args)      do { \
                                       MYLOG_LOC_DECL(loc); \
                                       mylog_list_vadd(&loc, fmt, args); \
                                   } while (false)
#define MYLOG_ADD_ERRNUM(n)          MYLOG_ADD("%s", strerror(n))
#define MYLOG_ADD_ERRNO()            MYLOG_ADD_ERRNUM(errno)

/*
 * The following log the current thread's list of error messages -- together
 * with an optional message -- and clear the list:
 */
#define MYLOG_ERRNUM(n, ...)       do { \
                                       MYLOG_LOC_DECL(loc); \
                                       mylog_list_add(&loc, "%s", strerror(n)); \
                                       mylog_log(&loc, MYLOG_ERROR, __VA_ARGS__); \
                                   } while (false)
/*
 * In the following, "..." must comprise at least a `printf()`-style
 * format-string to avoid a syntax error.
 */
#define MYLOG_ERRNO(...)             MYLOG_ERRNUM(errno, __VA_ARGS__)
#define MYLOG_ERROR(...)             MYLOG_LOG2(MYLOG_LEVEL_ERROR,   __VA_ARGS__)
#define MYLOG_WARNING(...)           MYLOG_LOG2(MYLOG_LEVEL_WARNING, __VA_ARGS__)
#define MYLOG_NOTICE(...)            MYLOG_LOG2(MYLOG_LEVEL_NOTICE,  __VA_ARGS__)
#define MYLOG_INFO(...)              MYLOG_LOG2(MYLOG_LEVEL_INFO,    __VA_ARGS__)
#define MYLOG_DEBUG(...)             MYLOG_LOG2(MYLOG_LEVEL_DEBUG,   __VA_ARGS__)
#define MYLOG_VLOG(level, fmt, args) do { \
                                         MYLOG_LOC_DECL(loc); \
                                         mylog_vlog(&loc, level, fmt, args); \
                                     } while (false)

#ifdef NDEBUG
    #define MYLOG_ASSERT(expr)
#else
    #define MYLOG_ASSERT(expr) {\
        do {\
            if (!(expr)) {\
                mylog_error("Assertion failure: " #expr);\
                abort();\
            }\
        } while (false)
    }
#endif

#ifdef __cplusplus
    }
#endif

#endif /* ULOG_MYLOG_H_ */
