/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: mylog.h
 * @author: Steven R. Emmerson
 *
 * This file defines the internal-use-only API of the LDM logging system.
 */

#ifndef ULOG_MYLOG_INTERNAL_H_
#define ULOG_MYLOG_INTERNAL_H_

#include "config.h"

#include <sys/types.h>

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Structure containing information on the location in the code where a log
 * message was generated.
 */
typedef struct {
    const char* file; ///< The pathname of the file
    int         line; ///< The origin-1 line-number in the file
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

#if WANT_SLOG
    // `slog` implementation:

    /// Map from MYLOG levels to SYSLOG priorities
    extern int mylog_syslog_priorities[];

    /// Returns the SYSLOG priority corresponding to a MYLOG level
    #define mylog_get_priority(level)     mylog_syslog_priorities[level]

    /// Indicates if a log message of the given level would be emitted
    #define mylog_is_level_enabled(level) slog_is_priority_enabled(level)

    extern int slog_is_priority_enabled(const mylog_level_t level);
#elif WANT_LOG4C
    // `log4c` implementation:

    #include <log4c.h>

    /// Map from MYLOG levels to LOG4C priorities
    extern int               mylog_log4c_priorities[];
    /// The current working LOG4C category
    extern log4c_category_t* mylog_category;

    /// Returns the current working LOG4C category
    #define mylog_get_category()          mylog_category

    /// Returns the LOG4C priority corresponding to a MYLOG level
    #define mylog_get_priority(level)     mylog_log4c_priorities[level]

    /// Indicates if a log message of the given level would be emitted
    #define mylog_is_level_enabled(level) \
        log4c_category_is_priority_enabled(mylog_get_category(), \
                mylog_get_priority(level))
#elif WANT_ULOG
    // `ulog` implementation:

    #include "ulog/ulog.h"
    #include <limits.h>
    #include <stdio.h>

    /// Map from MYLOG levels to SYSLOG priorities
    extern int mylog_syslog_priorities[];

    /// Returns the SYSLOG priority corresponding to a MYLOG level
    #define mylog_get_priority(level)     mylog_syslog_priorities[level]

    /// Indicates if a log message of the given level would be emitted
    #define mylog_is_level_enabled(level) \
        ulog_is_priority_enabled(mylog_get_priority(level))
#endif // `ulog` implementation

/**
 * Initializes the logging module's implementation. Should be called before any
 * other function.
 *
 * @param[in] id       The pathname of the program (e.g., `argv[0]`). Caller may
 *                     free.
 * @retval    0        Success.
 * @retval    -1       Error. Logging module is in an unspecified state.
 */
int mylog_impl_init(
        const char* const id);

/**
 * Finalizes the logging module's implementation. Should be called eventually
 * after `mylog_init_impl()`, after which no more logging should occur.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int mylog_impl_fini(void);

/**
 * Vets a logging level.
 *
 * @param[in] level  The logging level to be vetted.
 * @retval    true   iff `level` is a valid level.
 */
static inline bool mylog_vet_level(
        mylog_level_t level)
{
    return level >= MYLOG_LEVEL_DEBUG && level <= MYLOG_LEVEL_ERROR;
}

/**
 * Returns the string associated with a logging level.
 *
 * @param[in] level  The logging level. One of `MYLOG_LEVEL_DEBUG`,
 *                   `MYLOG_LEVEL_INFO`, `MYLOG_LEVEL_NOTICE`,
 *                   `MYLOG_LEVEL_WARNING`, `MYLOG_LEVEL_ERROR`,
 *                   `MYLOG_LEVEL_ALERT`, `MYLOG_LEVEL_CRIT`, or
 *                   `MYLOG_LEVEL_EMERG`. The string `"UNKNOWN"` is returned if
 *                   the level is not one of these values.
 * @return           The associated string.
 */
const char* mylog_level_to_string(
        const mylog_level_t level);

/**
 * Returns a pointer to the last component of a pathname.
 *
 * @param[in] pathname  The pathname.
 * @return              Pointer to the last component of the pathname.
 */
const char* mylog_basename(
        const char* const pathname);

/**
 * Adds a variadic message to the current thread's list of messages. Emits and
 * then clears the list.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message.
 * @param[in] args    Format arguments.
 */
void mylog_vlog_located(
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
 * @param[in] format  Format of the message or NULL.
 * @param[in] ...     Optional Format arguments.
 */
void mylog_log_located(
        const mylog_loc_t* const loc,
        const mylog_level_t      level,
        const char* const        format,
                                 ...);

/**
 * Adds a system error message and an optional user's message to the current
 * thread's message-list, emits the list, and then clears the list.
 *
 * @param[in] loc     The location where the error occurred. `loc->file` must
 *                    persist.
 * @param[in] errnum  The system error number (i.e., `errno`).
 * @param[in] fmt     Format of the user's message or NULL.
 * @param[in] ...     Optional format arguments.
 */
void mylog_errno_located(
        const mylog_loc_t* const loc,
        const int                errnum,
        const char* const        fmt,
                                 ...);

/**
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-list for the current thread.
 *
 * @param[in] loc    Location.
 * @param[in] level  The level at which to log the messages. One of
 *                   MYLOG_LEVEL_ERROR, MYLOG_LEVEL_WARNING, MYLOG_LEVEL_NOTICE,
 *                   MYLOG_LEVEL_INFO, or MYLOG_LEVEL_DEBUG; otherwise, the
 *                   behavior is undefined.
 */
void mylog_flush_located(
        const mylog_loc_t* const loc,
        const mylog_level_t      level);

/**
 * Emits a single log message.
 *
 * @param[in] level  Logging level.
 * @param[in] msg    The message.
 */
void mylog_write_one(
        const mylog_level_t    level,
        const Message* const   msg);

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] fmt  Format of the message.
 * @param[in] ...  Format arguments.
 */
void mylog_internal(
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
int mylog_vadd_located(
        const mylog_loc_t* const loc,
        const char *const        fmt,
        va_list                  args);

/**
 * Adds a log-message for the current thread.
 *
 * @param[in] loc  Location where the message was created. `loc->file` must
 *                 persist.
 * @param[in] fmt     Formatting string for the message.
 * @param[in] ...  Arguments for the formatting string.
 * @retval    0    Success.
 */
int mylog_add_located(
        const mylog_loc_t* const loc,
        const char *const        fmt,
                                 ...);

/**
 * Adds a system error message and an optional user message.
 *
 * @param[in] loc     Location.
 * @param[in] errnum  System error number (i.e., `errno`).
 * @param[in] fmt     Formatting string for the user message or NULL for no user
 *                    message.
 * @param[in] ...     Arguments for the formatting string.
 * @return
 */
int mylog_add_errno_located(
        const mylog_loc_t* const loc,
        const int                errnum,
        const char* const        fmt,
                                 ...);

/**
 * Allocates memory. Thread safe. Defined with explicit location parameters so
 * that `MYLOG_MALLOC` can be used in an initializer.
 *
 * @param[in] file      Pathname of the file.
 * @param[in] func      Name of the function.
 * @param[in] line      Line number in the file.
 * @param[in  nbytes    Number of bytes to allocate.
 * @param[in] msg       Message to print on error. Should complete the sentence
 *                      "Couldn't allocate <n> bytes for ...".
 * @retval    NULL      Out of memory. Log message added.
 * @return              Pointer to the allocated memory.
 */
void* mylog_malloc_located(
        const char* const file,
        const char* const func,
        const int         line,
        const size_t      nbytes,
        const char* const msg);

/// Declares an instance of a location structure
#define MYLOG_LOC_DECL(loc) const mylog_loc_t loc = {__FILE__, __LINE__}

#define MYLOG_LOG2(level, ...) do { \
    if (!mylog_is_level_enabled(level)) {\
        mylog_clear();\
    }\
    else {\
        MYLOG_LOC_DECL(loc); \
        mylog_log_located(&loc, level, __VA_ARGS__); \
    }\
} while (false)

#ifdef __cplusplus
    }
#endif

#endif /* ULOG_MYLOG_INTERNAL_H_ */
