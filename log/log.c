/**
 *   Copyright 2016, University Corporation for Atmospheric Research. All rights
 *   reserved. See the file COPYRIGHT in the top-level source-directory for
 *   licensing conditions.
 *
 * Provides for accumulating log-messages into a thread-specific list and the
 * logging of that list at a single logging level.
 *
 * This module is thread-safe.
 *
 * @file   log.c
 * @author Steven R. Emmerson
 *
 * REQUIREMENTS:
 *   - Can log to
 *     - System logging daemon (-l '')
 *     - Standard error stream (-l -) if not a daemon
 *     - File (-l _pathname_)
 *   - Default destination for log messages
 *     - If daemon
 *       - If backward-compatible: system logging daemon
 *       - If not backward-compatible: standard LDM log file
 *     - Otherwise, standard error stream
 *   - Pathname of standard LDM log file configurable at session time
 *   - Output format
 *     - If using system logging daemon: chosen by daemon
 *     - Otherwise:
 *       - Pattern: _time_ _process_ _priority_ _location_ _message_
 *         - _time_: <em>YYYYMMDD</em>T<em>hhmmss</em>.<em>uuuuuu</em>Z
 *         - _process_: _program_[_pid_]
 *         - _priority_: DEBUG | INFO | NOTE | WARN | ERROR
 *         - _location_: _file_:_line_
 *       - Example: 20160113T150106.734013Z noaaportIngester[26398] NOTE process_prod.c:216 SDUS58 PACR 062008 /pN0RABC inserted
 *   - Enable log file rotation by refreshing destination upon SIGHUP reception
 */
#include <config.h>

#undef NDEBUG
#include "log.h"
#include "mutex.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>   /* NULL */
#include <stdio.h>    /* vsnprintf(), snprintf() */
#include <stdlib.h>   /* malloc(), free(), abort() */
#include <string.h>
#include <unistd.h>

#ifndef _XOPEN_NAME_MAX
    #define _XOPEN_NAME_MAX 255 // not always defined
#endif

/**
 * A list of log messages.
 */
typedef struct list {
    Message*    first;
    Message*    last;           /* NULL => empty list */
} List;

/**
 * Key for the thread-specific list of log messages.
 */
static pthread_key_t         listKey;
/**
 * Whether or not the thread-specific key has been created.
 */
static pthread_once_t        key_creation_control = PTHREAD_ONCE_INIT;
/**
 * The mutex that makes this module thread-safe.
 */
static pthread_mutex_t       mutex;
/**
 * Whether a SIGHUP has been delivered.
 */
static volatile sig_atomic_t hupped;
/**
 * The SIGHUP signal set.
 */
static sigset_t              hup_sigset;
/**
 * The SIGHUP action for this module.
 */
static struct sigaction      hup_sigaction;
/**
 * The previous SIGHUP action when this module's SIGHUP action is registered.
 */
static struct sigaction      prev_hup_sigaction;
/**
 * The signal mask of all signals.
 */
static sigset_t              all_sigset;

/**
 * Blocks all signals for the current thread. This is done so that the
 * functions of this module may be called by a signal handler.
 *
 * @param[out] prevset  The signal mask on entry to this function. The caller
 *                      should call `restoreSigs(prevset)` to restore the
 *                      original signal mask.
 */
static void blockSigs(
        sigset_t* const prevset)
{
    (void)pthread_sigmask(SIG_BLOCK, &all_sigset, prevset);
}

/**
 * Restores the signal mask of the current thread.
 *
 * @param[in] sigset  The set of signals to be blocked.
 */
static void restoreSigs(
        const sigset_t* const sigset)
{
    (void)pthread_sigmask(SIG_SETMASK, sigset, NULL);
}

/**
 * Handles SIGHUP delivery. Sets variable `hupped` and ensures that any
 * previously-registered SIGHUP handler is called.
 *
 * @param[in] sig  SIGHUP.
 */
static void handle_sighup(
        const int sig)
{
    hupped = 1;
    if (prev_hup_sigaction.sa_handler != SIG_DFL &&
            prev_hup_sigaction.sa_handler != SIG_IGN) {
        (void)sigaction(SIGHUP, &prev_hup_sigaction, NULL);
        raise(SIGHUP);
        (void)sigprocmask(SIG_UNBLOCK, &hup_sigset, NULL);
        (void)sigaction(SIGHUP, &hup_sigaction, NULL);
    }
}

