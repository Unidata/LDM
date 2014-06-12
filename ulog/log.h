#ifndef LOG_H

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include "ulog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_FMT(fmt)                 "[%s:%d] " fmt
#define LOG_ERRNO()                 log_start(LOG_FMT("%s"),__FILE__,__LINE__,strerror(errno))
/*
 * The LOG_STARTn() macros are deprecated. Use the LOG_ADDn() macros instead.
 */
#define LOG_START0(fmt)             log_start(LOG_FMT(fmt),__FILE__,__LINE__)
#define LOG_START1(fmt,a)           log_start(LOG_FMT(fmt),__FILE__,__LINE__,a)
#define LOG_START2(fmt,a,b)         log_start(LOG_FMT(fmt),__FILE__,__LINE__,a,b)
#define LOG_START3(fmt,a,b,c)       log_start(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c)
#define LOG_START4(fmt,a,b,c,d)     log_start(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c,d)
#define LOG_START5(fmt,a,b,c,d,e)   log_start(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c,d,e)
#define LOG_ADD0(fmt)               log_add(LOG_FMT(fmt),__FILE__,__LINE__)
#define LOG_ADD1(fmt,a)             log_add(LOG_FMT(fmt),__FILE__,__LINE__,a)
#define LOG_ADD2(fmt,a,b)           log_add(LOG_FMT(fmt),__FILE__,__LINE__,a,b)
#define LOG_ADD3(fmt,a,b,c)         log_add(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c)
#define LOG_ADD4(fmt,a,b,c,d)       log_add(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c,d)
#define LOG_ADD5(fmt,a,b,c,d,e)     log_add(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c,d,e)
#define LOG_ADD6(fmt,a,b,c,d,e,f)   log_add(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c,d,e,f)
#define LOG_SERROR0(fmt)            log_serror(LOG_FMT(fmt),__FILE__,__LINE__)
#define LOG_SERROR1(fmt,a)          log_serror(LOG_FMT(fmt),__FILE__,__LINE__,a)
#define LOG_SERROR2(fmt,a,b)        log_serror(LOG_FMT(fmt),__FILE__,__LINE__,a,b)
#define LOG_SERROR3(fmt,a,b,c)      log_serror(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c)
#define LOG_SERROR4(fmt,a,b,c,d)    log_serror(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c,d)
#define LOG_SERROR5(fmt,a,b,c,d,e)  log_serror(LOG_FMT(fmt),__FILE__,__LINE__,a,b,c,d,e)
#define LOG_ERRNUM0(err,fmt)        log_errnum(err,LOG_FMT(fmt),__FILE__,__LINE__)
#define LOG_ERRNUM1(err,fmt,a)      log_errnum(err,LOG_FMT(fmt),__FILE__,__LINE__,a)
#define LOG_ERRNUM2(err,fmt,a,b)    log_errnum(err,LOG_FMT(fmt),__FILE__,__LINE__,a,b)
#define LOG_ERRNUM3(err,fmt,a,b,c)  log_errnum(err,LOG_FMT(fmt),__FILE__,__LINE__,a,b,c)
#define LOG_ERRNUM4(err,fmt,a,b,c,d)    log_errnum(err,LOG_FMT(fmt),__FILE__,__LINE__,a,b,c,d)
#define LOG_ERRNUM5(err,fmt,a,b,c,d,e)  log_errnum(err,LOG_FMT(fmt),__FILE__,__LINE__,a,b,c,d,e)
#define LOG_MALLOC(nbytes,msg)      log_malloc(nbytes, msg, __FILE__, __LINE__)

void log_clear();
void log_start(
    const char* const	fmt,
    ...);
void log_errno(void);
void log_add(
    const char *const   fmt,
    ...);
/**
 * Adds a variadic log-message to the message-list for the current thread.
 *
 * This function is thread-safe.
 *
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
    va_list             args) /**< The arguments referenced by the format. */;
void log_log(
    const int		level);

/*
 * Adds a system error-message based on the current value of "errno" and a
 * higher-level error-message.
 *
 * This function is thread-safe.
 */
void log_serror(
    const char* const fmt,  /**< The higher-level message format */
    ...)                    /**< Arguments referenced by the format */;

/*
 * Adds a system error-message based on a error number and a higher-level
 * error-message.
 *
 * This function is thread-safe.
 */
void log_errnum(
    const int           errnum, /**< The "errno" error number */
    const char* const   fmt,    /**< The higher-level message format or NULL
                                  *  for no higher-level message */
    ...)                        /**< Arguments referenced by the format */;

/**
 * Allocates memory.
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
    const int           line);

/**
 * Frees the log-message resources of the current thread.
 */
void log_free(void);

#ifdef __cplusplus
}
#endif

#endif
