/**
 *   Copyright 2016, University Corporation for Atmospheric Research. All rights
 *   reserved. See the file COPYRIGHT in the top-level source-directory for
 *   licensing conditions.
 *
 * This file implements the the provider-independent `log.h` API. It provides
 * for accumulating log-messages into a thread-specific list and the logging of
 * that list at a single logging level.
 *
 * This module is thread-safe.
 *
 * @file   log.c
 * @author Steven R. Emmerson
 *
 * REQUIREMENTS:
 *   - Can log to
 *     - System logging daemon (-l '')
 *     - Standard error stream (-l -) if it exists
 *     - File (-l _pathname_)
 *   - Default destination for log messages
 *     - To the standard error stream if it exists
 *     - Otherwise:
 *       - If backward-compatible: system logging daemon
 *       - If not backward-compatible: standard LDM log file
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
#include "StrBuf.h"

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
    #define _XOPEN_NAME_MAX 255 // Not always defined
#endif

#ifndef _XOPEN_PATH_MAX
    #define _XOPEN_PATH_MAX 1024 // Not always defined
#endif

/******************************************************************************
 * Private API:
 ******************************************************************************/

/// Whether or not this module is initialized.
static bool isInitialized = false;

/**
 * A list of log messages.
 */
typedef struct list {
    Message*    first;
    Message*    last;           /* NULL => empty list */
} List;

/**
 * Key for the thread-specific string-buffer.
 */
static pthread_key_t         sbKey;
/**
 * Key for the thread-specific list of log messages.
 */
static pthread_key_t         listKey;
/**
 * The thread identifier of the thread on which `log_init()` was called.
 */
static pthread_t             init_thread;
/**
 * Whether or not to avoid using the standard error stream.
 */
static bool                  avoid_stderr;
/**
 * Whether this module needs to be refreshed.
 */
static volatile sig_atomic_t refresh_needed;
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
 * The mutex that makes this module thread-safe.
 */
mutex_t                      log_mutex;

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
 * Handles SIGHUP delivery. Sets variable `refresh_needed` and ensures that any
 * previously-registered SIGHUP handler is called.
 *
 * @param[in] sig  SIGHUP.
 */
static void handle_sighup(
        const int sig)
{
    refresh_needed = 1;
    if (prev_hup_sigaction.sa_handler != SIG_DFL &&
            prev_hup_sigaction.sa_handler != SIG_IGN) {
        (void)sigaction(SIGHUP, &prev_hup_sigaction, NULL);
        raise(SIGHUP);
        (void)sigprocmask(SIG_UNBLOCK, &hup_sigset, NULL);
        (void)sigaction(SIGHUP, &hup_sigaction, NULL);
    }
}

/**
 * Initializes a location structure from another location structure.
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
    char*       d = dest->func_buf;
    const char* s = src->func;
    while (*s && d < dest->func_buf + sizeof(dest->func_buf) - 1)
        *d++ = *s++;
    *d = 0;
    dest->func = dest->func_buf;
    dest->line = src->line;
}

static void sb_create_key(void)
{
    int status = pthread_key_create(&sbKey, NULL);
    if (status != 0) {
        logl_internal(LOG_LEVEL_ERROR, "pthread_key_create() failure: "
                "errno=\"%s\"", strerror(status));
        abort();
    }
}

/**
 * Returns the current thread's string-buffer.
 *
 * This function is thread-safe. On entry, this module's lock shall be
 * unlocked.
 *
 * @return  The string-buffer of the current thread or NULL if the string-buffer
 *          doesn't exist and couldn't be created.
 */
