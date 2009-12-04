#ifndef LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_clear();
void log_vadd(
    const char *const   fmt,
    va_list		args);
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
