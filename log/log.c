/**
 *   Copyright 2019, University Corporation for Atmospheric Research. All rights
 *   reserved. See the file COPYRIGHT in the top-level source-directory for
 *   licensing conditions.
 *
 * This file implements the the provider-independent `log.h` API. It provides
 * for accumulating log-messages into a thread-specific queue and the logging of
 * that queue at a single logging level.
 *
 * All publicly-available functions in this module are thread-safe.
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
 *   - Enable log file rotation
 */
#include <config.h>

#undef NDEBUG
#include "log.h"
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

/**
 * A queue of log messages.
 */
typedef struct msg_queue {
    /**
     * First message of the current list
     */
    Message*    first;
    /**
     * Last message of the current list. Points to or before the last element of
     * the list. NULL => no messages (but not necessarily no list).
     */
    Message*    last;
} msg_queue_t;

/// Is the logging module initialized?
static volatile sig_atomic_t isInitialized = false;

/**
 * Key for the thread-specific queue of log messages.
 */
static pthread_key_t         queueKey;
/**
 * Whether or not to avoid using the standard error stream.
 */
static volatile sig_atomic_t avoid_stderr;
/**
 * Whether this module needs to be refreshed.
 */
static volatile sig_atomic_t refresh_needed;
/**
 * The mutex that makes this module thread-safe.
 */
static pthread_mutex_t       log_mutex;

/**
 * Locks this module.
 *
 * @retval EBUSY    Mutex is already locked
 * @retval EDEADLK  A deadlock condition was detected or the current thread
 *                  already owns the mutex
 * @retval 0        Success
 */
static int
lock()
{
    return pthread_mutex_lock(&log_mutex);
}

/**
 * Unlocks this module.
 *
 * @retval EPERM  The current thread doesn't own the mutex
 * @retval 0      Success
 */
static int
unlock(void)
{
    return pthread_mutex_unlock(&log_mutex);
}

static void
unlock_cleanup(void* arg)
{
	(void)unlock();
}

/**
 * Asserts that the current thread has acquired this module's lock.
 */
static void assertLocked(void)
{
    int status = pthread_mutex_trylock(&log_mutex);

    assert(status);
}

static bool isDevNull(const int fd)
{
    struct stat fdStat;
    if (fstat(fd, &fdStat) == -1)
        return false;

    struct stat devNullStat;
    if (stat("/dev/null", &devNullStat) == -1)
        return false;

    return fdStat.st_dev == devNullStat.st_dev &&
            fdStat.st_ino == devNullStat.st_ino;
}

/**
 * Initializes a location structure from another location structure.
 *
 * @param[out] dest    The location to be initialized.
 * @param[in]  src     The location whose values are to be used for
 *                     initialization.
 * @threadsafety       Safe
 * @asyncsignalsafety  Safe
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
 * @retval    NULL     Failure. `logl_internal()` called.
 * @return             New message structure
 * @threadsafety       Safe
 * @asyncsignalsafety  UnSafe
 */
