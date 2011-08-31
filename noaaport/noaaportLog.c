/*
 *   Copyright © 2011, University Corporation for Atmospheric Research.
 *
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#define _XOPEN_SOURCE 500

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>     /* NULL */
#include <stdio.h>      /* vsnprintf(), snprintf() */
#include <stdlib.h>     /* malloc(), free() */
#include <string.h>     /* strerrno() */

#include <ulog.h>       /* LDM logging module */

#include "config.h"
#include "noaaportLog.h"            /* Eat own dog food */

/**
 * A log-message.  Such structures accumulate.
 */
typedef struct message {
    char*               string; /**< message buffer */
    struct message*     next;   /**< pointer to next message */
    size_t              size;   /**< size of message buffer */
} Message;
#define DEFAULT_STRING_SIZE     256

/**
 * A list of log messages.
 */
typedef struct list {
    Message*    first;
    Message*    last;
} List;

/**
 * Key for the thread-specific list of log messages.
 */
static pthread_key_t    listKey;

/**
 * Whether or not the key has been created.
 */
static int              keyCreated = 0;

/**
 * The mutex that makes this module thread-safe.
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
 * Logs a system error.
 *
 * This function is thread-safe.
 */
void nplSerror(
    const char* fmt,    /**< The message format */
    ...)                /**< Arguments referenced by the format */
{
    va_list     args;

    va_start(args, fmt);
    lock();
    ulog(LOG_ERR, "%s", strerror(errno));
    vulog(LOG_ERR, fmt, args);
    unlock();
    va_end(args);
}

/*
 * Logs a program error.
 *
 * This function is thread-safe.
 */
void nplError(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    va_list     args;

    va_start(args ,fmt);
    lock();
    (void)vulog(LOG_ERR, fmt, args);
    unlock();
    va_end(args);
}

/*
 * Logs a warning.
 *
 * This function is thread-safe.
 */
void nplWarn(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    va_list     args;

    va_start(args ,fmt);
    lock();
    (void)vulog(LOG_WARNING, fmt, args);
    unlock();
    va_end(args);
}

/*
 * Logs a notice.
 *
 * This function is thread-safe.
 */
void nplNotice(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    va_list     args;

    va_start(args ,fmt);
    lock();
    (void)vulog(LOG_NOTICE, fmt, args);
    unlock();
    va_end(args);
}

/*
 * Logs an informational message.
 *
 * This function is thread-safe.
 */
void nplInfo(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    va_list     args;

    va_start(args ,fmt);
    lock();
    (void)vulog(LOG_INFO, fmt, args);
    unlock();
    va_end(args);
}

/*
 * Logs a debuging message.
 *
 * This function is thread-safe.
 */
void nplDebug(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    va_list     args;

    va_start(args ,fmt);
    lock();
    (void)vulog(LOG_DEBUG, fmt, args);
    unlock();
    va_end(args);
}

/**
 * Clears the accumulated log-messages.
 *
 * This function is thread-safe.
 */
static void nplClear()
{
    List*   list = getList();

    if (NULL != list)
        list->last = NULL;
}

/**
 * Adds a variadic log-message to the message-list for the current thread.
 *
 * This function is thread-safe.
 *
 * @retval 0            Success
 * @retval EAGAIN       Failure due to the buffer being too small for the
 *                      message.  The buffer has been expanded and the client
 *                      should call this function again.
 * @retval EINVAL       There are insufficient arguments. Error message logged.
 * @retval EILSEQ       A wide-character code that doesn't correspond to a
 *                      valid character has been detected. Error message logged.
 * @retval ENOMEM       Out-of-memory. Error message logged.
 * @retval EOVERFLOW    The length of the message is greater than {INT_MAX}.
 *                      Error message logged.
 */
int nplVadd(
    const char* const   fmt,  /**< The message format or NULL for no message */
    va_list             args) /**< The arguments referenced by the format. */
{
    int                 status = 0; /* default success */

    if (NULL != fmt) {
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
                    serror("nplVadd(): malloc(%lu) failure",
                        (unsigned long)sizeof(Message));
                    unlock();
                }
                else {
                    char*   string = (char*)malloc(DEFAULT_STRING_SIZE);

                    if (NULL == string) {
                        status = errno;

                        lock();
                        serror("nplVadd(): malloc(%lu) failure",
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
                    serror("nplVadd(): vsnprintf() failure");
                    unlock();
                }
                else if (msg->size <= nbytes) {
                    /* The buffer is too small for the message */
                    size_t  size = nbytes + 1;
                    char*   string = (char*)malloc(size);

                    if (NULL == string) {
                        status = errno;

                        lock();
                        serror("nplVadd(): malloc(%lu) failure",
                            (unsigned long)size);
                        unlock();
                    }
                    else {
                        free(msg->string);

                        msg->string = string;
                        msg->size = size;
                        status = EAGAIN;
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

/*
 * Sets the first log-message.
 *
 * This function is thread-safe.
 */
void nplStart(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    va_list     args;

    nplClear();
    va_start(args, fmt);

    if (EAGAIN == nplVadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)nplVadd(fmt, args);
    }

    va_end(args);
}

/*
 * Adds a log-message.
 *
 * This function is thread-safe.
 */
void nplAdd(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */
{
    va_list     args;

    va_start(args, fmt);

    if (EAGAIN == nplVadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)nplVadd(fmt, args);
    }

    va_end(args);
}

/*
 * Adds a system error-message based on the value of "errno" and a higher-level
 * error-message.
 *
 * This function is thread-safe.
 */
void nplErrno(
    const char* const fmt,  /**< [in] The higher-level message format or NULL
                              *  for no higher-level message */
    ...)                    /**< [in] Arguments referenced by the format */
{
    va_list     args;

    nplStart("%s", strerror(errno));
    va_start(args, fmt);

    if (EAGAIN == nplVadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)nplVadd(fmt, args);
    }

    va_end(args);
}

/*
 * Adds a system error-message based on a given "errno" value and a
 * higher-level error-message.
 *
 * This function is thread-safe.
 */
void nplErrnum(
    const int           errnum, /**< [in] The "errno" value */
    const char* const   fmt,    /**< The higher-level message format or NULL
                                  *  for no higher-level message */
    ...)                        /**< Arguments referenced by the format */
{
    va_list     args;

    nplStart("%s", strerror(errnum));
    va_start(args, fmt);

    if (EAGAIN == nplVadd(fmt, args)) {
        va_end(args);
        va_start(args, fmt);
        (void)nplVadd(fmt, args);
    }

    va_end(args);
}

/*
 * Logs the currently-accumulated log-messages and resets the message-list for
 * the current thread.
 *
 * This function is thread-safe.
 */
void nplLog(
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
            nplError("nplLog(): Invalid logging-level (%d)", level);
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
        nplClear();
    }                               /* have messages */
}