/**
 * Locks this module's lock.
 *
 * This function is thread-safe. On entry, this module's lock shall be
 * unlocked.
 */
static void lock(void)
{
    int status = pthread_mutex_lock(&mutex);
    log_assert(status == 0);
    if (hupped) {
        log_refresh();
        hupped = 0;
    }
}

/**
 * Unlocks this module's lock.
 *
 * This function is thread-safe. On entry, this module's lock shall be locked
 * by the current thread.
 */
static void unlock(void)
{
    int status = pthread_mutex_unlock(&mutex);
    log_assert(status == 0);
}

/**
 * Initializes a location structure.
 *
 * @param[out] dest  The location to be initialized.
 * @param[in]  src   The location whose values are to be used for
 *                   initialization.
 */
static void loc_init(
        log_loc_t* const restrict       dest,
        const log_loc_t* const restrict src)
{
    dest->file = src->file; // `__FILE__` is persistent
    char* d = dest->func_buf;
    char* s = src->func;
    while (*s && d < dest->func_buf + sizeof(dest->func_buf) - 1)
        *d++ = *s++;
    *d = 0;
    dest->func = dest->func_buf;
    dest->line = src->line;
}

/**
 * Initializes this logging module.
 */
int log_init(
        const char* const id)
{
    (void)sigfillset(&all_sigset);
    (void)sigemptyset(&hup_sigset);
    (void)sigaddset(&hup_sigset, SIGHUP);
    hup_sigaction.sa_mask = hup_sigset;
    hup_sigaction.sa_flags = SA_RESTART;
    hup_sigaction.sa_handler = handle_sighup;
    (void)sigaction(SIGHUP, &hup_sigaction, &prev_hup_sigaction);
    int status = mutex_init(&mutex, true, true);
    if (status == 0)
        status = log_impl_init(id);
    return status;
}

/**
 * Finalizes this logging module.
 */
int log_fini(void)
{
    int status = log_impl_fini();
    if (status == 0) {
        status = mutex_fini(&mutex);
        if (status == 0)
            (void)sigaction(SIGHUP, &prev_hup_sigaction, NULL);
    }
    return status ? -1 : 0;
}

static void create_key(void)
{
    int status = pthread_key_create(&listKey, NULL);

    if (status != 0) {
        log_internal("pthread_key_create() failure");
        abort();
    }
}

/**
 * Returns the current thread's message-list.
 *
 * This function is thread-safe. On entry, this module's lock shall be
 * unlocked.
 *
 * @return  The message-list of the current thread or NULL if the message-list
 *          doesn't exist and couldn't be created.
 */
static List* list_get(void)
{
    (void)pthread_once(&key_creation_control, create_key);
    List*   list = pthread_getspecific(listKey);
    if (NULL == list) {
        list = (List*)malloc(sizeof(List));
        if (NULL == list) {
            log_internal("malloc() failure");
        }
        else {
            int status = pthread_setspecific(listKey, list);
            if (status != 0) {
                log_internal("pthread_setspecific() failure");
                free(list);
                list = NULL;
            }
            else {
                list->first = NULL;
                list->last = NULL;
            }
        }
    }
    return list;
}

/**
 * Indicates if the message list of the current thread is empty.
 *
 * @retval true iff the list is empty
 */
static bool list_is_empty()
{
    sigset_t sigset;
    blockSigs(&sigset);

    List* const list = list_get();
    const bool  is_empty = list->last == NULL;

    restoreSigs(&sigset);
    return is_empty;
}

/**
 * Clears the accumulated log-messages of the current thread.
 */
static void list_clear()
{
    sigset_t sigset;
    blockSigs(&sigset);

    List*   list = list_get();

    if (NULL != list)
        list->last = NULL;

    restoreSigs(&sigset);
}

/**
 * Returns a new message structure.
 *
 * @param[in] entry   The message structure.
 * @retval    0       Success. `*entry` is set.
 * @retval    ENOMEM  Out-of-memory. Error message logged.
 */