static Message* msg_new(void)
{
    Message* msg = malloc(sizeof(Message));

    if (msg == NULL) {
        logl_internal(LOG_LEVEL_ERROR, "malloc() failure: size=%zu",
                sizeof(Message));
    }
    else {
        #define LOG_DEFAULT_STRING_SIZE     256
        char*   string = malloc(LOG_DEFAULT_STRING_SIZE);

        if (NULL == string) {
            logl_internal(LOG_LEVEL_ERROR, "malloc() failure: size=%u",
                    LOG_DEFAULT_STRING_SIZE);
            free(msg);
            msg = NULL;
        }
        else {
            *string = 0;
            msg->string = string;
            msg->size = LOG_DEFAULT_STRING_SIZE;
            msg->next = NULL;
        }
    } // `msg` allocated

    return msg;
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
 * @threadsafety         Safe
 * @asyncsignalsafety    Unsafe
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
            status = errno;
        }
        else {
            // The buffer is too small for the message. Expand it.
            size_t  size = nbytes + 1;
            char*   string = malloc(size);

            if (NULL == string) {
                status = errno;
                logl_internal(LOG_LEVEL_ERROR, "malloc() failure: size=%zu",
                        size);
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

/**
 * Indicates if a message queue is empty.
 *
 * @param[in] queue    The message queue.
 * @retval    true     Iff `queue == NULL` or the queue is empty
 * @threadsafety       Safe
 * @asyncsignalsafety  Safe
 */
static bool queue_is_empty(
        msg_queue_t* const queue)
{
    return queue == NULL || queue->last == NULL;
}

/**
 * Frees a thread-specific message queue. Called at thread exit. Logs an error
 * message and the message queue if the queue isn't empty.
 *
 * @param[in] arg  Pointer previously associated with `queueKey`. Will not be
 *                 `NULL` when called at thread exit.
 */
static void queue_free(void* const arg)
{
    msg_queue_t* const queue = (msg_queue_t*)arg;

    if (!queue_is_empty(queue)) {
		LOG_LOC_DECL(loc);
		logl_add(&loc, "The following messages were not logged:");
    	logl_flush(LOG_LEVEL_ERROR);
    }

    for (Message *msg = queue->first, *next; msg; msg = next) {
        next = msg->next;
        free(msg->string);
        free(msg);
    }

    queue->first = NULL;
    queue->last = NULL;
	free(queue);
}

static void queue_delete_key(void)
{
	(void)pthread_key_delete(queueKey);
}

static void lock_or_abort(void)
{
	int status = lock();

	if (status) {
		fprintf(stderr, "Couldn't lock mutex: %s\n", strerror(status));
		abort();
	}
}

static void unlock_or_abort(void)
{
	int status = unlock();

	if (status) {
		fprintf(stderr, "Couldn't unlock mutex: %s\n", strerror(status));
		abort();
	}
}

/**
 * Performs one-time initialization of this module.
 *
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
static void init_once(void)
{
	int status = pthread_atfork(lock_or_abort, unlock_or_abort,
			unlock_or_abort);

	if (status) {
		logl_internal(LOG_LEVEL_FATAL, "pthread_atfork() failure: %s",
				strerror(status));
		abort();
	}
	else {
		int status = pthread_key_create(&queueKey, queue_free);

		if (status != 0) {
			logl_internal(LOG_LEVEL_FATAL, "pthread_key_create() failure: "
					"errno=\"%s\"", strerror(status));
			abort();
		}

		if (atexit(queue_delete_key)) {
			logl_internal(LOG_LEVEL_FATAL, "Couldn't register "
					"queue_delete_key() with atexit()");
			abort();
		}
    }
}

/**
 * Returns the current thread's message-queue.
 *
 * This function is thread-safe. On entry, this module's lock shall be
 * unlocked.
 *
 * @return             The message-queue of the current thread or NULL if the
 *                     message-queue doesn't exist and couldn't be created.
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
static msg_queue_t* queue_get(void)
{
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
        } // `queue` allocated
    } // queue didn't exist

    return queue;
}

/**
 * Returns the next unused entry in a message-queue. Creates it if necessary.
 *
 * @param[in] queue    The message-queue.
 * @param[in] entry    The next unused entry.
 * @retval    0        Success. `*entry` is set.
 * @retval    ENOMEM   Out-of-memory. Error message logged.
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
static int queue_getNextEntry(
        msg_queue_t* const restrict queue,
        Message** const restrict    entry)
{
    int      status;
    Message* msg = (NULL == queue->last) ? queue->first : queue->last->next;

    if (msg != NULL) {
        *entry = msg;
        status = 0;
    }
    else {
        msg = msg_new();

        if (msg == NULL) {
            status = ENOMEM;
        }
        else {
            if (NULL == queue->first)
                queue->first = msg;  /* very first message */

            if (NULL != queue->last)
                queue->last->next = msg;

            *entry = msg;
            status = 0;
        } // `msg` allocated
    } // need new message structure

    return status;
}

/**
 * Clears the accumulated log-messages of the current thread. Doesn't delete
 * them.
 *
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
static void queue_clear(msg_queue_t* const queue)
{
    if (NULL != queue)
        queue->last = NULL;
}

/**
 * Returns the default destination for log messages. If `log_avoid_stderr()`
 * hasn't been called, then the default destination will be the standard error
 * stream; otherwise, the default destination will be that given by
 * log_get_default_daemon_destination().
 *
 * @retval ""          Log to the system logging daemon
 * @retval "-"         Log to the standard error stream
 * @return             The pathname of the log file
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
static const char* get_default_destination(void)
{
    return avoid_stderr
            ? logi_get_default_daemon_destination()
            : "-";
}

/**
 * Indicates if a message at a given logging level would be logged.
 *
 * @param[in] level    The logging level
 * @retval    true     iff a message at level `level` would be logged
 * @threadsafety       Safe
 * @asyncsignalsafety  Safe
 */
