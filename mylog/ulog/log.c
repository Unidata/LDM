/**
 *   Copyright © 2014, University Corporation for Atmospheric Research
 *   See file ../COPYRIGHT for copying and redistribution conditions.
 *
 * Provides for the accumulation of log-messages and the printing of all,
 * accumlated log-messages at a single priority.
 *
 * This module uses the ulog(3) module.
 *
 * This module is thread-safe.
 *
 * @author Steven R. Emmerson
 */
#include <config.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>   /* NULL */
#include <stdio.h>    /* vsnprintf(), snprintf() */
#include <stdlib.h>   /* malloc(), free(), abort() */
#include <string.h>
#include <unistd.h>

#include "log.h"


/**
 * A log-message.  Such structures accumulate in a thread-specific message-list.
 */
typedef struct message {
    struct message*     next;   /**< pointer to next message */
    char*               string; /**< message buffer */
#define LOG_DEFAULT_STRING_SIZE     256
    size_t              size;   /**< size of message buffer */
} Message;

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
static int              keyCreated = 0;

/**
 * The mutex that makes this module thread-safe and is also used to make use
 * of the ulog(3) module thread-safe.
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
    if (pthread_mutex_lock(&mutex) != 0)
        serror("Couldn't lock logging mutex");
}

/**
 * Unlocks this module's lock.
 *
 * This function is thread-safe. On entry, this module's lock shall be locked
 * by the current thread.
 */
