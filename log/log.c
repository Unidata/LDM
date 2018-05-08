/**
 *   Copyright 2016, University Corporation for Atmospheric Research. All rights
 *   reserved. See the file COPYRIGHT in the top-level source-directory for
 *   licensing conditions.
 *
 * This file implements the the provider-independent `log.h` API. It provides
 * for accumulating log-messages into a thread-specific queue and the logging of
 * that queue at a single logging level.
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
 *         - _location_: _file_:_func()_:_line_
 *       - Example: 20160113T150106.734013Z noaaportIngester[26398] NOTE process_prod.c:process_prod():216 SDUS58 PACR 062008 /pN0RABC inserted
 *   - Enable log file rotation by refreshing destination upon SIGUSR1 reception
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
#include <sys/stat.h>
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
 * A queue of log messages.
 */
typedef struct msg_queue {
    Message*    first;
    Message*    last;           /* NULL => empty queue */
} msg_queue_t;

/**
 * Key for the thread-specific queue of log messages.
 */
static pthread_key_t         queueKey;
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
 * The SIGUSR1 signal set.
 */
static sigset_t              usr1_sigset;
/**
 * The SIGUSR1 action for this module.
 */
static struct sigaction      usr1_sigaction;
/**
 * The previous SIGUSR1 action when this module's SIGUSR1 action is registered.
 */
static struct sigaction      prev_usr1_sigaction;
/**
 * The signal mask of all signals.
 */
static sigset_t              all_sigset;
/**
 * The signal-mask prior to calling logl_lock() so that this module is
 * async-signal-safe (i.e., can be called safely from a signal-catching
 * function).
 */
static sigset_t              prevSigs;
/**
 * The mutex that makes this module thread-safe.
 */
static mutex_t               log_mutex;

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
 * Handles SIGUSR1 delivery. Sets variable `refresh_needed` and ensures that any
 * previously-registered SIGUSR1 handler is called.
 *
 * @param[in] sig  SIGUSR1.
 */