static bool is_level_enabled(const log_level_t level)
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
 * @pre                Module is locked
 * @retval  0          Success
 * @retval -1          Failure
 * @asyncsignalsafety  Unsafe
 */
static int refresh_if_necessary(void)
{
    assertLocked();

    int status = 0;

    if (refresh_needed) {
        if (avoid_stderr && LOG_IS_STDERR_SPEC(logi_get_destination())) {
            // The logging destination must be changed
            status = logi_set_destination(
                    logi_get_default_daemon_destination());
        }
        else {
            status = 0;
        }

        if (status == 0) {
            status = logi_reinit();
            refresh_needed = 0;
        }
    }

    return status;
}

/**
 * The mapping from logging levels to system logging daemon priorities:
 */
static int syslog_priorities[] = {
        LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING, LOG_ERR
};

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
 * @threadsafety            Safe
 * @asyncsignalsafety       Unsafe
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
 * Returns a formated variadic message.
 *
 * @param[in] format   Message format
 * @param[in] args     Optional format arguments
 * @retval    NULL     Out-of-memory. `logl_internal()` called.
 * @return             Allocated string of formatted message. Caller should free
 *                     when it's no longer needed.
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
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

/******************************************************************************
 * Package-private API:
 ******************************************************************************/

/**
 *  Logging level.
 */
volatile sig_atomic_t log_level = LOG_LEVEL_NOTICE;

int logl_level_to_priority(const log_level_t level)
{
    return logl_vet_level(level) ? syslog_priorities[level] : LOG_ERR;
}

const char* logl_basename(const char* const pathname)
{
    const char* const cp = strrchr(pathname, '/');
    return cp ? cp + 1 : pathname;
}

/**
 * @asyncsignalsafety       Unsafe
 */
int logl_vlog(
        const log_loc_t* const  loc,
        const log_level_t       level,
        const char* const       format,
        va_list                 args)
{
    int status;

    if (lock()) {
        status = -1;
    }
    else {
    	pthread_cleanup_push(unlock_cleanup, NULL);

        if (!is_level_enabled(level)) {
            status = 0; // Success
        }
        else {
            char* msg = formatMsg(format, args);

            if (msg == NULL) {
                status = -1;
            }
            else {
                (void)refresh_if_necessary();

                status = logi_log(level, loc, msg);

                if (status == 0)
                    status = logi_flush();

                free(msg);
            } // Have message
        } // Message should be logged

        pthread_cleanup_pop(true);
    } // Module locked

    return status;
}

int logl_vadd(
        const log_loc_t* const restrict loc,
        const char* const restrict      fmt,
        va_list                         args)
{
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
            status = queue_getNextEntry(queue, &msg); // `queue != NULL`
            if (status == 0) {
                loc_init(&msg->loc, loc);
                status = msg_format(msg, fmt, args);
                if (status == 0)
                    queue->last = msg;
            } // have a message structure
        } // message-queue isn't NULL
    } // arguments aren't NULL

    return status;
}

int logl_add(
        const log_loc_t* const restrict loc,
        const char* const restrict      fmt,
                                        ...)
{
    va_list  args;

    va_start(args, fmt);
        int status = logl_vadd(loc, fmt, args);
    va_end(args);

    return status;
}

int logl_add_errno(
        const log_loc_t* const loc,
        const int              errnum,
        const char* const      fmt,
                               ...)
{
    int status = logl_add(loc, "%s", strerror(errnum));

    if (status == 0 && fmt && *fmt) {
        va_list     args;

        va_start(args, fmt);
            status = logl_vadd(loc, fmt, args);
        va_end(args);
    }

    return status;
}

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
 * Logs the currently-accumulated log-messages of the current thread and resets
 * the message-queue for the current thread.
 *
 * @param[in] level    The level at which to log the messages. One of
 *                     LOG_LEVEL_ERROR, LOG_LEVEL_WARNING, LOG_LEVEL_NOTICE,
 *                     LOG_LEVEL_INFO, or LOG_LEVEL_DEBUG; otherwise, the
 *                     behavior is undefined.
 * @retval    0        Success
 * @retval    -1       Error
 * @threadsafety       Safe
 * @asyncsignalsafety  Unsafe
 */