static int msg_new(
        Message** const entry)
{
    int      status;
    Message* msg = (Message*)malloc(sizeof(Message));
    if (msg == NULL) {
        status = errno;
        log_internal("malloc() failure");
    }
    else {
        #define LOG_DEFAULT_STRING_SIZE     256
        char*   string = (char*)malloc(LOG_DEFAULT_STRING_SIZE);
        if (NULL == string) {
            status = errno;
            log_internal("malloc() failure");
            free(msg);
        }
        else {
            *string = 0;
            msg->string = string;
            msg->size = LOG_DEFAULT_STRING_SIZE;
            msg->next = NULL;
            *entry = msg;
            status = 0;
        }
    } // `msg` allocated
    return status;
}

/**
 * Returns the next unused entry in a message-list. Creates it if necessary.
 *
 * @param[in] list    The message-list.
 * @param[in] entry   The next unused entry.
 * @retval    0       Success. `*entry` is set.
 * @retval    ENOMEM  Out-of-memory. Error message logged.
 */
static int list_getNextEntry(
        List* const restrict     list,
        Message** const restrict entry)
{
    Message* msg = (NULL == list->last) ? list->first : list->last->next;
    int      status;

    if (msg != NULL) {
        *entry = msg;
        status = 0;
    }
    else {
        status = msg_new(&msg);
        if (status == 0) {
            if (NULL == list->first)
                list->first = msg;  /* very first message */
            if (NULL != list->last)
                list->last->next = msg;
            *entry = msg;
        } // `msg` allocated
    } // need new message structure

    return status;
}

/**
 * Destroys a list of log-messages.
 *
 * @param[in] list  The list of messages.
 */
static void
list_fini(
        List* const list)
{
    Message* msg;
    Message* next;

    for (msg = list->first; msg; msg = next) {
        next = msg->next;
        free(msg->string);
        free(msg);
    }
}

/**
 * Prints a message into a message-list entry.
 *
 * @param[in] msg        The message entry.
 * @param[in] fmt        The message format.
 * @param[in] args       The arguments to be formatted.
 * @retval    0          Success. The message has been written into `*msg`.
 * @retval    EINVAL     `fmt` or `args` is `NULL`. Error message logged.
 * @retval    EINVAL     There are insufficient arguments. Error message logged.
 * @retval    EILSEQ     A wide-character code that doesn't correspond to a
 *                       valid character has been detected. Error message logged.
 * @retval    ENOMEM     Out-of-memory. Error message logged.
 * @retval    EOVERFLOW  The length of the message is greater than {INT_MAX}.
 *                       Error message logged.
 */
static int msg_format(
        Message* const restrict    msg,
        const char* const restrict fmt,
        va_list                    args)
{
    int nbytes = vsnprintf(msg->string, msg->size, fmt, args);
    int status;

    if (msg->size > nbytes) {
        status = 0;
    }
    else if (0 > nbytes) {
        /*
         * `vsnprintf()` isn't guaranteed to set `errno` according to The Open
         * Group Base Specifications Issue 6
         */
        status = errno ? errno : EILSEQ;
        log_internal("vsnprintf() failure");
    }
    else {
        // The buffer is too small for the message. Expand it.
        size_t  size = nbytes + 1;
        char*   string = (char*)malloc(size);

        if (NULL == string) {
            status = errno;
            log_internal("malloc() failure");
        }
        else {
            free(msg->string);
            msg->string = string;
            msg->size = size;
            (void)vsnprintf(msg->string, msg->size, fmt, args);
            status = 0;
        }
    }                           /* buffer is too small */

    return status;
}

/**
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-list for the current thread.
 *
 * @param[in] level  The level at which to log the messages. One of
 *                   LOG_LEVEL_ERROR, LOG_LEVEL_WARNING, LOG_LEVEL_NOTICE,
 *                   LOG_LEVEL_INFO, or LOG_LEVEL_DEBUG; otherwise, the
 *                   behavior is undefined.
 */
static void flush(
    const log_level_t level)
{
    sigset_t sigset;
    blockSigs(&sigset);
    List*   list = list_get();

    if (NULL != list && NULL != list->last) {
        lock();

        if (log_is_level_enabled(level)) {
            for (const Message* msg = list->first; NULL != msg;
                    msg = msg->next) {
                log_write_one(level, msg);

                if (msg == list->last)
                    break;
            }                       /* message loop */
        }                           /* messages should be printed */

        unlock();
        list_clear();
    }                               /* have messages */
    restoreSigs(&sigset);
}

