/**
 * Copyright 2015 University Corporation for Atmospheric Research. All rights
 * reserved. See the the file COPYRIGHT in the top-level source-directory for
 * licensing conditions.
 *
 *   @file: log.h
 * @author: Steven R. Emmerson
 *
 * This file defines the internal-use-only API of the LDM logging system.
 */

#ifndef ULOG_LOG_INTERNAL_H_
#define ULOG_LOG_INTERNAL_H_

#include "config.h"

#include <stdbool.h>
#include <string.h>
#include <sys/types.h>

#define SYSLOG_SPEC ""
#define STDERR_SPEC "-"
#define LOG_IS_SYSLOG_SPEC(spec) (strcmp(spec, SYSLOG_SPEC) == 0)
#define LOG_IS_STDERR_SPEC(spec) (strcmp(spec, STDERR_SPEC) == 0)
#define LOG_IS_FILE_SPEC(spec)   (!LOG_IS_SYSLOG_SPEC(spec) && !LOG_IS_STDERR_SPEC(spec))

#ifdef __cplusplus
    extern "C" {
#endif

/**
 * Structure containing information on the location in the code where a log
 * message was generated.
 */
typedef struct {
    // The pathname of the file
    const char* file;
    // Pointer to the name of the function. Might point to `func_buf` or
    // somwhere else (e.g., to a function's `__function__`` value).
    const char* func;
    // The origin-1 line-number in the file
    int         line;
    // Buffer that might contain the name of the function. NB: The C standard
    // doesn't actually limit the length of identifiers.
    char        func_buf[64];
} log_loc_t;

/**
 * A log-message.  Such structures accumulate in a thread-specific message-list.
 */
typedef struct message {
    struct message* next;       ///< Pointer to next message
    log_loc_t       loc;        ///< Location where the message was created
    char*           string;     ///< Message buffer
    size_t          size;       ///< Size of message buffer
} Message;

#if WANT_SLOG
    // `slog` implementation:

    /// Map from MYLOG levels to SYSLOG priorities
    extern int log_syslog_priorities[];

    /// Returns the SYSLOG priority corresponding to a MYLOG level
    #define log_get_priority(level)     log_syslog_priorities[level]

    /// Indicates if a log message of the given level would be emitted
    #define log_is_level_enabled(level) slog_is_priority_enabled(level)

    extern int slog_is_priority_enabled(const log_level_t level);
#elif WANT_LOG4C
    // `log4c` implementation:

    #include <log4c.h>

    /// Map from MYLOG levels to LOG4C priorities
    extern int               log_log4c_priorities[];
    /// The current working LOG4C category
    extern log4c_category_t* log_category;

    /// Returns the current working LOG4C category
    #define log_get_category()          log_category

    /// Returns the LOG4C priority corresponding to a MYLOG level
    #define log_get_priority(level)     log_log4c_priorities[level]

    /// Indicates if a log message of the given level would be emitted
    #define log_is_level_enabled(level) \
        log4c_category_is_priority_enabled(log_get_category(), \
                log_get_priority(level))
#elif WANT_ULOG
    // `ulog` implementation:

    #include "ulog/ulog.h"
    #include <limits.h>
    #include <stdio.h>

    /// Map from MYLOG levels to SYSLOG priorities
    extern int log_syslog_priorities[];

    /// Returns the SYSLOG priority corresponding to a MYLOG level
    #define log_get_priority(level)     log_syslog_priorities[level]

    /// Indicates if a log message of the given level would be emitted
    #define log_is_level_enabled(level) \
        ulog_is_priority_enabled(log_get_priority(level))
#endif // `ulog` implementation

/**
 * Acquires this module's mutex.
 */
void log_lock(void);

/**
 * Releases this module's mutex.
 */
void log_unlock(void);

/**
 * Indicates if the current process is a daemon.
 *
 * @retval `true` iff the current process is a daemon
 */
bool log_am_daemon(void);

/**
 * Returns the logging destination. Should be called between log_init() and
 * log_fini().
 *
 * @pre          Module is locked
 * @return       The logging destination. One of <dl>
 *                   <dt>""      <dd>The system logging daemon.
 *                   <dt>"-"     <dd>The standard error stream.
 *                   <dt>else    <dd>The pathname of the log file.
 *               </dl>
 */
const char* log_get_destination_impl(void);

/**
 * Sets the logging destination.
 *
 * @pre                Module is locked
 * @param[in] dest     The logging destination. Caller may free. One of <dl>
 *                         <dt>""   <dd>The system logging daemon.
 *                         <dt>"-"  <dd>The standard error stream.
 *                         <dt>else <dd>The file whose pathname is `dest`.
 *                     </dl>
 * @retval    0        Success.
 * @retval    -1       Failure.
 */
int log_set_destination_impl(
        const char* const dest);

/**
 * Initializes the logging module's implementation. Should be called before any
 * other function.
 *
 * @param[in] id       The pathname of the program (e.g., `argv[0]`). Caller may
 *                     free.
 * @retval    0        Success.
 * @retval    -1       Error. Logging module is in an unspecified state.
 */
int log_init_impl(
        const char* const id);

/**
 * Re-initializes the logging module based on its state just prior to calling
 * log_fini_impl(). If log_fini_impl(), wasn't called, then the result is
 * unspecified.
 *
 * @retval   -1        Failure
 * @retval    0        Success
 */
int log_reinit_impl(void);

/**
 * Finalizes the logging module's implementation. Should be called eventually
 * after `log_impl_init()`, after which no more logging should occur.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int log_fini_impl(void);

/**
 * Vets a logging level.
 *
 * @param[in] level  The logging level to be vetted.
 * @retval    true   iff `level` is a valid level.
 */
static inline bool log_vet_level(
        log_level_t level)
{
    return level >= LOG_LEVEL_DEBUG && level <= LOG_LEVEL_ERROR;
}

/**
 * Returns a pointer to the last component of a pathname.
 *
 * @param[in] pathname  The pathname.
 * @return              Pointer to the last component of the pathname.
 */
const char* log_basename(
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
void log_vlog_located(
        const log_loc_t* const loc,
        const log_level_t      level,
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
void log_log_located(
        const log_loc_t* const loc,
        const log_level_t      level,
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
void log_errno_located(
        const log_loc_t* const loc,
        const int              errnum,
        const char* const      fmt,
                               ...);

/**
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-list for the current thread.
 *
 * @param[in] loc    Location.
 * @param[in] level  The level at which to log the messages. One of
 *                   LOG_LEVEL_ERROR, LOG_LEVEL_WARNING, LOG_LEVEL_NOTICE,
 *                   LOG_LEVEL_INFO, or LOG_LEVEL_DEBUG; otherwise, the
 *                   behavior is undefined.
 */
void log_flush_located(
        const log_loc_t* const loc,
        const log_level_t      level);

/**
 * Emits a single log message.
 *
 * @param[in] level  Logging level.
 * @param[in] loc    The location where the message was generated.
 * @param[in] string The message.
 */
void log_write(
        const log_level_t level,
        const log_loc_t*  loc,
        const char*       string);

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
int log_vadd_located(
        const log_loc_t* const loc,
        const char *const      fmt,
        va_list                args);

/**
 * Adds a log-message for the current thread.
 *
 * @param[in] loc  Location where the message was created. `loc->file` must
 *                 persist.
 * @param[in] fmt     Formatting string for the message.
 * @param[in] ...  Arguments for the formatting string.
 * @retval    0    Success.
 */
int log_add_located(
        const log_loc_t* const loc,
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
int log_add_errno_located(
        const log_loc_t* const loc,
        const int                errnum,
        const char* const        fmt,
                                 ...);

/**
 * Allocates memory. Thread safe. Defined with explicit location parameters so
 * that `LOG_MALLOC` can be used in an initializer.
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
void* log_malloc_located(
        const char* const file,
        const char* const func,
        const int         line,
        const size_t      nbytes,
        const char* const msg);

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] level  Logging level.
 * @param[in] loc    Location where the message was generated.
 * @param[in] ...    Message arguments -- starting with the format.
 */
void log_internal_located(
        const log_level_t      level,
        const log_loc_t* const loc,
                               ...);

/**
 * Declares an instance of a location structure. NB: `__func__` is an automatic
 * variable with local scope.
 */
#define LOG_LOC_DECL(loc) const log_loc_t loc = {__FILE__, __func__, __LINE__}

#define LOG_LOG(level, ...) do {\
    if (log_is_level_enabled(level)) {\
        LOG_LOC_DECL(loc);\
        log_log_located(&loc, level, __VA_ARGS__);\
    }\
} while (false)

#define LOG_LOG_FLUSH(level, ...) do {\
    if (!log_is_level_enabled(level)) {\
        log_clear();\
    }\
    else {\
        LOG_LOC_DECL(loc);\
        log_add_located(&loc, __VA_ARGS__);\
        log_flush(level);\
    }\
} while (false)

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] ...  Message arguments -- starting with the format.
 */
#define log_internal(level, ...) do { \
    LOG_LOC_DECL(loc); \
    log_internal_located(level, &loc, __VA_ARGS__); \
} while (false)

#ifdef __cplusplus
    }
#endif

#endif /* ULOG_LOG_INTERNAL_H_ */
