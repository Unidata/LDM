/*
 *   Copyright 2011, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */

#include <config.h>

#include <log.h>
#include <stdarg.h>
#include <stddef.h>   /* NULL */
#include <stdio.h>    /* vsnprintf(), snprintf() */
#include <stdlib.h>   /* malloc(), free(), abort() */
#include <string.h>
#include <strings.h>  /* strdup() */

#include "log.h"

#include "error.h"


struct error {
    char        msg[512];
    ErrorObj*    cause;
    const char* file;
    const char* func;
    size_t      msglen;
    int         code;
    unsigned    line;
};


/******************************************************************************
 * Public API:
 ******************************************************************************/


ErrorObj*
err_new(
    const int           code,
    ErrorObj* const     cause,
    const char* const   file,
    const char* const   func,
    const unsigned      line, 
    const char* const   fmt,
    ...)
{
    ErrorObj *err;

    log_assert(file != NULL);

    err = (ErrorObj*)malloc(sizeof(ErrorObj));

    if (NULL == err) {
        log_syserr("malloc(%lu) failure",
            (unsigned long)sizeof(ErrorObj));
        abort();
    }
    else {
        err->line = line;
        err->file = file;
        err->func = func;

        if (NULL == fmt) {
            err->msg[0] = 0;
            err->msglen = 0;
            err->code = code;
            err->cause = cause;
        }
        else {
            va_list     args;
            int         nbytes;
            size_t      size = sizeof(err->msg);

            va_start(args, fmt);

            nbytes = vsnprintf(err->msg, size, fmt, args);
            if (nbytes < 0) {
                nbytes = snprintf(err->msg, size, 
                    "err_new(): vsnprintf() failure: \"%s\"", fmt);
            }
            else if (nbytes >= size) {
                nbytes = size - 1;
            }

            va_end(args);

            err->msg[nbytes] = 0;
            err->msglen = (size_t)nbytes;

            log_assert(err->msglen < size);

            err->code = code;
            err->cause = cause;
        }
    }                                   /* "err" allocated */

    return err;
}


int
err_code(
    const ErrorObj*     err)
{
    log_assert(err != NULL);

    return err->code;
}


ErrorObj*
err_cause(
    const ErrorObj*     err)
{
    log_assert(err != NULL);

    return err->cause;
}


const char*
err_message(
    const ErrorObj*     err)
{
    return err->msg;
}

static void err_log_r(
        const ErrorObj* const err,
        const unsigned        level)
{
    if (err->cause)
        err_log_r(err->cause, level);
    const log_loc_t loc = {err->file, err->func, err->line};
    logl_log(&loc, level, "%.*s", (int)err->msglen, err->msg);
}


/*
 * This function is not re-entrant because it contains static variables that are
 * potentially modified on every invocation.
 */
void
err_log(
    const ErrorObj* const       err,
    const enum err_level        level)
{
    static const unsigned log_levels[] = {
        LOG_LEVEL_ERROR,
        LOG_LEVEL_WARNING,
        LOG_LEVEL_NOTICE,
        LOG_LEVEL_INFO,
        LOG_LEVEL_DEBUG
    };
#if 1
    if (log_is_level_enabled(log_levels[level]))
        err_log_r(err, log_levels[level]);
#else

    if (log_is_level_enabled(log_levels[level])) {
        const ErrorObj*          e;
        const char* const       stdErrSep = "; ";
        const char* const       stdLocSep = ": ";
        const char*             errSep;
        const char*             locSep;

        static char             initialBuf[1024];
        static char*            buf = initialBuf;
        static size_t           buflen = sizeof(initialBuf);

        log_assert(err != NULL);

        {
            size_t          totlen = 0;

            errSep = "";

            for (e = err; e != NULL; e = e->cause) {
                totlen += strlen(errSep);

                errSep = "";

                if (ERR_DEBUG != level) {
                    locSep = "";
                }
                else {
                    totlen += sprintf(buf, "%s@%u", e->file, e->line);
                    locSep = stdLocSep;
                    errSep = stdErrSep;
                }

                if (0 < e->msglen) {
                    totlen += strlen(locSep) + e->msglen;
                    errSep = stdErrSep;
                }
            }

            totlen++;                   /* + '\0' */

            if (NULL == buf) {
                buf = (char*)malloc(totlen);
                buflen = totlen;
            }
            else if (totlen > buflen) {
                if (buf != initialBuf)
                    free(buf);

                buf = (char*)malloc(totlen);
                buflen = totlen;
            }

            log_assert(NULL != buf);
        }

        {
            char           *cp = buf;

            errSep = "";

            for (e = err; e != NULL; e = e->cause) {
                (void)strcpy(cp, errSep);
                cp += strlen(errSep);

                errSep = "";

                if (ERR_DEBUG != level) {
                    locSep = "";
                }
                else {
                    cp += sprintf(cp, "%s@%u", e->file, e->line);
                    locSep = stdLocSep;
                    errSep = stdErrSep;
                }

                if (0 < e->msglen) {
                    (void)strcpy(cp, locSep);
                    cp += strlen(locSep);

                    (void)memcpy(cp, e->msg, e->msglen);
                    cp += e->msglen;

                    errSep = stdErrSep;
                }
            }

            *cp = 0;
        }

        /*
         * NB: The message is not used as the format parameter because "buf"
         * might have formatting characters in it (e.g., "%") from, for example,
         * a call to "s_prod_info()" with a dangerous product-identifier.
         */
        log_log_q(log_levels[level], "%s", buf);
    }
#endif
}


void
err_free(
    ErrorObj* const     err)
{
    if (err != NULL) {
        if (err->cause != NULL)
            err_free(err->cause);

        free((void*)err);
    }
}


/*
 * Logs the error and then frees it.  This function is equivalent to
 *      err_log(err);
 *      err_free(err);
 *
 * Arguments:
 *      err     The error.
 *      level   The logging level.
 */
void
err_log_and_free(
    ErrorObj* const             err,
    const enum err_level        level)
{
    err_log(err, level);
    err_free(err);
}
