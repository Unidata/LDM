/*
 *   Copyright 2014, University Corporation for Atmospheric Research.
 *   All rights reserved. See file COPYRIGHT in the top-level source-directory
 *   for copying and redistribution conditions.
 */

#ifndef _LDMPRINT_H_
#define _LDMPRINT_H_
#include "ldm.h"
#include <stddef.h>	/* size_t */
#include <stdarg.h>	/* size_t */
#if defined(__cplusplus) || defined(__STDC__)

/**
 * Returns formatted arguments.
 *
 * @param[in] initSize  The initial size of the formatting buffer.
 * @param[in] fmt       The format for the arguments.
 * @param[in] args      The arguments to be formatted.
 * @retval    NULL      Error. `log_add()` called.
 * @return              Pointer to the string buffer containing the formatted
 *                      arguments. The caller should free when it's no longer
 *                      needed.
 */
char*
ldm_vformat(
    const size_t      initSize,
    const char* const fmt,
    va_list           args);

/**
 * Returns formatted arguments.
 *
 * @param[in] initSize  The initial size of the formatting buffer.
 * @param[in] fmt       The format for the arguments.
 * @param[in] ...       The arguments to be formatted.
 * @retval    NULL      Error. `log_add()` called.
 * @return              Pointer to the string buffer containing the formatted
 *                      arguments. The caller should free when it's no longer
 *                      needed.
 */
char*
ldm_format(
    const size_t               initSize,
    const char* const restrict fmt,
    ...);

extern int
sprint_time_t(char *buf, size_t bufsize, time_t ts); /* obsolete ? */

/**
 * Returns the string representation of a timestamp.
 *
 * @param[in]  pc    The timestamp to be formatted.
 * @param[out] buf   The buffer into which to format the timestamp.
 *                   May be NULL only if `size == 0`.
 * @param[in]  size  The size of the buffer in bytes.
 * @retval     -1    The timestamp couldn't be formatted.
 * @return           The number of characters that it takes to format the
 *                   timestamp (excluding the terminating NUL). If equal to or
 *                   greater than `size`, then the returned string is not
 *                   NUL-terminated.
 */
int ts_format(
        const timestampt* const ts,
        char*                   buf,
        size_t                  size);

/**
 * Formats a timestamp. Deprecated in favor of `ts_format()`
 *
 * @param[out] buf      Buffer.
 * @param[in]  bufsize  Size of buffer in bytes.
 * @param[in]  tvp      Timestamp.
 * @retval     -1       `buf == NULL`, buffer is too small, or `bufsize >
 *                      {INT_MAX}`.
 * @return              Number of bytes written excluding terminating NUL.
 */
extern int
sprint_timestampt(char *buf, size_t bufsize, const timestampt *tvp);

/**
 * Returns the string representation of a feedtype.
 *
 * @param[in]  feedtype  The feedtype to be formatted.
 * @param[out] buf       The buffer into which to format the feedtype. May be
 *                       NULL only if `size == 0`.
 * @param[in]  size      The size of the buffer in bytes.
 * @retval     -1        The feedtype can't be formatted.
 * @return               The number of characters that it takes to format the
 *                       feedtype (excluding the terminating NUL). If equal to
 *                       or greater than `size`, then the returned string is not
 *                       NUL-terminated.
 */
int
ft_format(
        feedtypet    feedtype,
        char* const  buf,
        const size_t size);

// Deprecated in favor of `ft_format()`
extern int
sprint_feedtypet(char *buf, size_t bufsize, feedtypet feedtype);

extern const char *
s_feedtypet(feedtypet feedtype);

extern char *
s_rendezvoust(char *buf, size_t bufsize, const rendezvoust *rdv);

extern int
sprint_signaturet(char *buf,
	size_t bufsize, const signaturet signaturep);

extern char *
s_signaturet(char *buf, size_t bufsize, const signaturet signaturep);

/**
 * Returns the string representation of a product-specification.
 *
 * @param[in]  ps    The product-specification to be formatted.
 * @param[out] buf   The buffer into which to format the product-specification.
 *                   May be NULL only if `size == 0`.
 * @param[in]  size  The size of the buffer in bytes.
 * @retval     -1    The product-specification couldn't be formatted.
 * @return           The number of characters that it takes to format the
 *                   product-specification (excluding the terminating NUL). If
 *                   equal to or greater than `size`, then the returned string
 *                   is not NUL-terminated.
 */
int
ps_format(
        const prod_spec* const restrict ps,
        char* const restrict            buf,
        const size_t                    size);

// Deprecated in favor of `ps_format()`
extern int
sprint_prod_spec(char *buf,
	size_t bufsize, const prod_spec *specp);

/**
 * Returns the string representation of a product-class.
 *
 * @param[in]  pc    The product-class to be formatted.
 * @param[out] buf   The buffer into which to format the product-class.
 *                   May be NULL only if `size == 0`.
 * @param[in]  size  The size of the buffer in bytes.
 * @retval     -1    The product-class couldn't be formatted.
 * @return           The number of characters that it takes to format the
 *                   product-class (excluding the terminating NUL). If equal to
 *                   or greater than `size`, then the returned string is not
 *                   NUL-terminated.
 */
int pc_format(
        const prod_class_t* const pc,
        char*                     buf,
        size_t                    size);

extern char *
s_prod_class(char *buf,
	size_t bufsize, const prod_class_t *clssp);

extern char *
s_prod_info(char *buf, size_t bufsize, const prod_info *infop,
	int doSignature);

extern char *
s_ldm_errt(ldm_errt code);

extern char *
s_ldmproc(unsigned long proc);

/*
 * Parses a formatted signature.
 *
 * Arguments:
 *	string	Pointer to the formatted signature.
 * Returns:
 *	-1	Failure.  log_errno() called.
 *	else	Number of bytes parsed.
 */
int
sigParse(
    const char* const	string,
    signaturet* const	signature);

#else /* Old Style C */
/* TODO Will not be done, we now specify an ansi compiler */
#endif

#endif /* !_LDMPRINT_H_ */
