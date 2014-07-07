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
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>   /* NULL */
#include <stdio.h>    /* vsnprintf(), snprintf() */
#include <stdlib.h>   /* malloc(), free(), abort() */
#include <string.h>

#include "ulog.h"

#include "log.h"


/**
 * A log-message.  Such structures accumulate in a thread-specific message-list.
 */
typedef struct message {
    struct message*     next;   /**< pointer to next message */
    char*               string; /**< message buffer */
#define DEFAULT_STRING_SIZE     256
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
    List*   list = getList();

    if (NULL != list)
        list->last = NULL;
}

/**
 * Adds a variadic log-message to the message-list for the current thread.
 *
 * @param[in] fmt       Formatting string.
 * @param[in] args      Formatting arguments.
 * @retval 0            Success
 * @retval EAGAIN       Failure due to the buffer being too small for the
 *                      message.  The buffer has been expanded and the client
 *                      should call this function again.
 * @retval EINVAL       \a fmt or \a args is \c NULL. Error message logged.
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
    int                 status;

    if (NULL == fmt) {
        lock();
        uerror("log_vadd(): NULL argument");
        unlock();

        status = EINVAL;
    }
    else {
        List*   list = getList();

        if (NULL != list) {
            Message*    msg = (NULL == list->last) ? list->first :
                list->last->next;

            status = 0;

            if (msg == NULL) {
                msg = (Message*)malloc(sizeof(Message));

                if (msg == NULL) {
                    status = errno;

                    lock();
                    serror("log_vadd(): malloc(%lu) failure",
                        (unsigned long)sizeof(Message));
                    unlock();
                }
                else {
                    char*   string = (char*)malloc(DEFAULT_STRING_SIZE);

                    if (NULL == string) {
                        status = errno;

                        lock();
                        serror("log_vadd(): malloc(%lu) failure",
                            (unsigned long)DEFAULT_STRING_SIZE);
                        unlock();
                    }
                    else {
                        msg->string = string;
                        msg->size = DEFAULT_STRING_SIZE;
                        msg->next = NULL;

                        if (NULL == list->first)
                            list->first = msg;  /* very first message */
                    }
                }
            }

            if (0 == status) {
                int nbytes = vsnprintf(msg->string, msg->size, fmt, args);

                if (0 > nbytes) {
                    status = errno;

                    lock();
                    serror("log_vadd(): vsnprintf() failure");
                    unlock();
                }
                else if (msg->size <= nbytes) {
                    /* The buffer is too small for the message */
                    size_t  size = nbytes + 1;
                    char*   string = (char*)malloc(size);

                    if (NULL == string) {
                        status = errno;

                        lock();
                        serror("log_vadd(): malloc(%lu) failure",
                            (unsigned long)size);
                        unlock();
                    }
                    else {
                        free(msg->string);

                        msg->string = string;
                        msg->size = size;
                        status = EAGAIN;

                        if (NULL != list->last)
                            list->last->next = msg;
                    }
                }
                else {
                    if (NULL != list->last)
                        list->last->next = msg;

                    list->last = msg;
                }
            }                               /* have a message structure */
        }                                   /* message-list isn't NULL */
    }                                       /* arguments aren't NULL */

    return status;
}

/**
 * Sets the first log-message for the current thread.
 */
void log_start(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    va_list     args;

    log_clear();
    va_start(args, fmt);

    if (EAGAIN == log_vadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)log_vadd(fmt, args);
    }

    va_end(args);
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
    va_list     args;

    va_start(args, fmt);

    if (EAGAIN == log_vadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)log_vadd(fmt, args);
    }

    va_end(args);
}

/**
 * Adds a system error-message for the current thread.
 */
void log_errno(void)
{
    log_start("%s", strerror(errno));
}

/**
 * Adds a system error-message for the current thread based on the current value
 * of "errno" and a higher-level error-message.
 */
void log_serror(
    const char* const fmt,  /**< The higher-level message format */
    ...)                    /**< Arguments referenced by the format */
{
    va_list     args;

    log_errno();
    va_start(args, fmt);

    if (EAGAIN == log_vadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)log_vadd(fmt, args);
    }

    va_end(args);
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
}

/**
 * Allocates memory. Thread safe.
 *
 * @param nbytes        Number of bytes to allocate.
 * @param msg           Message to print on error. Should complete the sentence
 *                      "Couldn't allocate <n> bytes for ...".
 * @param file          Name of the file.
 * @param line          Line number in the file.
 * @retval NULL         Out of memory. \c log_add() called.
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
 * Frees the log-message resources of the current thread.
 */
void
log_free(void)
{
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
}