/**
 * Returns the string associated with a logging level.
 *
 * @param[in] level  The logging level. One of `LOG_LEVEL_DEBUG`,
 *                   `LOG_LEVEL_INFO`, `LOG_LEVEL_NOTICE`,
 *                   `LOG_LEVEL_WARNING`, `LOG_LEVEL_ERROR`,
 *                   `LOG_LEVEL_ALERT`, `LOG_LEVEL_CRIT`, or
 *                   `LOG_LEVEL_EMERG`. The string `"UNKNOWN"` is returned if
 *                   the level is not one of these values.
 * @return           The associated string.
 */
const char* log_level_to_string(
        const log_level_t level)
{
    static const char* const strings[] = {"DEBUG", "INFO", "NOTE", "WARN",
        "ERROR", "ALERT", "CRIT", "FATAL"};
    return log_vet_level(level) ? strings[level] : "UNKNOWN";
}

/**
 * Returns a pointer to the last component of a pathname.
 *
 * @param[in] pathname  The pathname.
 * @return              Pointer to the last component of the pathname.
 */
const char* log_basename(
        const char* const pathname)
{
    const char* const cp = strrchr(pathname, '/');
    return cp ? cp + 1 : pathname;
}

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
        const log_loc_t* const restrict loc,
        const char* const restrict      fmt,
        va_list                         args)
{
    sigset_t sigset;
    blockSigs(&sigset);

    int status;

    if (NULL == fmt) {
        log_internal("NULL argument");
        status = EINVAL;
    }
    else {
        List* list = list_get();
        if (NULL == list) {
            status = ENOMEM;
        }
        else {
            Message* msg;
            status = list_getNextEntry(list, &msg);
            if (status == 0) {
                loc_init(&msg->loc, loc);
                status = msg_format(msg, fmt, args);
                if (status == 0)
                    list->last = msg;
            } // have a message structure
        } // message-list isn't NULL
    } // arguments aren't NULL

    restoreSigs(&sigset);
    return status;
}

/**
 * Adds a log-message for the current thread.
 *
 * @param[in] fmt  Formatting string for the message.
 * @param[in] ...  Arguments for the formatting string.
 * @retval    0    Success.
 */
int log_add_located(
        const log_loc_t* const restrict loc,
        const char* const restrict      fmt,
                                        ...)
{
    sigset_t sigset;
    blockSigs(&sigset);
    va_list     args;
    va_start(args, fmt);
    int status = log_vadd_located(loc, fmt, args);
    va_end(args);
    restoreSigs(&sigset);
    return status;
}

/**
 * Adds a system error message and an optional user message.
 *
 * @param[in] loc     Location.
 * @param[in] errnum  System error number (i.e., `errno`).
 * @param[in] fmt     Formatting string for the message or NULL.
 * @param[in] ...     Arguments for the formatting string.
 * @return
 */
int log_add_errno_located(
        const log_loc_t* const loc,
        const int                errnum,
        const char* const        fmt,
                                 ...)
{
    sigset_t sigset;
    blockSigs(&sigset);
    int status = log_add_located(loc, "%s", strerror(errnum));
    if (status == 0 && fmt && *fmt) {
        va_list     args;
        va_start(args, fmt);
        status = log_vadd_located(loc, fmt, args);
        va_end(args);
    }
    restoreSigs(&sigset);
    return status;
}

/**
 * Clears the message-list of the current thread.
 */
void
log_clear(void)
{
    list_clear();
}

/**
 * Frees the log-message resources of the current thread. Should only be called
 * when no more logging by the current thread will occur.
 */
void
log_free(void)
{
    sigset_t sigset;
    blockSigs(&sigset);

    List*   list = list_get();

    if (list) {
        if (list->last)
            log_error("%s() called with pending messages", __func__);
        list_fini(list);
        free(list);
        (void)pthread_setspecific(listKey, NULL);
    }

    restoreSigs(&sigset);
}

