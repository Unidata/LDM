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

extern int
sprint_timestampt(char *buf, size_t bufsize, const timestampt *tvp);

extern int
sprint_feedtypet(char *buf, size_t bufsize, feedtypet feedtype);

extern char *
s_feedtypet(feedtypet feedtype);

extern char *
s_rendezvoust(char *buf, size_t bufsize, const rendezvoust *rdv);

extern int
sprint_signaturet(char *buf,
	size_t bufsize, const signaturet signaturep);

extern char *
s_signaturet(char *buf, size_t bufsize, const signaturet signaturep);

extern int
sprint_prod_spec(char *buf,
	size_t bufsize, const prod_spec *specp);

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
