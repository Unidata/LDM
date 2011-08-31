/*
 *   Copyright © 2011, University Corporation for Atmospheric Research.
 *   See file COPYRIGHT for copying and redistribution conditions.
 */
#ifndef NOAAPORT_LOG_H
#define NOAAPORT_LOG_H

#include <stdarg.h>

#include <ulog.h>

#define NPL_FMT(fmt)                fmt " [%s:%d]"
#define NPL_ERRNO()                 nplErrno(NPL_FMT("%s"),strerror(errno),__FILE__,__LINE__)
#define NPL_START0(fmt)             nplStart(NPL_FMT(fmt),__FILE__,__LINE__);
#define NPL_START1(fmt,a)           nplStart(NPL_FMT(fmt),a,__FILE__,__LINE__);
#define NPL_START2(fmt,a,b)         nplStart(NPL_FMT(fmt),a,b,__FILE__,__LINE__);
#define NPL_START3(fmt,a,b,c)       nplStart(NPL_FMT(fmt),a,b,c,__FILE__,__LINE__);
#define NPL_START4(fmt,a,b,c,d)     nplStart(NPL_FMT(fmt),a,b,c,d,__FILE__,__LINE__);
#define NPL_START5(fmt,a,b,c,d,e)   nplStart(NPL_FMT(fmt),a,b,c,d,e,__FILE__,__LINE__);
#define NPL_ADD0(fmt)               nplAdd(NPL_FMT(fmt),__FILE__,__LINE__)
#define NPL_ADD1(fmt,a)             nplAdd(NPL_FMT(fmt),a,__FILE__,__LINE__)
#define NPL_ADD2(fmt,a,b)           nplAdd(NPL_FMT(fmt),a,b,__FILE__,__LINE__)
#define NPL_ADD3(fmt,a,b,c)         nplAdd(NPL_FMT(fmt),a,b,c,__FILE__,__LINE__)
#define NPL_ADD4(fmt,a,b,c,d)       nplAdd(NPL_FMT(fmt),a,b,c,d,__FILE__,__LINE__)
#define NPL_ADD5(fmt,a,b,c,d,e)     nplAdd(NPL_FMT(fmt),a,b,c,d,e,__FILE__,__LINE__)
#define NPL_SERROR0(fmt)            nplErrno(NPL_FMT(fmt),__FILE__,__LINE__)
#define NPL_SERROR1(fmt,a)          nplErrno(NPL_FMT(fmt),a,__FILE__,__LINE__)
#define NPL_SERROR2(fmt,a,b)        nplErrno(NPL_FMT(fmt),a,b,__FILE__,__LINE__)
#define NPL_SERROR3(fmt,a,b,c)      nplErrno(NPL_FMT(fmt),a,b,c,__FILE__,__LINE__)
#define NPL_SERROR4(fmt,a,b,c,d)    nplErrno(NPL_FMT(fmt),a,b,c,d,__FILE__,__LINE__)
#define NPL_SERROR5(fmt,a,b,c,d,e)  nplErrno(NPL_FMT(fmt),a,b,c,d,e,__FILE__,__LINE__)
#define NPL_ERRNUM0(err,fmt)            nplErrnum(err,NPL_FMT(fmt),__FILE__,__LINE__)
#define NPL_ERRNUM1(err,fmt,a)          nplErrnum(err,NPL_FMT(fmt),a,__FILE__,__LINE__)
#define NPL_ERRNUM2(err,fmt,a,b)        nplErrnum(err,NPL_FMT(fmt),a,b,__FILE__,__LINE__)
#define NPL_ERRNUM3(err,fmt,a,b,c)      nplErrnum(err,NPL_FMT(fmt),a,b,c,__FILE__,__LINE__)
#define NPL_ERRNUM4(err,fmt,a,b,c,d)    nplErrnum(err,NPL_FMT(fmt),a,b,c,d,__FILE__,__LINE__)
#define NPL_ERRNUM5(err,fmt,a,b,c,d,e)  nplErrnum(err,NPL_FMT(fmt),a,b,c,d,e,__FILE__,__LINE__)

/**
 * Logs a system error.
 *
 * This function is thread-safe.
 */
void nplSerror(
    const char* fmt,    /**< The message format */
    ...)                /**< Arguments referenced by the format */;

/*
 * Logs a program error.
 *
 * This function is thread-safe.
 */
void nplError(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */;

/*
 * Logs a warning.
 *
 * This function is thread-safe.
 */
void nplWarn(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */;

/*
 * Logs a notice.
 *
 * This function is thread-safe.
 */
void nplNotice(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */;

/*
 * Logs an informational message.
 *
 * This function is thread-safe.
 */
void nplInfo(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */;

/*
 * Logs a debuging message.
 *
 * This function is thread-safe.
 */
void nplDebug(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */;

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
    va_list             args) /**< The arguments referenced by the format. */;

/*
 * Sets the first log-message.
 *
 * This function is thread-safe.
 */
void nplStart(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */;

/*
 * Adds a log-message.
 *
 * This function is thread-safe.
 */
void nplAdd(
    const char* const fmt,  /**< The message format */
    ...)                    /**< Arguments referenced by the format */;

/*
 * Adds a system error-message based on the value of "errno" and a higher-level
 * error-message.
 *
 * This function is thread-safe.
 */
void nplErrno(
    const char* const fmt,  /**< [in] The higher-level message format or NULL
                              *  for no higher-level message */
    ...)                    /**< [in] Arguments referenced by the format */;

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
    ...)                        /**< Arguments referenced by the format */;

/*
 * Logs the currently-accumulated log-messages and resets the message-list for
 * the current thread.
 *
 * This function is thread-safe.
 */
void nplLog(
    const int   level)  /**< The level at which to log the messages.  One of
                          *  LOG_ERR, LOG_WARNING, LOG_NOTICE, LOG_INFO, or
                          *  LOG_DEBUG; otherwise, the behavior is undefined. */;

#endif