int logl_flush(const log_level_t level)
{
    int                status = 0; // Success
    msg_queue_t* const queue = queue_get();

    if (!queue_is_empty(queue)) {
        /*
         * The following message is added so that the location of the call to
         * log_flush() is logged in case the call needs to be adjusted.
        logl_add(loc, "Log messages flushed");
         */
        if (is_level_enabled(level)) {
            if (lock()) {
                status = -1;
            }
            else {
				pthread_cleanup_push(unlock_cleanup, NULL);

                (void)refresh_if_necessary();

                for (const Message* msg = queue->first; NULL != msg;
                        msg = msg->next) {
                    status = logi_log(level, &msg->loc, msg->string);

                    if (status)
                        break;

                    if (msg == queue->last)
                        break;
                } // Message loop

                status = logi_flush();

				pthread_cleanup_pop(true);
            } // Module locked
        } // Messages should be printed

        queue_clear(queue);
    } // Message queue isn't empty

    return status;
}

int logl_vlog_q(
        const log_loc_t* const restrict loc,
        const log_level_t               level,
        const char* const restrict      format,
        va_list                         args)
{
    if (format && *format)
        logl_vadd(loc, format, args);

    return logl_flush(level);
}

/**
 * @asyncsignalsafety       Unsafe
 */
int logl_log(
        const log_loc_t* const restrict loc,
        const log_level_t               level,
        const char* const restrict      format,
                                        ...)
{
    va_list args;

    va_start(args, format);
        int status = logl_vlog(loc, level, format, args);
    va_end(args);

    return status;
}

int logl_errno(
        const log_loc_t* const     loc,
        const int                  errnum,
        const char* const restrict fmt,
                                   ...)
{
    va_list args;

    va_start(args, fmt);
        int status = logl_log(loc, LOG_LEVEL_ERROR, "%s", strerror(errnum));

        if (status == 0)
            status = logl_vlog(loc, LOG_LEVEL_ERROR, fmt, args);
    va_end(args);

    return status;
}

int logl_log_q(
        const log_loc_t* const restrict loc,
        const log_level_t               level,
        const char* const restrict      format,
                                        ...)
{
    va_list args;

    va_start(args, format);
        int status = logl_vlog_q(loc, level, format, args);
    va_end(args);

    return status;
}

int logl_errno_q(
        const log_loc_t* const     loc,
        const int                  errnum,
        const char* const restrict fmt,
                                   ...)
{
    va_list args;

    va_start(args, fmt);
        logl_add(loc, "%s", strerror(errnum));
        int status = logl_vlog_q(loc, LOG_LEVEL_ERROR, fmt, args);
    va_end(args);

    return status;
}

/******************************************************************************
 * Public API:
 ******************************************************************************/

bool log_stderr_is_open(void)
{
    struct stat stderr_stat;

    return fstat(STDERR_FILENO, &stderr_stat) == 0;
}

bool log_amDaemon(void)
{
    char  ctermId[L_ctermid];
    char* ptr = ctermid(ctermId);
    if (ptr == NULL || *ptr == 0)
        return true;
    const int fd = open(ptr, O_RDWR);
    if (fd >= 0) {
        close(fd);
        return false;
    }
    return true;
}

int log_init(const char* const id)
{
	int status;

	if (isInitialized) {
		status = -1;
	}
	else {
		static pthread_once_t queueKeyControl = PTHREAD_ONCE_INIT;
		(void)pthread_once(&queueKeyControl, init_once);

		isInitialized = true;

		status = logi_init(id);

		if (status) {
			perror("logi_init()");
		}
		else {
			// `avoid_stderr` must be set before `get_default_destination()`
			avoid_stderr = isDevNull(STDERR_FILENO);
			status = logi_set_destination(get_default_destination());

			if (status) {
				perror("logi_set_destination()");
			}
			else {
				/*
				 * For an unknown reason, a mutex that is robust,
				 * error-checking, recursive, or prevents priority inversion,
				 * cannot be unlocked by a child process created by `fork()` --
				 * which defeats the reason for calling `pthread_atfork()`.
				 * SRE 2019-12-03
				 * $ uname -a
				 * Linux gilda 3.10.0-862.14.4.el7.x86_64 #1 SMP Wed Sep 26 15:12:11 UTC 2018 x86_64 x86_64 x86_64 GNU/Linux
				 * $ gcc -dumpversion
				 * 4.8.5
				 */
				if (pthread_mutex_init(&log_mutex, NULL)) {
					perror("pthread_mutex_init()");
					status = -1;
				}
			} // logging destination set in implementation
		} // Implementation initialized
	} // Module wasn't initialized

    return status;
}

