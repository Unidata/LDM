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

#include <stdarg.h>
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
 * A log-message.  Such structures accumulate in a thread-specific message-queue.
 */
typedef struct message {
    struct message* next;       ///< Pointer to next message
    log_loc_t       loc;        ///< Location where the message was created
    char*           string;     ///< Message buffer
    size_t          size;       ///< Size of message buffer
} Message;

#if WANT_ULOG
    // `ulog` implementation:
    #include "ulog/ulog.h"
    #include <limits.h>
    #include <stdio.h>
#endif

/**
 *  Logging level.
 */
extern volatile log_level_t log_level;

/**
 * The persistent destination specification.
 */
extern char        log_dest[];

/**
 * Finalizes the logging module. Should be called eventually after
 * log_init(), after which no more logging should occur.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int log_fini_located(
        const log_loc_t* loc);

/**
 * Frees the log-message resources of the current thread. Should only be called
 * when no more logging by the current thread will occur.
 */
void log_free_located(
        const log_loc_t* loc);


/******************************************************************************
 * Internal logging library functions:
 ******************************************************************************/

/**
 * Returns the system logging priority associated with a logging level.
 *
 * @param[in] level    The logging level
 * @retval    LOG_ERR  `level` is invalid
 * @return             The system logging priority associated with `level`
 */
int logl_level_to_priority(
        const log_level_t level);

/**
 * Acquires this module's mutex.
 */
void logl_lock(void);

/**
 * Releases this module's mutex.
 */
void logl_unlock(void);

/**
 * Vets a logging level.
 *
 * @param[in] level  The logging level to be vetted.
 * @retval    true   iff `level` is a valid level.
 */
static inline bool logl_vet_level(
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
const char* logl_basename(
        const char* const pathname);

/**
 * Logs a single variadic message, bypassing the message-queue.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message.
 * @param[in] args    Format arguments.
 */
void logl_vlog_1(
        const log_loc_t* const  loc,
        const log_level_t       level,
        const char* const       format,
        va_list                 args);

/**
 * Logs a single message, bypassing the message-queue.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message or NULL.
 * @param[in] ...     Optional format arguments.
 */
void logl_log_1(
        const log_loc_t* const loc,
        const log_level_t      level,
        const char* const      format,
                               ...);

/**
 * Logs a single message based on a system error code, bypassing the message
 * queue.
 *
 * @param[in] loc     The location where the error occurred. `loc->file` must
 *                    persist.
 * @param[in] errnum  The system error code (e.g., `errno`).
 * @param[in] fmt     Format of the user's message or NULL.
 * @param[in] ...     Optional format arguments.
 */
void logl_errno_1(
        const log_loc_t* const loc,
        const int              errnum,
        const char* const      fmt,
                               ...);

/**
 * Adds a variadic message to the current thread's queue of messages. Emits and
 * then clears the queue.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message.
 * @param[in] args    Format arguments.
 */
void logl_vlog_q(
        const log_loc_t* const  loc,
        const log_level_t       level,
        const char* const       format,
        va_list                 args);

/**
 * Adds a message to the current thread's queue of messages. Emits and then
 * clears the queue.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message or NULL.
 * @param[in] ...     Optional Format arguments.
 */
void logl_log_q(
        const log_loc_t* const loc,
        const log_level_t      level,
        const char* const      format,
                               ...);

/**
 * Adds a system error message and an optional user's message to the current
 * thread's message-queue, emits the queue, and then clears the queue.
 *
 * @param[in] loc     The location where the error occurred. `loc->file` must
 *                    persist.
 * @param[in] errnum  The system error number (i.e., `errno`).
 * @param[in] fmt     Format of the user's message or NULL.
 * @param[in] ...     Optional format arguments.
 */
void logl_errno_q(
        const log_loc_t* const loc,
        const int              errnum,
        const char* const      fmt,
                               ...);

/**
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-queue for the current thread.
 *
 * @param[in] loc    Location.
 * @param[in] level  The level at which to log the messages. One of
 *                   LOG_LEVEL_ERROR, LOG_LEVEL_WARNING, LOG_LEVEL_NOTICE,
 *                   LOG_LEVEL_INFO, or LOG_LEVEL_DEBUG; otherwise, the
 *                   behavior is undefined.
 */
void logl_flush(
        const log_loc_t* const loc,
        const log_level_t      level);

/**
 * Adds a variadic log-message to the message-queue for the current thread.
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
int logl_vadd(
        const log_loc_t* const  loc,
        const char *const       fmt,
        va_list                 args);

/**
 * Adds a log-message for the current thread.
 *
 * @param[in] loc  Location where the message was created. `loc->file` must
 *                 persist.
 * @param[in] fmt     Formatting string for the message.
 * @param[in] ...  Arguments for the formatting string.
 * @retval    0    Success.
 */
int logl_add(
        const log_loc_t* const loc,
        const char *const      fmt,
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
int logl_add_errno(
        const log_loc_t* const loc,
        const int              errnum,
        const char* const      fmt,
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
void* logl_malloc(
        const char* const file,
        const char* const func,
        const int         line,
        const size_t      nbytes,
        const char* const msg);

/**
 * Re-allocates memory. Thread safe.
 *
 * @param[in] file      Pathname of the file.
 * @param[in] func      Name of the function.
 * @param[in] line      Line number in the file.
 * @param[in] buf       Previously-allocated buffer
 * @param[in  nbytes    Number of bytes to re-allocate.
 * @param[in] msg       Message to print on error. Should complete the sentence
 *                      "Couldn't allocate <n> bytes for ...".
 * @retval    NULL      Out of memory. Log message added.
 * @return              Pointer to the allocated memory.
 */
void* logl_realloc(
        const char* const file,
        const char* const func,
        const int         line,
        void*             buf,
        const size_t      nbytes,
        const char* const msg);

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] ...  Message arguments -- starting with the format.
 */
#define logl_internal(level, ...) do { \
    LOG_LOC_DECL(loc); \
    logi_internal(level, &loc, __VA_ARGS__); \
} while (false)

#ifdef NDEBUG
    #define logl_assert(expr)
#else
    /**
     * Tests an assertion. Writes an error-message and then aborts the process
     * if the assertion is false.
     *
     * @param[in] expr  The assertion to be tested.
     */
    #define logl_assert(expr) do { \
        if (!(expr)) { \
            logl_internal(LOG_LEVEL_ERROR, "Assertion failure: %s", #expr); \
            abort(); \
        } \
    } while (false)
#endif

/**
 * Declares an instance of a location structure. NB: `__func__` is an automatic
 * variable with local scope.
 */
#define LOG_LOC_DECL(loc) const log_loc_t loc = {__FILE__, __func__, __LINE__}

#define LOG_LOG(level, ...) do {\
    if ((level) < log_level) {\
        log_clear();\
    }\
    else {\
        LOG_LOC_DECL(loc);\
        logl_log_q(&loc, level, __VA_ARGS__);\
    }\
} while (0)

