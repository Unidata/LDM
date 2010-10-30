/*
 *   Copyright 2006, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 *
 * Provides for the accumulation of log-messages and the printing of all,
 * accumlated log-messages at a single priority.
 *
 * This module uses the ulog(3) module.
 *
 * This module is not thread-safe.
 */
/* $Id: log.c,v 1.1.2.4 2006/12/17 22:07:19 steve Exp $ */

#include <config.h>

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>   /* NULL */
#include <stdio.h>    /* vsnprintf(), snprintf() */
#include <stdlib.h>   /* malloc(), free(), abort() */
#include <string.h>
#include <strings.h>  /* strdup() */

#include "ulog.h"

#include "log.h"


/*
 * A log-message.  Such structures accumulate: they are freed only by
 * log_close().
 */
typedef struct message {
    char                string[512];
    struct message*     next;
} Message;

/*
 * The first (i.e., most fundamental) log-message.
 */
static Message*         first = NULL;

/*
 * Pointer to the last message to be set.
 */
static Message*         last = NULL;

/*
 * Whether or not this module is initialized.
 */
static int              initialized = 0;


/*
 * Closes this module, releasing all resources.
 */
static void log_close(void)
{
    Message*        msg = first;

    while (NULL != msg) {
        Message*    next = msg->next;

        free(msg);
        msg = next;
    }

    first = NULL;
    last = NULL;
}


/*
 * Initializes this module.
 */
static void log_init(void)
{
    if (!initialized) {
        if (atexit(log_close))
            serror("log_init(): Couldn't register atexit(3) routine: %s",
                strerror(errno));
        initialized = 1;
    }
}


/*
 * Adds a log-message.  If the format is NULL, then no message will be added.
 * Arguments:
 *      fmt     The format for the message or NULL.
 *      args    The arguments referenced by the format.
 */
static void log_vadd(
    const char *const   fmt,
    va_list             args)
{
    log_init();

    if (fmt != NULL) {
        Message*        msg = (NULL == last) ? first : last->next;

        if (msg == NULL) {
            msg = (Message*)malloc(sizeof(Message));

            if (msg == NULL) {
                serror("log_vadd(): malloc(%lu) failure",
                    (unsigned long)sizeof(Message));
            }
            else {
                msg->string[0] = 0;
                msg->next = NULL;

                if (NULL == first)
                    first = msg;         /* very first message structure */
            }
        }

        if (msg != NULL) {
            int         nbytes;

            nbytes = vsnprintf(msg->string, sizeof(msg->string)-1, fmt, args);
            if (nbytes < 0) {
                nbytes = snprintf(msg->string, sizeof(msg->string)-1, 
                    "log_vadd(): vsnprintf() failure: \"%s\"", fmt);
            }

            msg->string[sizeof(msg->string)-1] = 0;
            if (NULL != last) {
                last->next = msg;
            }
            last = msg;
        }                               /* msg != NULL */
    }                                   /* format string != NULL */
}


/******************************************************************************
 * Public API:
 ******************************************************************************/


/*
 * Clears the accumulated log-messages.  If log_log() is invoked after this
 * function, then no messages will be logged.
 */
void log_clear()
{
    last = NULL;
}


/*
 * Resets this module and adds the first log-message.  This function
 * is equivalent to calling log_clear() followed by log_add().
 * Arguments:
 *      fmt     The format for the message or NULL.  If NULL, then no
 *              messsage is added.
 *      ...     The arguments referenced in the format.
 */
void log_start(
    const char* const   fmt,
    ...)
{
    va_list     args;

    log_clear();
    va_start(args, fmt);
    log_vadd(fmt, args);
    va_end(args);
}


/*
 * Adds a log-message.
 * Arguments:
 *      fmt     The format for the message or NULL.
 *      ...     The arguments referenced in the format.
 */
void log_add(
    const char *const   fmt,
    ...)
{
    va_list     args;

    va_start(args, fmt);
    log_vadd(fmt, args);
    va_end(args);
}


/*
 * Resets this module and adds an "errno" message.  Calling this function is
 * equivalent to calling log_start(strerror(errno)).
 */
void log_errno(void)
{
    log_start(strerror(errno));
}

/*
 * Adds a system error-message and a higher-level error-message.  This is a
 * convenience function for the sequence
 *      log_errno();
 *      log_add(fmt, ...);
 *
 * ARGUMENTS:
 *      fmt             The format-string for the higher-level error-message or
 *                      NULL.  If NULL, then no higher-level error-message is
 *                      added.
 *      ...             Arguments for the format-string.
 */
void log_serror(
    const char *const   fmt,
    ...)
{
    va_list     args;

    log_errno();
    va_start(args, fmt);
    log_vadd(fmt, args);
    va_end(args);
}


/*
 * Logs the currently-accumulated log-messages and resets this module.
 *
 * This function is thread-safe.
 * Arguments:
 *      level   The level at which to log the messages.  One of LOG_ERR,
 *              LOG_WARNING, LOG_NOTICE, LOG_INFO, or LOG_DEBUG; otherwise,
 *              the behavior is undefined.
 */
void log_log(
    const int   level)
{
    if (NULL != last) {
        static const unsigned       allPrioritiesMask = 
            LOG_MASK(LOG_ERR) |
            LOG_MASK(LOG_WARNING) |
            LOG_MASK(LOG_NOTICE) | 
            LOG_MASK(LOG_INFO) |
            LOG_MASK(LOG_DEBUG);
        const int                   priorityMask = LOG_MASK(level);

        if ((priorityMask & allPrioritiesMask) == 0) {
            uerror("log_log(): Invalid logging-level (%d)", level);
        }
        else if (getulogmask() & priorityMask) {
            const Message*     msg;

            for (msg = first; NULL != msg; msg = msg->next) {
                /*
                 * NB: The message is not printed using "ulog(level,
                 * msg->string)" because "msg->string" might have formatting
                 * characters in it (e.g., "%") from, for example, a call to
                 * "s_prod_info()" with a dangerous product-identifier.
                 */
                ulog(level, "%s", msg->string);

                if (msg == last)
                    break;
            }
        }                                       /* messages should be printed */

        log_clear();
    }                                           /* have messages */
}