static StrBuf* sb_get(void)
{
    /**
     * Whether or not the thread-specific string-buffer key has been created.
     */
    static pthread_once_t key_creation_control = PTHREAD_ONCE_INIT;
    (void)pthread_once(&key_creation_control, sb_create_key);
    StrBuf* sb = pthread_getspecific(sbKey);
    if (NULL == sb) {
        sb = sbNew();
        if (NULL == sb) {
            logl_internal(LOG_LEVEL_ERROR, "sbNew() failure");
        }
        else {
            int status = pthread_setspecific(sbKey, sb);
            if (status != 0) {
                logl_internal(LOG_LEVEL_ERROR, "pthread_setspecific() failure: "
                        "errno=\"%s\"", strerror(status));
                sbFree(sb);
                sb = NULL;
            }
        }
    }
    return sb;
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
    Message* msg = malloc(sizeof(Message));
    if (msg == NULL) {
        status = errno;
        logl_internal(LOG_LEVEL_ERROR, "malloc() failure: size=%zu",
                sizeof(Message));
    }
    else {
        #define LOG_DEFAULT_STRING_SIZE     256
        char*   string = malloc(LOG_DEFAULT_STRING_SIZE);
        if (NULL == string) {
            status = errno;
            logl_internal(LOG_LEVEL_ERROR, "malloc() failure: size=%u",
                    LOG_DEFAULT_STRING_SIZE);
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
    va_list argsCopy;
    va_copy(argsCopy, args);

    int nbytes = vsnprintf(msg->string, msg->size, fmt, args);
    int status;

    if (msg->size > nbytes) {
        status = 0;
    }
    else if (0 > nbytes) {
        // EINTR, EILSEQ, ENOMEM, or EOVERFLOW
        logl_internal(LOG_LEVEL_ERROR, "vsnprintf() failure: "
                "fmt=\"%s\", errno=\"%s\"", fmt, strerror(errno));
    }
    else {
        // The buffer is too small for the message. Expand it.
        size_t  size = nbytes + 1;
        char*   string = malloc(size);

        if (NULL == string) {
            status = errno;
            logl_internal(LOG_LEVEL_ERROR, "malloc() failure: size=%zu", size);
        }
        else {
            free(msg->string);
            msg->string = string;
            msg->size = size;
            (void)vsnprintf(msg->string, msg->size, fmt, argsCopy);
            status = 0;
        }
    }                           /* buffer is too small */

    va_end(argsCopy);

    return status;
}

static void list_create_key(void)
{
    int status = pthread_key_create(&listKey, NULL);
    if (status != 0) {
        logl_internal(LOG_LEVEL_ERROR, "pthread_key_create() failure: "
                "errno=\"%s\"", strerror(status));
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
    /**
     * Whether or not the thread-specific list key has been created.
     */
    static pthread_once_t key_creation_control = PTHREAD_ONCE_INIT;
    (void)pthread_once(&key_creation_control, list_create_key);
    List* list = pthread_getspecific(listKey);
    if (NULL == list) {
        list = malloc(sizeof(List));
        if (NULL == list) {
            logl_internal(LOG_LEVEL_ERROR, "malloc() failure: size=%zu",
                    sizeof(List));
        }
        else {
            int status = pthread_setspecific(listKey, list);
            if (status != 0) {
                logl_internal(LOG_LEVEL_ERROR, "pthread_setspecific() failure: "
                        "errno=\"%s\"", strerror(status));
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
    const bool  is_empty = list == NULL || list->last == NULL;

    restoreSigs(&sigset);
    return is_empty;
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
 * Returns the default destination for log messages, which depends on whether or
 * not log_avoid_stderr() has been called. If it hasn't been called, then the
 * default destination will be the standard error stream; otherwise, the default
 * destination will be that given by log_get_default_daemon_destination().
 *
 * @pre         Module is locked
 * @retval ""   Log to the system logging daemon
 * @retval "-"  Log to the standard error stream
 * @return      The pathname of the log file
 */
static const char* get_default_destination(void)
{
    return avoid_stderr
            ? log_get_default_daemon_destination()
            : "-";
}

/**
 * Initializes this logging module. Called by log_init() and log_reinit().
 *
 * @retval    -1  Failure
 * @retval     0  Success
 */
static int init(void)
{
    int status;
    if (isInitialized) {
        logl_internal(LOG_LEVEL_ERROR, "Logging module already initialized");
        status = -1;
    }
    else {
        (void)sigfillset(&all_sigset);
        (void)sigemptyset(&hup_sigset);
        (void)sigaddset(&hup_sigset, SIGHUP);
        hup_sigaction.sa_mask = hup_sigset;
        hup_sigaction.sa_flags = SA_RESTART;
        hup_sigaction.sa_handler = handle_sighup;
        (void)sigaction(SIGHUP, &hup_sigaction, &prev_hup_sigaction);
        status = mutex_init(&log_mutex, false, true);
        if (status)
            logl_internal(LOG_LEVEL_ERROR, "Couldn't initialize mutex: %s",
                    strerror(status));
    }
    return status;
}

/**
 * Indicates if a message at a given logging level would be logged.
 *
 * @param[in] level  The logging level
 * @retval    true   iff a message at level `level` would be logged
 */
static bool is_level_enabled(
        const log_level_t level)
{
    return logl_vet_level(level) && level >= log_level;
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
        logl_lock();

        if (is_level_enabled(level)) {
            for (const Message* msg = list->first; NULL != msg;
                    msg = msg->next) {
                logi_log(level, &msg->loc, msg->string);

                if (msg == list->last)
                    break;
            }                       /* message loop */
        }                           /* messages should be printed */

        logl_unlock();
        list_clear();
    }                               /* have messages */
    restoreSigs(&sigset);
}

/**
 * Refreshes the logging module. If logging is to the system logging daemon,
 * then it will continue to be. If logging is to a file, then the file is closed
 * and re-opened; thus enabling log file rotation. If logging is to the standard
 * error stream, then it will continue to be if log_avoid_stderr() hasn't been
 * called; otherwise, logging will be to the provider default. Should be called
 * after log_init().
 *
 * @pre        Module is locked
 * @retval  0  Success
 * @retval -1  Failure
 */
static int refresh(void)
{
    return logi_set_destination();
}

/******************************************************************************
 * Package-private API:
 ******************************************************************************/

/**
 * The persistent destination specification.
 */
char log_dest[_XOPEN_PATH_MAX];

/**
 *  Logging level.
 */
log_level_t log_level = LOG_LEVEL_NOTICE;

/**
 * The mapping from `log` logging levels to system logging daemon priorities:
 * Accessed by macros in `log_private.h`.
 */
int log_syslog_priorities[] = {
        LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING, LOG_ERR
};

/**
 * Returns the system logging priority associated with a logging level.
 *
 * @param[in] level    The logging level
 * @retval    LOG_ERR  `level` is invalid
 * @return             The system logging priority associated with `level`
 */
int logl_level_to_priority(
        const log_level_t level)
{
    return logl_vet_level(level) ? log_syslog_priorities[level] : LOG_ERR;
}

/**
 * Acquires this module's lock.
 *
 * This function is thread-safe.
 */
void logl_lock(void)
{
    int status = mutex_lock(&log_mutex);
    logl_assert(status == 0);
    if (refresh_needed) {
        refresh_needed = 0;
        (void)refresh();
    }
}

/**
 * Releases this module's lock.
 *
 * This function is thread-safe. On entry, this module's lock shall be locked
 * by the current thread.
 */
void logl_unlock(void)
{
    int status = mutex_unlock(&log_mutex);
    logl_assert(status == 0);
}

/**
 * Returns a pointer to the last component of a pathname.
 *
 * @param[in] pathname  The pathname.
 * @return              Pointer to the last component of the pathname.
 */
const char* logl_basename(
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
int logl_vadd(
        const log_loc_t* const restrict loc,
        const char* const restrict      fmt,
        va_list                         args)
{
    sigset_t sigset;
    blockSigs(&sigset);

    int status;

    if (NULL == fmt) {
        logl_internal(LOG_LEVEL_ERROR, "NULL argument");
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
int logl_add(
        const log_loc_t* const restrict loc,
        const char* const restrict      fmt,
                                        ...)
{
    sigset_t sigset;
    blockSigs(&sigset);
    va_list     args;
    va_start(args, fmt);
    int status = logl_vadd(loc, fmt, args);
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
int logl_add_errno(
        const log_loc_t* const loc,
        const int                errnum,
        const char* const        fmt,
                                 ...)
{
    sigset_t sigset;
    blockSigs(&sigset);
    int status = logl_add(loc, "%s", strerror(errnum));
    if (status == 0 && fmt && *fmt) {
        va_list     args;
        va_start(args, fmt);
        status = logl_vadd(loc, fmt, args);
        va_end(args);
    }
    restoreSigs(&sigset);
    return status;
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
void* logl_malloc(
        const char* const restrict file,
        const char* const restrict func,
        const int                  line,
        const size_t               nbytes,
        const char* const          msg)
{
    void* obj = malloc(nbytes);

    if (obj == NULL) {
        log_loc_t loc = {file, func, line};
        logl_add(&loc, "Couldn't allocate %lu bytes for %s", nbytes, msg);
    }

    return obj;
}

/**
 * Adds a variadic message to the current thread's list of messages. Emits and
 * then clears the list.
 *
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message or NULL.
 * @param[in] args    Optional format arguments.
 */
void logl_vlog(
        const log_loc_t* const restrict loc,
        const log_level_t               level,
        const char* const restrict      format,
        va_list                         args)
{
    if (format && *format) {
#if 1
        logl_vadd(loc, format, args);
    }
    flush(level);
#else
        StrBuf* sb = sb_get();
        if (sb == NULL) {
            logl_internal(LOG_LEVEL_ERROR,
                    "Couldn't get thread-specific string-buffer");
        }
        else if (sbPrintV(sb, format, args) == NULL) {
            logl_internal(LOG_LEVEL_ERROR,
                    "Couldn't format message into string-buffer");
        }
        else {
            logi_log(level, loc, sbString(sb));
        }
    }
#endif
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
void logl_log(
        const log_loc_t* const restrict loc,
        const log_level_t               level,
        const char* const restrict      format,
                                        ...)
{
    va_list args;
    va_start(args, format);
    logl_vlog(loc, level, format, args);
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
void logl_errno(
        const log_loc_t* const     loc,
        const int                  errnum,
        const char* const restrict fmt,
                                   ...)
{
    va_list args;
    va_start(args, fmt);
    logl_add(loc, "%s", strerror(errnum));
    logl_vlog(loc, LOG_LEVEL_ERROR, fmt, args);
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
void logl_flush(
        const log_loc_t* const loc,
        const log_level_t      level)
{
    if (!list_is_empty()) {
        /*
         * The following message is added so that the location of the call to
         * log_flush() is logged in case the call needs to be adjusted.
         */
        logl_add(loc, "Log messages flushed");
        flush(level);
    }
}

/**
 * Frees the log-message resources of the current thread. Should only be called
 * when no more logging by the current thread will occur.
 */
void
logl_free(void)
{
    sigset_t sigset;
    blockSigs(&sigset);

    List*   list = list_get();

    if (list) {
        if (list->last)
            logl_internal(LOG_LEVEL_WARNING,
                    "Loging message-queue isn't empty");
        list_fini(list);
        free(list);
        (void)pthread_setspecific(listKey, NULL);
    }

    restoreSigs(&sigset);
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Initializes this logging module.
 *
 * @param[in] id The pathname of the program (e.g., `argv[0]`). Caller may free.
 * @retval    -1  Failure
 * @retval     0  Success
 */
int log_init(
        const char* const id)
{
    int status = init();
    if (status == 0) {
        // The following isn't done by log_reinit():
        init_thread = pthread_self();
        avoid_stderr = (fcntl(STDERR_FILENO, F_GETFD) == -1);
        log_level = LOG_LEVEL_NOTICE;
        const char* const dest = get_default_destination();
        // `dest` doesn't overlap `log_dest`
        strncpy(log_dest, dest, sizeof(log_dest))[sizeof(log_dest)-1] = 0;
        status = logi_init(id);
        if (status)
            logl_internal(LOG_LEVEL_ERROR,
                    "Couldn't initialize implementation");
        isInitialized = (status == 0);
    }
    return status;
}

/**
 * Tells this module to avoid using the standard error stream (because the
 * process has become a daemon, for example).
 */
void log_avoid_stderr(void)
{
    logl_lock();
    avoid_stderr = true;
    if (LOG_IS_STDERR_SPEC(log_dest)) { // Don't change if unnecessary
        /*
         * log_get_default_daemon_destination() doesn't lock and the string it
         * returns doesn't overlap `log_dest`.
         */
        const char* const dest = log_get_default_daemon_destination();
        strncpy(log_dest, dest, sizeof(log_dest))[sizeof(log_dest)-1] = 0;
        (void)logi_set_destination();
    }
    logl_unlock();
}

/**
 * Re-initializes this logging module based on its state just prior to calling
 * log_fini().
 *
 * @retval    -1  Failure
 * @retval     0  Success
 */
int log_reinit(void)
{
    int status = init();
    if (status == 0) {
        status = logi_reinit();
        isInitialized = (status == 0);
    }
    return status;
}

/**
 * Refreshes the logging module. If logging is to the system logging daemon,
 * then it will continue to be. If logging is to a file, then the file is closed
 * and re-opened; thus enabling log file rotation. If logging is to the standard
 * error stream, then it will continue to be if log_avoid_stderr() hasn't been
 * called; otherwise, logging will be to the provider default.
 *
 * This function is async-signal-safe.
 */
void log_refresh(void)
{
    refresh_needed = 1;
}

/**
 * Sets the logging identifier. Should be called after `log_init()`.
 *
 * @param[in] id        The new identifier. Caller may free.
 * @retval    0         Success.
 * @retval    -1        Failure.
 */
int log_set_id(
        const char* const id)
{
    int status;
    if (id == NULL) {
        status = -1;
    }
    else {
        logl_lock();
        status = logi_set_id(id);
        logl_unlock();
    }
    return status;
}

/**
 * Modifies the logging identifier. Should be called after log_init().
 *
 * @param[in] hostId    The identifier of the remote host. Caller may free.
 * @param[in] isFeeder  Whether or not the process is sending data-products or
 *                      notifications.
 * @param[in] id        The logging identifier. Caller may free.
 * @retval    0         Success.
 * @retval    -1        Failure.
 */
int log_set_upstream_id(
        const char* const hostId,
        const bool        isFeeder)
{
    int status;
    if (hostId == NULL) {
        status = -1;
    }
    else {
        char id[_POSIX_HOST_NAME_MAX + 6 + 1]; // hostname + "(type)" + 0
        (void)snprintf(id, sizeof(id), "%s(%s)", hostId,
                isFeeder ? "feed" : "noti");
        id[sizeof(id)-1] = 0;
        logl_lock();
        status = logi_set_id(id);
        logl_unlock();
    }
    return status;
}

/**
 * Returns the default destination for log messages, which depends on whether or
 * not log_avoid_stderr() has been called. If it hasn't been called, then the
 * default destination will be the standard error stream; otherwise, the default
 * destination will be that given by log_get_default_daemon_destination().
 *
 * @retval ""   The system logging daemon
 * @retval "-"  The standard error stream
 * @return      The pathname of the log file
 */
const char* log_get_default_destination(void)
{
    logl_lock();
    const char* dest = get_default_destination();
    logl_unlock();
    return dest;
}

/**
 * Sets the logging destination. Should be called between log_init() and
 * log_fini().
 *
 * @param[in] dest     The logging destination. Caller may free. One of <dl>
 *                         <dt>""   <dd>The system logging daemon.
 *                         <dt>"-"  <dd>The standard error stream.
 *                         <dt>else <dd>The file whose pathname is `dest`.
 *                     </dl>
 * @retval    0        Success.
 * @retval    -1       Failure.
 */
int log_set_destination(
        const char* const dest)
{
    int status;
    if (dest == NULL) {
        status = -1;
    }
    else {
        logl_lock();
        /*
         * Handle potential overlap because log_get_destination() returns
         * `log_dest`.
         */
        size_t nbytes = strlen(dest);
        if (nbytes > sizeof(log_dest)-1)
            nbytes = sizeof(log_dest)-1;
        (void)memmove(log_dest, dest, nbytes);
        log_dest[nbytes] = 0;
        status = logi_set_destination();
        logl_unlock();
    }
    return status;
}

/**
 * Returns the logging destination. Should be called between log_init() and
 * log_fini().
 *
 * @return       The logging destination. One of <dl>
 *                   <dt>""      <dd>The system logging daemon.
 *                   <dt>"-"     <dd>The standard error stream.
 *                   <dt>else    <dd>The pathname of the log file.
 *               </dl>
 */
const char* log_get_destination(void)
{
    logl_lock();
    const char* path = log_dest;
    logl_unlock();
    return path;
}

/**
 * Enables logging down to a given level. Should be called after log_init().
 *
 * @param[in] level  The lowest level through which logging should occur.
 * @retval    0      Success.
 * @retval    -1     Failure.
 */
int log_set_level(
        const log_level_t level)
{
    int status;
    if (!logl_vet_level(level)) {
        status = -1;
    }
    else {
        logl_lock();
        log_level = level;
        logi_set_level();
        logl_unlock();
        status = 0;
    }
    return status;
}

/**
 * Lowers the logging threshold by one. Wraps at the bottom.
 */
void log_roll_level(void)
{
    logl_lock();
    log_level = (log_level == LOG_LEVEL_DEBUG)
            ? LOG_LEVEL_ERROR
            : log_level - 1;
    logl_unlock();
}

/**
 * Returns the current logging level.
 *
 * @return The lowest level through which logging will occur.
 */
log_level_t log_get_level(void)
{
    logl_lock(); // For visibility of changes
    log_level_t level = log_level;
    logl_unlock();
    return level;
}

/**
 * Indicates if a message at a given logging level would be logged.
 *
 * @param[in] level  The logging level
 * @retval    true   iff a message at level `level` would be logged
 */
bool log_is_level_enabled(
        const log_level_t level)
{
    logl_lock();
    int enabled = is_level_enabled(level);
    logl_unlock();
    return enabled;
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
    logl_lock();
    logl_free();
    logl_unlock();
}

/**
 * Finalizes the logging module. Frees all thread-specific resources. Frees all
 * thread-independent resources if the current thread is the one on which
 * log_init() was called.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
int log_fini(void)
{
    int status;
    if (!isInitialized) {
        // Can't log an error message because not initialized
        status = -1;
    }
    else {
        logl_lock();
        logl_free();
        if (!pthread_equal(init_thread, pthread_self())) {
            status = 0;
        }
        else {
            status = logi_fini();
            if (status == 0) {
                logl_unlock();
                status = mutex_fini(&log_mutex);
                if (status == 0) {
                    (void)sigaction(SIGHUP, &prev_hup_sigaction, NULL);
                    isInitialized = false;
                }
            }
        }
    }
    return status ? -1 : 0;
}