static void unlock(void)
{
    if (pthread_mutex_unlock(&mutex) != 0)
        serror("Couldn't unlock logging mutex");
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
static List* getList(void)
{
    List*   list;
    int     status;

    lock();

    if (!keyCreated) {
        status = pthread_key_create(&listKey, NULL);

        if (status != 0) {
            serror("getList(): pthread_key_create() failure: %s",
                    strerror(status));
        }
        else {
            keyCreated = 1;
        }
    }

    unlock();

    if (!keyCreated) {
        list = NULL;
    }
    else {
        list = pthread_getspecific(listKey);

        if (NULL == list) {
            list = (List*)malloc(sizeof(List));

            if (NULL == list) {
                lock();
                serror("getList(): malloc() failure");
                unlock();
            }
            else {
                if ((status = pthread_setspecific(listKey, list)) != 0) {
                    lock();
                    serror("getList(): pthread_setspecific() failure: %s",
                        strerror(status));
                    unlock();
                    free(list);

                    list = NULL;
                }
                else {
                    list->first = NULL;
                    list->last = NULL;
                }
            }
        }
    }

    return list;
}


/******************************************************************************
 * Public API:
 ******************************************************************************/


/**
 * Clears the accumulated log-messages of the current thread.
 */
void log_clear()
{
    sigset_t sigset;
    blockSigs(&sigset);

    List*   list = getList();

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
    Message* msg = (Message*)malloc(sizeof(Message));
    int      status;

    if (msg == NULL) {
        status = errno;

        lock();
        serror("log_vadd(): malloc(%lu) failure",
            (unsigned long)sizeof(Message));
        unlock();
    }
    else {
        char*   string = (char*)malloc(LOG_DEFAULT_STRING_SIZE);

        if (NULL == string) {
            status = errno;

            lock();
            serror("log_vadd(): malloc(%lu) failure",
                (unsigned long)LOG_DEFAULT_STRING_SIZE);
            unlock();
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
static int log_getNextEntry(
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
 * Prints a message into a message-list entry.
 *
 * @param[in] msg        The message entry.
 * @param[in] fmt        The message format.
 * @param[in] args       The arguments to be formatted.
 * @retval    0          Success. The message has been written into `*msg`.
 * @retval    EAGAIN     The character buffer was too small. Try again.
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
        lock();
        serror("log_vadd(): vsnprintf() failure");
        unlock();
    }
    else {
        // The buffer is too small for the message. Expand it.
        size_t  size = nbytes + 1;
        char*   string = (char*)malloc(size);

        if (NULL == string) {
            status = errno;
            lock();
            serror("log_vadd(): malloc(%lu) failure", (unsigned long)size);
            unlock();
        }
        else {
            free(msg->string);
            msg->string = string;
            msg->size = size;
            status = EAGAIN;
        }
    }                           /* buffer is too small */

    return status;
}

/**
 * Adds a variadic log-message to the message-list for the current thread.
 *
 * @param[in] fmt       Formatting string.
 * @param[in] args      Formatting arguments.
 * @retval 0            Success
 * @retval EAGAIN       The character buffer was too small. Try again.
 * @retval EINVAL       `fmt` or `args` is `NULL`. Error message logged.
 * @retval EINVAL       There are insufficient arguments. Error message logged.
 * @retval EILSEQ       A wide-character code that doesn't correspond to a
 *                      valid character has been detected. Error message logged.
 * @retval ENOMEM       Out-of-memory. Error message logged.
 * @retval EOVERFLOW    The length of the message is greater than {INT_MAX}.
 *                      Error message logged.
 */
int log_vadd(
    const char* const   fmt,  /**< The message format */
    va_list             args) /**< The arguments referenced by the format. */
{
    sigset_t sigset;
    blockSigs(&sigset);

    int status;

    if (NULL == fmt) {
        lock();
        uerror("log_vadd(): NULL argument");
        unlock();
        status = EINVAL;
    }
    else {
        List* list = getList();
        if (NULL == list) {
            status = ENOMEM;
        }
        else {
            Message* msg;
            status = log_getNextEntry(list, &msg);
            if (status == 0) {
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
 * Sets the first log-message for the current thread.
 */
void log_start(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    sigset_t sigset;
    blockSigs(&sigset);

    va_list     args;

    log_clear();
    va_start(args, fmt);

    if (EAGAIN == log_vadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)log_vadd(fmt, args);
    }

    va_end(args);
    restoreSigs(&sigset);
}

/**
 * Adds a log-message for the current thread.
 *
 * @param[in] fmt  Formatting string for the message.
 * @param[in] ...  Arguments for the formatting string.
 */
void log_add(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    sigset_t sigset;
    blockSigs(&sigset);

    va_list     args;

    va_start(args, fmt);

    if (EAGAIN == log_vadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)log_vadd(fmt, args);
    }

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

    if (EAGAIN == log_vadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)log_vadd(fmt, args);
    }

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

        if (EAGAIN == log_vadd(fmt, args)) {
            va_end(args);
            va_start(args, fmt);
            (void)log_vadd(fmt, args);
        }

        va_end(args);
    }

    restoreSigs(&sigset);
}

/**
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-list for the current thread.
 */
void log_log(
    const int   level)  /**< The level at which to log the messages.  One of
                          *  LOG_ERR, LOG_WARNING, LOG_NOTICE, LOG_INFO, or
                          *  LOG_DEBUG; otherwise, the behavior is undefined. */
{
    sigset_t sigset;
    blockSigs(&sigset);

    List*   list = getList();

    if (NULL != list && NULL != list->last) {
        static const unsigned       allPrioritiesMask = 
            LOG_MASK(LOG_ERR) |
            LOG_MASK(LOG_WARNING) |
            LOG_MASK(LOG_NOTICE) | 
            LOG_MASK(LOG_INFO) |
            LOG_MASK(LOG_DEBUG);
        const int                   priorityMask = LOG_MASK(level);

        lock();

        if ((priorityMask & allPrioritiesMask) == 0) {
            uerror("log_log(): Invalid logging-level (%d)", level);
        }
        else if (getulogmask() & priorityMask) {
            const Message*     msg;

            for (msg = list->first; NULL != msg; msg = msg->next) {
                /*
                 * NB: The message is not printed using "ulog(level,
                 * msg->string)" because "msg->string" might have formatting
                 * characters in it (e.g., "%") from, for example, a call to
                 * "s_prod_info()" with a dangerous product-identifier.
                 */
                ulog(level, "%s", msg->string);

                if (msg == list->last)
                    break;
            }                       /* message loop */
        }                           /* messages should be printed */

        unlock();
        log_clear();
    }                               /* have messages */

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
        log_serror(LOG_FMT("Couldn't allocate %lu bytes for %s"), file, line,
                nbytes, msg);

    return obj;
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

    List*   list = getList();

    if (list) {
        Message* msg;
        Message* next;

        for (msg = list->first; msg; msg = next) {
            next = msg->next;
            free(msg->string);
            free(msg);
        }
        free(list);
        (void)pthread_setspecific(listKey, NULL);
    }

    restoreSigs(&sigset);
}

/**
 * Returns the logging options appropriate to a log-file specification.
 *
 * @param[in] logFileSpec  Log-file specification:
 *                             NULL  Use syslog(3)
 *                             ""    Use syslog(3)
 *                             "-"   Log to `stderr`
 *                             else  Pathname of log-file
 * @return                 Logging options appropriate to the log-file
 *                         specification.
 */
unsigned
log_getLogOpts(
        const char* const logFileSpec)
{
    return (logFileSpec && 0 == strcmp(logFileSpec, "-"))
        /*
         * Interactive invocation. Use ID, timestamp, UTC, no PID, and no
         * console.
         */
        ? LOG_IDENT
        /*
         * Non-interactive invocation. Use ID, timestamp, UTC, PID, and the
         * console as a last resort.
         */
        : LOG_IDENT | LOG_PID | LOG_CONS;
}

/**
 * Initializes logging. This should be called before the command-line is
 * decoded.
 *
 * @param[in] progName    Name of the program. Caller may modify on return.
 * @param[in] maxLogLevel Initial maximum logging-level. One of LOG_ERR,
 *                        LOG_WARNING, LOG_NOTICE, LOG_INFO, or LOG_DEBUG.
 *                        Log messages up to this level will be logged.
 * @param[in] facility    Logging facility. Typically LOG_LDM.
 */
void
log_initLogging(
        const char* const progName,
        const int         maxLogLevel,
        const int         facility)
{
    char* logFileSpec;
    int   ttyFd = open("/dev/tty", O_RDONLY);

    if (-1 == ttyFd) {
        // No controlling terminal => daemon => use syslog(3)
        logFileSpec = NULL;
    }
    else {
        // Controlling terminal exists => interactive => log to `stderr`
        (void)close(ttyFd);
        logFileSpec = "-";
    }

    (void)setulogmask(LOG_UPTO(maxLogLevel));
    (void)openulog(progName, log_getLogOpts(logFileSpec), facility,
            logFileSpec);
}
