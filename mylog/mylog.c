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
 * @file   mylog.c
 * @author Steven R. Emmerson
 *
 * REQUIREMENTS:
 *   - Can log to
 *     - System logging daemon (-l '')
 *     - Standard error stream (-l -) if not a daemon
 *     - File (-l _pathname_)
 *   - Default output
 *     - If daemon
 *       - If backward-compatible: system logging daemon
 *       - If not backward-compatible: standard LDM log file
 *     - Otherwise, standard error stream
 *   - Output format consistent with earlier logging except for timestamp
 *   - Timestamp format
 *     - Chosen by system logging daemon when used
 *     - Otherwise, in ISO 8601 format with microsecond precision and UTC
 *       timezone (<em>YYYYMMDD</em>T<em>hhmmss.uuuuuu</em>Z)
 *   - Allow log file rotation via SIGHUP handling
 *   - Pathname of standard LDM log file configurable with session resolution,
 *     at least
 */
#include <config.h>

#undef NDEBUG
#include "mylog.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
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
static pthread_key_t    listKey;

/**
 * Whether or not the thread-specific key has been created.
 */
static pthread_once_t   key_creation_control = PTHREAD_ONCE_INIT;

/**
 * The mutex that makes this module thread-safe.
 */
static pthread_mutex_t  mutex = PTHREAD_MUTEX_INITIALIZER;

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
    sigset_t sigset;

    (void)sigfillset(&sigset);
    (void)pthread_sigmask(SIG_BLOCK, &sigset, prevset);
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
 * Locks this module's lock.
 *
 * This function is thread-safe. On entry, this module's lock shall be
 * unlocked.
 */
static void lock(void)
{
    int status = pthread_mutex_lock(&mutex);
    mylog_assert(status == 0);
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
    mylog_assert(status == 0);
}

static void create_key(void)
{
    int status = pthread_key_create(&listKey, NULL);

    if (status != 0) {
        mylog_internal("pthread_key_create() failure");
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
            mylog_internal("malloc() failure");
        }
        else {
            int status = pthread_setspecific(listKey, list);
            if (status != 0) {
                mylog_internal("pthread_setspecific() failure");
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
        mylog_internal("malloc() failure");
    }
    else {
        #define LOG_DEFAULT_STRING_SIZE     256
        char*   string = (char*)malloc(LOG_DEFAULT_STRING_SIZE);
        if (NULL == string) {
            status = errno;
            mylog_internal("malloc() failure");
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
        mylog_internal("vsnprintf() failure");
    }
    else {
        // The buffer is too small for the message. Expand it.
        size_t  size = nbytes + 1;
        char*   string = (char*)malloc(size);

        if (NULL == string) {
            status = errno;
            mylog_internal("malloc() failure");
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
 * Returns a pointer to the last component of a pathname.
 *
 * @param[in] pathname  The pathname.
 * @return              Pointer to the last component of the pathname.
 */
const char* mylog_basename(
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
int mylog_vadd_located(
        const mylog_loc_t* const restrict loc,
        const char* const restrict        fmt,
        va_list                           args)
{
    sigset_t sigset;
    blockSigs(&sigset);

    int status;

    if (NULL == fmt) {
        mylog_internal("NULL argument");
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
                msg->loc = *loc;
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
int mylog_add_located(
        const mylog_loc_t* const restrict loc,
        const char* const restrict        fmt,
                                          ...)
{
    sigset_t sigset;
    blockSigs(&sigset);
    va_list     args;
    va_start(args, fmt);
    int status = mylog_vadd_located(loc, fmt, args);
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
int mylog_add_errno_located(
        const mylog_loc_t* const loc,
        const int                errnum,
        const char* const        fmt,
                                 ...)
{
    sigset_t sigset;
    blockSigs(&sigset);
    int status = mylog_add_located(loc, "%s", strerror(errnum));
    if (status == 0 && fmt && *fmt) {
        va_list     args;
        va_start(args, fmt);
        status = mylog_vadd_located(loc, fmt, args);
        va_end(args);
    }
    restoreSigs(&sigset);
    return status;
}

/**
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-list for the current thread.
 *
 * @param[in] level  The level at which to log the messages. One of MYLOG_LEVEL_ERROR,
 *                   MYLOG_LEVEL_WARNING, MYLOG_LEVEL_NOTICE, MYLOG_LEVEL_INFO,
 *                   or MYLOG_LEVEL_DEBUG; otherwise, the behavior is
 *                   undefined.
 */
void mylog_flush(
    const mylog_level_t level)
{
    sigset_t sigset;
    blockSigs(&sigset);

    List*   list = list_get();

    if (NULL != list && NULL != list->last) {
        lock();

        if (mylog_is_level_enabled(level)) {
            for (const Message* msg = list->first; NULL != msg;
                    msg = msg->next) {
                mylog_write_one(level, msg);

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
 * Clears the message-list of the current thread.
 */
void
mylog_clear(void)
{
    list_clear();
}

/**
 * Frees the log-message resources of the current thread. Should only be called
 * when no more logging by the current thread will occur.
 */
void
mylog_free(void)
{
    sigset_t sigset;
    blockSigs(&sigset);

    List*   list = list_get();

    if (list) {
        if (list->last)
            mylog_error("%s() called with pending messages", __func__);
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
void* mylog_malloc_located(
        const char* const restrict file,
        const char* const restrict func,
        const int                  line,
        const size_t               nbytes,
        const char* const          msg)
{
    void* obj = malloc(nbytes);

    if (obj == NULL) {
        mylog_loc_t loc = {file, line};
        mylog_add_located(&loc, "Couldn't allocate %lu bytes for %s", nbytes, msg);
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
void mylog_vlog_located(
        const mylog_loc_t* const restrict loc,
        const mylog_level_t               level,
        const char* const restrict        format,
        va_list                           args)
{
    if (format && *format)
        mylog_vadd_located(loc, format, args);
    mylog_flush(level);
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
void mylog_log_located(
        const mylog_loc_t* const restrict loc,
        const mylog_level_t               level,
        const char* const restrict        format,
                                          ...)
{
    va_list args;
    va_start(args, format);
    mylog_vlog_located(loc, level, format, args);
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
void mylog_errno_located(
        const mylog_loc_t* const   loc,
        const int                  errnum,
        const char* const restrict fmt,
                                   ...)
{
    va_list args;
    va_start(args, fmt);
    mylog_add_located(loc, "%s", strerror(errnum));
    mylog_vlog_located(loc, MYLOG_LEVEL_ERROR, fmt, args);
    va_end(args);
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
        mylog_internal(MYLOG_LEVEL_ERROR, "malloc() failure");

    return obj;
}
#endif