int log_fini_located(const log_loc_t* const loc)
{
    int status = 0;

    if (isInitialized) {
		logl_log(loc, LOG_LEVEL_DEBUG, "Terminating logging");

		if (logi_fini())
			status = -1;

		if (pthread_mutex_destroy(&log_mutex))
			status = -1;

		isInitialized = false;
    }

    return status;
}

void
log_free_located(const log_loc_t* const loc)
{}

void log_avoid_stderr(void)
{
    avoid_stderr = true;
    refresh_needed = true;
}

void log_refresh(void)
{
    refresh_needed = 1;
}

int log_set_id(const char* const id)
{
    int status;

    if (id == NULL) {
        status = -1;
    }
    else {
        if (lock()) {
            status = -1;
        }
        else {
			pthread_cleanup_push(unlock_cleanup, NULL);

            status = logi_set_id(id);

			pthread_cleanup_pop(true);
        } // Module is locked
    } // Valid ID

    return status;
}

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

        if (lock()) {
            status = -1;
        }
        else {
			pthread_cleanup_push(unlock_cleanup, NULL);

            status = logi_set_id(id);

			pthread_cleanup_pop(true);
        } // Module is locked
    } // Valid host ID

    return status;
}

const char* log_get_default_destination(void)
{
    return get_default_destination();
}

int log_set_destination(const char* const dest)
{
    int status;

    if (dest == NULL) {
        status = -1;
    }
    else {
        if (lock()) {
            status = -1;
        }
        else {
            pthread_cleanup_push(unlock_cleanup, NULL);

            status = logi_set_destination(dest);

            pthread_cleanup_pop(true);
        } // Module is locked
    }

    return status;
}

const char* log_get_destination(void)
{
    const char* dest = NULL;

    if (lock() == 0) {
    	pthread_cleanup_push(unlock_cleanup, NULL);

        dest = logi_get_destination();

        pthread_cleanup_pop(true);
    } // Module is locked

    return dest;
}

int log_set_level(const log_level_t level)
{
    int status;

    if (!logl_vet_level(level)) {
        status = -1;
    }
    else {
        log_level = level;
        status = 0;
    }

    return status;
}

void log_roll_level(void)
{
    int level = log_level - 1;

    if (level < 0)
        level = LOG_LEVEL_NOTICE;

    log_level = level;
}

log_level_t log_get_level(void)
{
    return log_level;
}

bool log_is_level_enabled(const log_level_t level)
{
    return is_level_enabled(level);
}

void
log_clear(void)
{
    msg_queue_t*   queue = queue_get();

    queue_clear(queue);
}

int
log_flush(const log_level_t level)
{
    int status = -1;
    int prevState;

    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &prevState) == 0) {
        status = 0;

        if (logl_flush(level))
            status = -1;

        if (pthread_setcancelstate(prevState, &prevState))
            status = -1;
    } // Thread cancellation is disabled

    return status;
}

const char* log_get_default_daemon_destination(void)
{
    return logi_get_default_daemon_destination();
}

int log_set_facility(const int facility)
{
    int status;

    if (lock()) {
        status = -1;
    }
    else {
    	pthread_cleanup_push(unlock_cleanup, NULL);

        status = logi_set_facility(facility);

        pthread_cleanup_pop(true);
    } // Module is locked

    return status;
}

int log_get_facility(void)
{
    int status;

    if (lock()) {
        status = -1;
    }
    else {
    	pthread_cleanup_push(unlock_cleanup, NULL);

        status = logi_get_facility();

        pthread_cleanup_pop(true);
    } // Module is locked

    return status;
}

const char* log_get_id(void)
{
    const char* ident = NULL;

    if (lock() == 0) {
        ident = logi_get_id();

        if (unlock())
            ident = NULL;
    } // Module is locked

    return ident;
}

int log_set_options(const unsigned options)
{
	int status;

	if (lock()) {
		status = -1;
	}
	else {
    	pthread_cleanup_push(unlock_cleanup, NULL);
    	status = logi_set_options(options);
        pthread_cleanup_pop(true);
	}

	return status;
}

unsigned log_get_options(void)
{
    unsigned options;

    if (lock()) {
        abort();
    }
    else {
    	pthread_cleanup_push(unlock_cleanup, NULL);
        options = logi_get_options();
        pthread_cleanup_pop(true);
    } // Module is locked

    return options;
}