/******************************************************************************
 * Logging implementation functions:
 ******************************************************************************/

/**
 * Sets the logging destination.
 *
 * @pre                Module is locked
 * @retval  0          Success
 * @retval -1          Failure
 */
int logi_set_destination(void);

/**
 * Initializes the logging module's implementation. Should be called before any
 * other function. `log_dest` must be set.
 *
 * @param[in] id       The pathname of the program (e.g., `argv[0]`). Caller may
 *                     free.
 * @retval    0        Success.
 * @retval    -1       Error. Logging module is in an unspecified state.
 */
int logi_init(
        const char* const id);

/**
 * Re-initializes the logging module based on its state just prior to calling
 * log_fini_impl(). If log_fini_impl(), wasn't called, then the result is
 * unspecified.
 *
 * @retval   -1        Failure
 * @retval    0        Success
 */
int logi_reinit(void);

/**
 * Enables logging down to the level given by `log::log_level`. Should be called
 * after logi_init().
 */
void logi_set_level(void);

/**
 * Sets the logging identifier. Should be called between `logi_init()` and
 * `logi_fini()`.
 *
 * @param[in] id        The new identifier. Caller may free.
 * @retval    0         Success.
 * @retval    -1        Failure.
 */
int logi_set_id(
        const char* const id);

/**
 * Finalizes the logging module's implementation. Should be called eventually
 * after `log_impl_init()`, after which no more logging should occur.
 *
 * @retval 0   Success.
 * @retval -1  Failure. Logging module is in an unspecified state.
 */
int logi_fini(void);

/**
 * Emits a single log message.
 *
 * @param[in] level  Logging level.
 * @param[in] loc    The location where the message was generated.
 * @param[in] string The message.
 */
void logi_log(
        const log_level_t level,
        const log_loc_t*  loc,
        const char*       string);

/**
 * Flushes logging.
 */
void logi_flush(void);

/**
 * Emits an error message. Used internally when an error occurs in this logging
 * module.
 *
 * @param[in] level  Logging level.
 * @param[in] loc    Location where the message was generated.
 * @param[in] ...    Message arguments -- starting with the format.
 */
void logi_internal(
        const log_level_t      level,
        const log_loc_t* const loc,
                               ...);

#ifdef __cplusplus
    }
#endif

#endif /* ULOG_LOG_INTERNAL_H_ */