/**
 * Allocates memory. Thread safe.
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
        const char* const restrict file,
        const char* const restrict func,
        const int                  line,
        const size_t               nbytes,
        const char* const          msg)
{
    void* obj = malloc(nbytes);

    if (obj == NULL) {
        log_loc_t loc = {file, line};
        log_add_located(&loc, "Couldn't allocate %lu bytes for %s", nbytes, msg);
    }

    return obj;
}

/**
 * Adds a variadic message to the current thread's list of messages. Emits and
 * then clears the list.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message or NULL.
 * @param[in] args    Optional format arguments.
 */
void log_vlog_located(
        const log_loc_t* const restrict loc,
        const log_level_t               level,
        const char* const restrict      format,
        va_list                         args)
{
    if (format && *format)
        log_vadd_located(loc, format, args);
    flush(level);
}

/**
 * Adds a message to the current thread's list of messages. Emits and then
 * clears the list.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message or NULL.
 * @param[in] ...     Optional format arguments.
 */
void log_log_located(
        const log_loc_t* const restrict loc,
        const log_level_t               level,
        const char* const restrict      format,
                                        ...)
{
    va_list args;
    va_start(args, format);
    log_vlog_located(loc, level, format, args);
    va_end(args);
}

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
        const log_loc_t* const   loc,
        const int                  errnum,
        const char* const restrict fmt,
                                   ...)
{
    va_list args;
    va_start(args, fmt);
    log_add_located(loc, "%s", strerror(errnum));
    log_vlog_located(loc, LOG_LEVEL_ERROR, fmt, args);
    va_end(args);
}

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
        const log_level_t      level)
{
    if (!list_is_empty()) {
        /*
         * The following message is added so that the location of the call to
         * log_flush() is logged in case the call needs to be adjusted.
         */
        log_add_located(loc, "Log messages flushed");
        flush(level);
    }
}


#if 0
/**
 * Sets the first log-message for the current thread.
 *
 * @param[in] fmt  The message format
 * @param[in] ...  Arguments referenced by the format
 */
void log_start(
        const char* const fmt,
                          ...)
{
    sigset_t sigset;
    blockSigs(&sigset);
    va_list     args;
    list_clear();
    va_start(args, fmt);
    log_vadd(fmt, args);
    va_end(args);
    restoreSigs(&sigset);
}

/**
 * Sets a system error-message as the first error-message for the current
 * thread.
 */
void log_errno(void)
{
    log_start("%s", strerror(errno));
}

/**
 * Sets a system error-message as the first error-message for the current thread
 * based on the current value of "errno" and a higher-level error-message.
 */
void log_serror(
    const char* const fmt,  /**< The higher-level message format */
    ...)                    /**< Arguments referenced by the format */
{
    sigset_t sigset;
    blockSigs(&sigset);
    va_list     args;
    log_errno();
    va_start(args, fmt);
    log_vadd(fmt, args);
    va_end(args);
    restoreSigs(&sigset);
}

/**
 * Adds a system error-message for the current thread based on a error number
 * and a higher-level error-message.
 */
void log_errnum(
    const int           errnum, /**< The "errno" error number */
    const char* const   fmt,    /**< The higher-level message format or NULL
                                  *  for no higher-level message */
    ...)                        /**< Arguments referenced by the format */
{
    sigset_t sigset;
    blockSigs(&sigset);

    log_start(strerror(errnum));

    if (NULL != fmt) {
        va_list     args;
        va_start(args, fmt);
        log_vadd(fmt, args);
        va_end(args);
    }

    restoreSigs(&sigset);
}

/**
 * Allocates memory. Thread safe.
 *
 * @param nbytes        Number of bytes to allocate.
 * @param msg           Message to print on error. Should complete the sentence
 *                      "Couldn't allocate <n> bytes for ...".
 * @param file          Name of the file.
 * @param line          Line number in the file.
 * @retval NULL         Out of memory. \c log_start() called.
 * @return              Pointer to the allocated memory.
 */
void* log_malloc(
    const size_t        nbytes,
    const char* const   msg,
    const char* const   file,
    const int           line)
{
    void* obj = malloc(nbytes);

    if (obj == NULL)
        log_internal(LOG_LEVEL_ERROR, "malloc() failure");

    return obj;
}
#endif