static void handle_sigusr1(
        const int sig)
{
    refresh_needed = 1;
    if (prev_usr1_sigaction.sa_handler != SIG_DFL &&
            prev_usr1_sigaction.sa_handler != SIG_IGN) {
        (void)sigaction(SIGUSR1, &prev_usr1_sigaction, NULL);
        raise(SIGUSR1);
        (void)sigprocmask(SIG_UNBLOCK, &usr1_sigset, NULL);
        (void)sigaction(SIGUSR1, &usr1_sigaction, NULL);
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
 * Prints a message into a message-queue entry.
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

static void queue_create_key(void)
{
    int status = pthread_key_create(&queueKey, NULL);
    if (status != 0) {
        logl_internal(LOG_LEVEL_ERROR, "pthread_key_create() failure: "
                "errno=\"%s\"", strerror(status));
        abort();
    }
}

/**
 * Returns the current thread's message-queue.
 *
 * This function is thread-safe. On entry, this module's lock shall be
 * unlocked.
 *
 * @return  The message-queue of the current thread or NULL if the message-queue
 *          doesn't exist and couldn't be created.
 */
static msg_queue_t* queue_get(void)
{
    /**
     * Whether or not the thread-specific queue key has been created.
     */
    static pthread_once_t key_creation_control = PTHREAD_ONCE_INIT;
    (void)pthread_once(&key_creation_control, queue_create_key);
    msg_queue_t* queue = pthread_getspecific(queueKey);
    if (NULL == queue) {
        queue = malloc(sizeof(msg_queue_t));
        if (NULL == queue) {
            logl_internal(LOG_LEVEL_ERROR, "malloc() failure: size=%zu",
                    sizeof(msg_queue_t));
        }
        else {
            int status = pthread_setspecific(queueKey, queue);
            if (status != 0) {
                logl_internal(LOG_LEVEL_ERROR, "pthread_setspecific() failure: "
                        "errno=\"%s\"", strerror(status));
                free(queue);
                queue = NULL;
            }
            else {
                queue->first = NULL;
                queue->last = NULL;
            }
        }
    }
    return queue;
}

/**
 * Indicates if a given message queue is empty.
 *
 * @param[in] queue  The message queue.
 * @retval    true   iff `queue == NULL` or the queue is empty
 */
static bool queue_is_empty(
        msg_queue_t* const queue)
{
    return queue == NULL || queue->last == NULL;
}

/**
 * Indicates if the message queue of the current thread is empty.
 *
 * @retval true iff the queue is empty
 */
static bool logl_is_queue_empty()
{
    sigset_t sigset;
    blockSigs(&sigset);

    msg_queue_t* const queue = queue_get();
    const bool  is_empty = queue_is_empty(queue);

    restoreSigs(&sigset);
    return is_empty;
}

/**
 * Returns the next unused entry in a message-queue. Creates it if necessary.
 *
 * @param[in] queue   The message-queue.
 * @param[in] entry   The next unused entry.
 * @retval    0       Success. `*entry` is set.
 * @retval    ENOMEM  Out-of-memory. Error message logged.
 */
static int queue_getNextEntry(
        msg_queue_t* const restrict queue,
        Message** const restrict    entry)
{
    Message* msg = (NULL == queue->last) ? queue->first : queue->last->next;
    int      status;

    if (msg != NULL) {
        *entry = msg;
        status = 0;
    }
    else {
        status = msg_new(&msg);
        if (status == 0) {
            if (NULL == queue->first)
                queue->first = msg;  /* very first message */
            if (NULL != queue->last)
                queue->last->next = msg;
            *entry = msg;
        } // `msg` allocated
    } // need new message structure

    return status;
}

/**
 * Clears the accumulated log-messages of the current thread.
 */
static void queue_clear()
{
    sigset_t sigset;
    blockSigs(&sigset);

    msg_queue_t*   queue = queue_get();

    if (NULL != queue)
        queue->last = NULL;

    restoreSigs(&sigset);
}

/**
 * Destroys a queue of log-messages.
 *
 * @param[in] queue  The queue of messages.
 */
static void
queue_fini(
        msg_queue_t* const queue)
{
    Message* msg;
    Message* next;

    for (msg = queue->first; msg; msg = next) {
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
 * Initializes this logging module. Called by log_init().
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
        (void)sigemptyset(&usr1_sigset);
        (void)sigaddset(&usr1_sigset, SIGUSR1);
        usr1_sigaction.sa_mask = usr1_sigset;
        usr1_sigaction.sa_flags = SA_RESTART;
        usr1_sigaction.sa_handler = handle_sigusr1;
        (void)sigaction(SIGUSR1, &usr1_sigaction, &prev_usr1_sigaction);
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
 * @param[in] level    The logging level
 * @retval    true     iff a message at level `level` would be logged
 * @asyncsignalsafety  Safe
 */
static bool is_level_enabled(
        const log_level_t level)
{
    return logl_vet_level(level) && level >= log_level;
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
static inline int refresh_if_necessary(void)
{
    int status = 0;
    if (refresh_needed) {
        status = logi_set_destination();
        refresh_needed = 0;
    }
    return status;
}

/**
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-queue for the current thread.
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
    msg_queue_t*   queue = queue_get();

    if (NULL != queue && NULL != queue->last) {
        if (is_level_enabled(level)) {
            (void)refresh_if_necessary();
            for (const Message* msg = queue->first; NULL != msg;
                    msg = msg->next) {
                logi_log(level, &msg->loc, msg->string);

                if (msg == queue->last)
                    break;
            }                       /* message loop */
            logi_flush();
        }                           /* messages should be printed */

        queue_clear();
    }                               /* have messages */
    restoreSigs(&sigset);
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
volatile log_level_t log_level = LOG_LEVEL_NOTICE;

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
 * This function is thread-safe and async-signal-safe.
 */
void logl_lock(void)
{
    /*
     * Because this module calls async-signal-unsafe functions (e.g.,
     * pthread_mutex_lock()), the current thread's signal-mask is set to block
     * most signals so that this module's functions can be called from a
     * signal-catching function.
     */
    blockSigs(&prevSigs);
    int status = mutex_lock(&log_mutex);
    logl_assert(status == 0);
}

/**
 * Releases this module's lock.
 *
 * This function is thread-safe and async-signal-safe.
 *
 * @pre This module's lock shall be locked on the current thread.
 */
void logl_unlock(void)
{
    int status = mutex_unlock(&log_mutex);
    logl_assert(status == 0);
    restoreSigs(&prevSigs);
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
 * Tries to return a formatted variadic message.
 * @param[out] msg          Formatted message. Caller should free if the
 *                          returned number of bytes is less than the estimated
 *                          number of bytes.
 * @param[in]  nbytesGuess  Estimated size of message in bytes -- *including*
 *                          the terminating NUL
 * @param[in]  format       Message format
 * @param[in]  args         Optional argument of format
 * @retval     -1           Out-of-memory. `logl_internal()` called.
 * @return                  Number of bytes necessary to contain the formatted
 *                          message -- *excluding* the terminating NUL. If
 *                          greater than or equal to the estimated number of
 *                          bytes, then `*msg` isn't set.
 */
static ssize_t tryFormatingMsg(
        char** const      msg,
        const ssize_t     nbytesGuess,
        const char* const format,
        va_list           args)
{
    ssize_t nbytes;
    char*   buf = malloc(nbytesGuess);
    if (buf == NULL) {
        logl_internal(LOG_LEVEL_ERROR, "Couldn't allocate %ld-byte message "
                "buffer", (long)nbytesGuess);
        nbytes = -1;
    }
    else {
        nbytes = vsnprintf(buf, nbytesGuess, format, args);
        if (nbytes >= nbytesGuess) {
            free(buf);
        }
        else {
            *msg = buf;
        }
    }
    return nbytes;
}

/**
 * Returns a a formated variadic message.
 * @param[in] format  Message format
 * @param[in] args    Optional format arguments
 * @retval    NULL    Out-of-memory. `logl_internal()` called.
 * @return            Allocated string of formatted message. Caller should free
 *                    when it's no longer needed.
 */
static char* formatMsg(
        const char* format,
        va_list     args)
{
    va_list argsCopy;
    va_copy(argsCopy, args);
    char*   msg = NULL;
    ssize_t nbytes = tryFormatingMsg(&msg, 256, format, args);
    if (nbytes >= 256)
        (void)tryFormatingMsg(&msg, nbytes+1, format, argsCopy);
    va_end(argsCopy);
    return msg;
}

void logl_vlog_1(
        const log_loc_t* const  loc,
        const log_level_t       level,
        const char* const       format,
        va_list                 args)
{
    if (is_level_enabled(level)) {
        sigset_t sigset;
        blockSigs(&sigset);
        char* msg = formatMsg(format, args);
        if (msg) {
            (void)refresh_if_necessary();
            logi_log(level, loc, msg);
            free(msg);
        }
        restoreSigs(&sigset);
    } // Message should be logged
}

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
        msg_queue_t* queue = queue_get();
        if (NULL == queue) {
            status = ENOMEM;
        }
        else {
            Message* msg;
            status = queue_getNextEntry(queue, &msg);
            if (status == 0) {
                loc_init(&msg->loc, loc);
                status = msg_format(msg, fmt, args);
                if (status == 0)
                    queue->last = msg;
            } // have a message structure
        } // message-queue isn't NULL
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
    va_list  args;
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
        const char* const msg)
{
    void* obj = realloc(buf, nbytes);
    if (obj == NULL) {
        log_loc_t loc = {file, func, line};
        logl_add(&loc, "Couldn't re-allocate %lu bytes for %s", nbytes, msg);
    }
    return obj;
}

/**
 * Adds a variadic message to the current thread's queue of messages. Logs and
 * then clears the queue.
 *
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message or NULL.
 * @param[in] args    Optional format arguments.
 */
void logl_vlog_q(
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
 * Logs a single message.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message or NULL.
 * @param[in] ...     Optional format arguments.
 */
void logl_log_1(
        const log_loc_t* const restrict loc,
        const log_level_t               level,
        const char* const restrict      format,
                                        ...)
{
    va_list args;
    va_start(args, format);
    logl_vlog_1(loc, level, format, args);
    va_end(args);
}

/**
 * Adds a message to the current thread's queue of messages. Logs and then
 * clears the queue.
 *
 * @param[in] loc     Location where the message was generated.
 * @param[in] level   Logging level.
 * @param[in] format  Format of the message or NULL.
 * @param[in] ...     Optional format arguments.
 */
void logl_log_q(
        const log_loc_t* const restrict loc,
        const log_level_t               level,
        const char* const restrict      format,
                                        ...)
{
    va_list args;
    va_start(args, format);
    logl_vlog_q(loc, level, format, args);
    va_end(args);
}

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
        const log_loc_t* const     loc,
        const int                  errnum,
        const char* const restrict fmt,
                                   ...)
{
    va_list args;
    va_start(args, fmt);
    logl_add(loc, "%s", strerror(errnum));
    logl_vlog_q(loc, LOG_LEVEL_ERROR, fmt, args);
    va_end(args);
}

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
        const log_level_t      level)
{
    if (!logl_is_queue_empty()) {
        /*
         * The following message is added so that the location of the call to
         * log_flush() is logged in case the call needs to be adjusted.
        logl_add(loc, "Log messages flushed");
         */
        flush(level);
    }
}

/**
 * Frees the log-message resources of the current thread. Should only be called
 * when no more logging by the current thread will occur.
 *
 * @pre This module is locked
 */
void logl_free(
        const log_loc_t* const loc)
{
    msg_queue_t* queue = queue_get();
    if (!queue_is_empty(queue)) {
        logl_log_q(loc, LOG_LEVEL_WARNING,
                "logl_free() called with the above messages still in the "
                "message-queue");
    }
    if (queue) {
        queue_fini(queue);
        free(queue);
        (void)pthread_setspecific(queueKey, NULL);
    }
}

/**
 * Finalizes the logging module. Frees all thread-specific resources. Frees all
 * thread-independent resources if the current thread is the one on which
 * log_init() was called.
 *
 * @retval -1  Failure
 * @retval  0  Success
 */
int logl_fini(
        const log_loc_t* const loc)
{
    int status;
    if (!isInitialized) {
        // Can't log an error message because not initialized
        status = -1;
    }
    else {
        logl_free(loc);
        if (!pthread_equal(init_thread, pthread_self())) {
            status = 0;
        }
        else {
            status = logi_fini();
        }
    }
    return status ? -1 : 0;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

/**
 * Indicates if the standard error file descriptor refers to a file that is not
 * `/dev/null`. This function may be called at any time.
 *
 * @retval true   Standard error file descriptor refers to a file that is not
 *               `/dev/null`
 * @retval false  Standard error file descriptor is closed or refers to
 *                `/dev/null`.
 */
bool log_is_stderr_useful(void)
{
    static struct stat dev_null_stat;
    static bool        initialized = false;
    if (!initialized) {
        (void)stat("/dev/null", &dev_null_stat); // Can't fail
        initialized = true;
    }
    struct stat stderr_stat;
    return (fstat(STDERR_FILENO, &stderr_stat) == 0) &&
        ((stderr_stat.st_ino != dev_null_stat.st_ino) ||
                (stderr_stat.st_dev != dev_null_stat.st_dev));
}

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
        log_level = LOG_LEVEL_NOTICE;
        (void)strncpy(log_dest, STDERR_SPEC, sizeof(log_dest));
        status = logi_init(id);
        if (status == 0) {
            init_thread = pthread_self();
            avoid_stderr = !log_is_stderr_useful();
            const char* const dest = get_default_destination();
            // `dest` doesn't overlap `log_dest`
            strncpy(log_dest, dest, sizeof(log_dest))[sizeof(log_dest)-1] = 0;
            status = logi_set_destination();
            isInitialized = status == 0;
        }
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
        /*
         * Handle potential overlap because log_get_destination() returns
         * `log_dest`.
         */
        size_t nbytes = strlen(dest);
        if (nbytes > sizeof(log_dest)-1)
            nbytes = sizeof(log_dest)-1;
        logl_lock();
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
    logi_set_level();
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
    bool enabled = is_level_enabled(level);
    logl_unlock();
    return enabled;
}

/**
 * Clears the message-queue of the current thread.
 */
void
log_clear(void)
{
    queue_clear();
}

/**
 * Frees the log-message resources of the current thread. Should only be called
 * when no more logging by the current thread will occur.
 */
void
log_free_located(
        const log_loc_t* const loc)
{
    logl_lock();
    logl_free(loc);
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
int log_fini_located(
        const log_loc_t* const loc)
{
    logl_lock();
    int status = logl_fini(loc);
    logl_unlock();
    if (status == 0) {
        status = mutex_fini(&log_mutex);
        if (status == 0) {
            (void)sigaction(SIGUSR1, &prev_usr1_sigaction, NULL);
            isInitialized = false;
        }
    }
    return status ? -1 : 0;
}
