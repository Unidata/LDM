#ifndef LOG_H

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include "ulog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_FMT(fmt)                fmt " [%s:%d]"
#define LOG_ERRNO()                 log_start(LOG_FMT("%s"),strerror(errno),__FILE__,__LINE__)
#define LOG_START0(fmt)             log_start(LOG_FMT(fmt),__FILE__,__LINE__);
#define LOG_START1(fmt,a)           log_start(LOG_FMT(fmt),a,__FILE__,__LINE__);
#define LOG_START2(fmt,a,b)         log_start(LOG_FMT(fmt),a,b,__FILE__,__LINE__);
#define LOG_START3(fmt,a,b,c)       log_start(LOG_FMT(fmt),a,b,c,__FILE__,__LINE__);
#define LOG_START4(fmt,a,b,c,d)     log_start(LOG_FMT(fmt),a,b,c,d,__FILE__,__LINE__);
#define LOG_START5(fmt,a,b,c,d,e)   log_start(LOG_FMT(fmt),a,b,c,d,e,__FILE__,__LINE__);
#define LOG_ADD0(fmt)               log_add(LOG_FMT(fmt),__FILE__,__LINE__)
#define LOG_ADD1(fmt,a)             log_add(LOG_FMT(fmt),a,__FILE__,__LINE__)
#define LOG_ADD2(fmt,a,b)           log_add(LOG_FMT(fmt),a,b,__FILE__,__LINE__)
#define LOG_ADD3(fmt,a,b,c)         log_add(LOG_FMT(fmt),a,b,c,__FILE__,__LINE__)
#define LOG_ADD4(fmt,a,b,c,d)       log_add(LOG_FMT(fmt),a,b,c,d,__FILE__,__LINE__)
#define LOG_ADD5(fmt,a,b,c,d,e)     log_add(LOG_FMT(fmt),a,b,c,d,e,__FILE__,__LINE__)
#define LOG_SERROR0(fmt)            log_serror(LOG_FMT(fmt),__FILE__,__LINE__)
#define LOG_SERROR1(fmt,a)          log_serror(LOG_FMT(fmt),a,__FILE__,__LINE__)
#define LOG_SERROR2(fmt,a,b)        log_serror(LOG_FMT(fmt),a,b,__FILE__,__LINE__)
#define LOG_SERROR3(fmt,a,b,c)      log_serror(LOG_FMT(fmt),a,b,c,__FILE__,__LINE__)
#define LOG_SERROR4(fmt,a,b,c,d)    log_serror(LOG_FMT(fmt),a,b,c,d,__FILE__,__LINE__)
#define LOG_SERROR5(fmt,a,b,c,d,e)  log_serror(LOG_FMT(fmt),a,b,c,d,e,__FILE__,__LINE__)

void log_clear();
void log_start(
    const char* const	fmt,
    ...);
void log_errno(void);
void log_add(
    const char *const   fmt,
    ...);
void log_log(
    const int		level);

/*
 * Write a system error.  This is a convenience function for the sequence
 *      log_errno();
 *      log_add(fmt, ...);
 *      log_log(LOG_ERR);
 *
 * ARGUMENTS:
 *      fmt             The format-string for the log message.
 *      ...             Arguments for the format-string.
 */
void log_serror(
    const char *const   fmt,
    ...);

#ifdef __cplusplus
}
#endif

#endif
