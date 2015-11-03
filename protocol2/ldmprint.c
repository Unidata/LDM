/*
 *   Copyright 2015, University Corporation for Atmospheric Research
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */

/* 
 * Utility functions for printing contents of some protocol data structures
 */
#include <config.h>

#include "ldm.h"
#include "ldmprint.h"
#include "atofeedt.h"           /* for fassoc[] */
#include "log.h"
#include <timestamp.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <rpc/rpc.h>
#include <netinet/in.h>

static char tprintbuf[1987];
static const char nada[] = "(null)";

/**
 *
 * @param[in]     n       The number of bytes just attempted to be written to
 *                        `*cp` or `-1`.
 * @param[in,out] nbytes  The number of bytes to be incremented if `n >= 0`;
 *                        otherwise, set to `-1`.
 * @param[in,out] cp      The pointer into the character buffer to be
 *                        incremented iff `n >= 0 && n <= *left`;
 * @param[in,out] left    The number of unwritten bytes remaining in the
 *                        character buffer, including the terminating NUL.
 *                        Decremented by `n` iff `n >= 0 && n <= *left`. Set to
 *                        `0` iff `n >= 0 && n > *left`.
 * @retval true iff the write attempt was successful and was NUL-terminated:
 *              `n >= 0 && n <= *left`.
 */
static bool post_snprintf(
        const int              n,
        int* const restrict    nbytes,
        char** const restrict  cp,
        size_t* const restrict left)
{
    if (n < 0) {
        *nbytes = -1;
        return false;
    }

    *nbytes += n;
    if (n > *left) {
        *left = 0;
    }
    else {
        *cp += n;
        *left -= n;
    }
    return true;
}

/**
 * Returns an attempt at formatting arguments. This function is thread-safe.
 *
 * @param[in]  initSize  The size to allocate for the formatting buffer,
 *                       including the terminating NUL character.
 * @param[in]  fmt       The format for the arguments.
 * @param[in]  args      The arguments to be formatted. Must have been
 *                       initialized by `va_start()` or `va_copy()`.
 * @param[out] nbytes    The number of bytes that would be written to the
 *                       buffer -- excluding the terminating NUL character --
 *                       if it was sufficiently capacious.
 * @retval     NULL      Error. `log_start()` called.
 * @return               Pointer to the NUL-terminated string buffer containing
 *                       the formatted arguments. The caller should free when
 *                       it's no longer needed.
 */
static char*
tryFormat(
    const size_t               size,
    const char* const restrict fmt,
    va_list                    args,
    int* const restrict        nbytes)
{
    char* buf = LOG_MALLOC(size, "formatting buffer");

    if (buf)
        *nbytes = vsnprintf(buf, size, fmt, args);

    return buf;
}

/**
 * Returns formatted arguments. This function is thread-safe.
 *
 * @param[in] initSize  The initial size of the formatting buffer, including the
 *                      terminating NUL character.
 * @param[in] fmt       The format for the arguments.
 * @param[in] args      The arguments to be formatted. Must have been
 *                      initialized by `va_start()` or `va_copy()`.
 * @retval    NULL      Error. `log_start()` called.
 * @return              Pointer to the string buffer containing the formatted
 *                      arguments. The caller should free when it's no longer
 *                      needed.
 */
char*
ldm_vformat(
    const size_t      initSize,
    const char* const fmt,
    va_list           args)
{
    int     nbytes;
    va_list ap;

    va_copy(ap, args);

    char* buf = tryFormat(initSize, fmt, args, &nbytes);
    if (buf) {
        if (nbytes >= initSize) {
            free(buf);
            buf = tryFormat(nbytes+1, fmt, ap, &nbytes);
        }
    }

    va_end(ap);

    return buf;
}

/**
 * Returns formatted arguments. This function is thread-safe.
 *
 * @param[in] initSize  The initial size of the formatting buffer, including the
 *                      terminating NUL character.
 * @param[in] fmt       The format for the arguments.
 * @param[in] ...       The arguments to be formatted.
 * @retval    NULL      Error. `log_start()` called.
 * @return              Pointer to the string buffer containing the formatted
 *                      arguments. The caller should free when it's no longer
 *                      needed.
 */
char*
ldm_format(
    const size_t               initSize,
    const char* const restrict fmt,
    ...)
{
    va_list args;

    va_start(args, fmt);
    char* buf = ldm_vformat(initSize, fmt, args);
    va_end(args);
    return buf;
}


/**
 * Formats a timestamp.
 *
 * @param[out] buf      Buffer.
 * @param[in]  bufsize  Size of buffer in bytes.
 * @param[in]  ts       Timestamp.
 * @retval     -1       Buffer is too small.
 * @return              Number of bytes written excluding terminating NUL.
 */
int
sprint_time_t(char *buf, size_t bufsize, time_t ts)
{
        struct tm tm_ts;
        size_t len;

#define P_TIMET_LEN 15 /* YYYYMMDDHHMMSS\0 */
        if(!buf || bufsize < P_TIMET_LEN)
                return -1;

        (void)gmtime_r(&ts, &tm_ts);
        len = strftime(buf, bufsize, "%Y%m%d%H%M%S", &tm_ts);
        return (int)len;
}


/**
 * Returns the string representation of a timestamp.
 *
 * @param[in]  ts    The timestamp to be formatted.
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
        size_t                  size)
{
    int nbytes;

    if (buf == NULL && size) {
        nbytes = -1;
    }
    else {
        int n;

        nbytes = 0;

        if (tvEqual(*ts, TS_NONE)) {
            n = snprintf(buf, size, "TS_NONE");
            (void)post_snprintf(n, &nbytes, &buf, &size);
        }
        else if (tvEqual(*ts, TS_ZERO)) {
            n = snprintf(buf, size, "TS_ZERO");
            (void)post_snprintf(n, &nbytes, &buf, &size);
        }
        else if (tvEqual(*ts, TS_ENDT)) {
            n = snprintf(buf, size, "TS_ENDT");
            (void)post_snprintf(n, &nbytes, &buf, &size);
        }
        else {
            n = sprint_time_t(buf, size, ts->tv_sec);
            if (post_snprintf(n, &nbytes, &buf, &size)) {
                n = snprintf(buf, size, ".%06d", (int)(ts->tv_usec));
                (void)post_snprintf(n, &nbytes, &buf, &size);
            }
        }
    }

    return nbytes;
}


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
int
sprint_timestampt(char *buf, size_t bufsize, const timestampt *tvp)
{
    #define P_TIMESTAMP_LEN (P_TIMET_LEN + 7) // "YYYYMMDDhhmmss.uuuuuu\0"
    if(!buf || bufsize < P_TIMESTAMP_LEN)
            return -1;

    return ts_format(tvp, buf, bufsize);
}


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
int ft_format(
        feedtypet    feedtype,
        char* const  buf,
        const size_t size)
{
    int nbytes;

    if (buf == NULL && size) {
        nbytes = -1;
    }
    else if (feedtype == NONE) {
        nbytes = snprintf(buf, size, "%s", "NONE");
    }
    else {
        static struct fal* anyEntry = NULL;

        if (anyEntry == NULL) { // Compute and save for subsequent calls
            // Find the most inclusive feedtype, to work backwards from
            for (anyEntry = fassoc;
                    anyEntry->type != ANY && anyEntry->name != NULL;
                    anyEntry++)
                ;
            if (anyEntry->name == NULL) {
                // Who took ANY out of the feedtype name table?
                anyEntry = NULL;
            }
        }

        if (anyEntry == NULL) {
            nbytes = -1;
        }
        else {
            char*       cp = buf;
            size_t      left = size;
            struct fal* ftEntry = anyEntry; // Start at ANY and work backwards

            nbytes = 0;

            while (feedtype && ftEntry->type != NONE) {
                if ((ftEntry->type & feedtype) == ftEntry->type) { // Match
                    int n = snprintf(cp, left, nbytes ? "|%s" : "%s", ftEntry->name);
                    if (!post_snprintf(n, &nbytes, &cp, &left))
                        break;
                    feedtype &= ~ftEntry->type;
                }
                ftEntry--;
            }

            if (buf && nbytes >= 0) {
                // Capitalize it
                for (cp = buf; cp < buf + size && *cp; cp++)
                    if (islower(*cp))
                        *cp = toupper(*cp);

                if (feedtype) {
                    // Handle error, some unnamed bits in there
                    int n = snprintf(cp, left, nbytes ? "|0x%08x" : "0x%08x",
                            (unsigned)feedtype);
                    (void)post_snprintf(n, &nbytes, &cp, &left);
                }
            }
        }
    }

    return nbytes;
}


/*
 * TODO: needs work
 *
 * @return               The number of characters encoded.
 * @return -1            if the buffer is NULL or the buffer size is less than
 *                       129.
 * @return -1            if feedtype table doesn't contain "any".
 * @return -1            if the given feedtype can't be encoded.
 */
int
sprint_feedtypet(char *buf, size_t bufsize, feedtypet feedtype)
{
    // Maximum number of bytes of any feedtype expression we will construct
    #define FDTT_SBUF_SIZE (128)
    if(buf == NULL || bufsize < FDTT_SBUF_SIZE +1)
        return -1;

    return ft_format(feedtype, buf, bufsize);
}

/**
 * Returns the formatted representation of a feedtype.
 *
 * @param[in] feedtype  Feedtype.
 * @return              String representation of feedtype. Caller must not free.
 */
const char *
s_feedtypet(feedtypet feedtype)
{
        static char buf[FDTT_SBUF_SIZE +1];
        if(sprint_feedtypet(buf, sizeof(buf), feedtype) <= 0)
                return NULL;
        return buf;
}

static int
sprint_ldm_addr_rpc(char *buf, size_t bufsize, const ldm_addr_rpc *rdv) 
{
        if(buf == NULL)
        {
                buf = tprintbuf;
                bufsize = sizeof(tprintbuf);
        }
#define RA_SBUF_SIZE (HOSTNAMESIZE +1 +11+1 +11+1)
        if(bufsize < RA_SBUF_SIZE + 1)
                return -1;
        (void) memset(buf, 0, bufsize);

        if(rdv == NULL)
                return sprintf(buf, "%s", nada);
        
        /* else */
        return sprintf(buf, "%s %11lu %11lu",
                        rdv->hostname, 
                        rdv->prog,
                        rdv->vers
                        );
}

static const char *
s_proto(int protocol)
{
#define PROTO_SBUF_SIZE 3
        switch (protocol) {
        case IPPROTO_TCP:
                return "tcp";
        case IPPROTO_UDP:
                return "udp";
        }
        return "UNK";
}

static int
sprint_ldm_addr_ip(char *buf, size_t bufsize, const ldm_addr_ip *rdv) 
{
        if(buf == NULL)
        {
                buf = tprintbuf;
                bufsize = sizeof(tprintbuf);
        }
#define RI_SBUF_SIZE (PROTO_SBUF_SIZE + 1 + 5 + 1 + 16)
        if(bufsize < RI_SBUF_SIZE + 1)
                return -1;
        (void) memset(buf, 0, bufsize);

        if(rdv == NULL)
                return sprintf(buf, "%s", nada);
        
        /* else */
        /* TODO: dotted quad print of rdv->addr */
        return sprintf(buf, "%s %5hu 0x%08lx",
                        s_proto(rdv->protocol), 
                        rdv->port,
                        rdv->addr);
}


char *
s_rendezvoust(char *buf, size_t bufsize, const rendezvoust *rdv) 
{
        if(rdv != NULL)
        {
                switch (rdv->type) {
                case LDM_ADDR_RPC:
                        if(sprint_ldm_addr_rpc(buf, bufsize,
                                        &rdv->rendezvoust_u.rpc) > 0)
                                return buf;
                        break;
                case LDM_ADDR_IP:
                        if(sprint_ldm_addr_ip(buf, bufsize,
                                        &rdv->rendezvoust_u.ip) > 0)
                                return buf;
                        break;
                case LDM_ADDR_NONE:
                default:
                        break;
                }
        }
        (void) sprintf(buf, "%s", nada);
        return buf;
}


/**
 * Formats a signature.
 *
 * @param[out] buf        Buffer.
 * @param[in]  bufsize    Size of buffer in bytes.
 * @param[in]  signature  Signature to be formatted.
 * @retval     0         `buf == NULL` or is too small.
 * @return                Number of bytes written excluding terminating NUL.
 */
int
sprint_signaturet(char *buf, size_t bufsize, const signaturet signature)
{
    int    len = 0;
    size_t sigLen = sizeof(signaturet);

    if(buf != NULL && bufsize >= 2*sigLen + 1) {
        char                *bp = buf;
        const unsigned char *sp = (unsigned char*)signature;;

        while (sp < (unsigned char*)signature + sigLen)
            bp += sprintf(bp, "%02x", *sp++);

        len = (int)(bp - buf);

        assert(len == 2*sigLen);
    }

    return len;
}


/*
 * Returns a string representation of a product signature.  If the user-
 * supplied buffer is NULL, then a static, internal buffer is used.  This
 * buffer is overwritten by each, successive call.
 *
 * buf                 The buffer into which to put the string representation
 *                     or NULL.
 * bufsize             The size of the buffer in bytes.
 * signaturep          The signature whose string representation is desired.
 *
 * Return
 *   NULL              If the user-supplied buffer is too small.
 *   <else>            A pointer to the buffer containing the string
 *                     representation.
 */
char* s_signaturet(char *buf, size_t bufsize, const signaturet signaturep)
{
        if(buf == NULL)
        {
                buf = tprintbuf;
                bufsize = sizeof(tprintbuf);
        }
        
        {
                const int len = sprint_signaturet(buf, bufsize, signaturep);
                if(len < 2 * sizeof(signaturet))
                        return NULL;
        }
        return buf;
}


/**
 * Parses a formatted signature.
 *
 * @param[in]  string     Pointer to the formatted signature.
 * @param[out] signature  Pointer to the data-product MD5 signature.
 * @retval     -1         Failure. \c log_start() called.
 * @return                Number of bytes parsed.
 */
int
sigParse(
    const char* const   string,
    signaturet* const   signature)
{
    int                 i;
    signaturet          tmpSig;
    int                 nbytes;

    errno = 0;

    for (i = 0; i < sizeof(signaturet); i++) {
        unsigned        value;

        if (sscanf(string + 2*i, "%2x", &value) != 1)
            break;

        tmpSig[i] = (unsigned char)value;
    }

    if (i != sizeof(signaturet)) {
        LOG_SERROR0();
        nbytes = -1;
    }
    else {
        (void)memcpy(signature, tmpSig, sizeof(signaturet));
        nbytes = 2*sizeof(signaturet);
    }

    return nbytes;
}


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
int ps_format(
        const prod_spec* const ps,
        char*                  buf,
        size_t                 size)
{
    int nbytes = 0;

    if(buf == NULL && size) {
        nbytes = -1;
    }
    else if (ps == NULL) {
        nbytes = snprintf(buf, size, "%s", nada);
    }
    else {
        int n = snprintf(buf, size, "{");

        if (post_snprintf(n, &nbytes, &buf, &size)) {
            n = ft_format(ps->feedtype, buf, size);

            if (post_snprintf(n, &nbytes, &buf, &size)) {
                n = snprintf(buf, size, ", \"%s\"}",
                        ps->pattern ? ps->pattern : nada);

                (void)post_snprintf(n, &nbytes, &buf, &size);
            }
        }
    }

    return nbytes;
}


/**
 * Deprecated in favor of `ps_format()`.
 */
int
sprint_prod_spec(char *buf,
        size_t bufsize, const prod_spec *specp)
{
    #define MIN_PSPECLEN (1 + 7 + 1 + 1 + 1 + 1 +1)
    #define MAX_PSPECLEN (MIN_PSPECLEN + MAXPATTERN)
    if(buf == NULL || (bufsize < MAX_PSPECLEN
                    && bufsize < MIN_PSPECLEN
                     + strlen(specp ? specp->pattern : nada)))
        return -1;

    return ps_format(specp, buf, bufsize);
}


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
        size_t                    size)
{
    int nbytes;

    if(buf == NULL && size) {
        nbytes = -1;
    }
    else if (pc == NULL) {
        nbytes = snprintf(buf, size, "%s", nada);
    }
    else {
        nbytes = 0;

        int n = ts_format(&pc->from, buf, size);

        if (post_snprintf(n, &nbytes, &buf, &size)) {
            n = snprintf(buf, size, " ");

            if (post_snprintf(n, &nbytes, &buf, &size)) {
                n = ts_format(&pc->to, buf, size);

                if (post_snprintf(n, &nbytes, &buf, &size)) {
                    n = snprintf(buf, size, " {");

                    if (post_snprintf(n, &nbytes, &buf, &size)) {
                        const unsigned psa_len = pc->psa.psa_len;

                        for(int i = 0; size > 0 && i < psa_len; i++) {
                            if (i) {
                                n = snprintf(buf, size, ",");
                                if (!post_snprintf(n, &nbytes, &buf, &size))
                                    break;
                            }

                            n = ps_format(&pc->psa.psa_val[i], buf, size);

                            if (!post_snprintf(n, &nbytes, &buf, &size))
                                break;
                        }

                        if (nbytes >= 0) {
                            n = snprintf(buf, size, "}");
                            (void)post_snprintf(n, &nbytes, &buf, &size);
                        }
                    } // " {" printed
                } // `pc->to` printed
            } // space printed
        } // `pc->from` printed
    } // typical case

    return nbytes;
}


/**
 * Deprecated in favor of `pc_format()`.
 */
char *
s_prod_class(char *buf,
        size_t bufsize, const prod_class_t *clssp)
{
    if (buf == NULL) {
        buf = tprintbuf;
        bufsize = sizeof(tprintbuf);
    }

    if (bufsize < 2 * P_TIMESTAMP_LEN + MAX_PSPECLEN)
        return NULL;

    return pc_format(clssp, buf, bufsize) < 0 ? NULL : buf;
}


/**
 * Formats product information. Thread safe if and only if `buf != NULL`.
 *
 * @param[out] buf          Output buffer or NULL, in which case a static buffer
 *                          is used.
 * @param[in]  bufsize      Size of buffer in bytes. Ignored if `buf == NULL`.
 *                          Should be at least `LDM_INFO_MAX`.
 * @param[in]  infop        Product information.
 * @param[in]  doSignature  Whether or not to format the signature.
 * @retval     NULL         Buffer is too small or product information couldn't
 *                          be formatted.
 * @return                  `buf`. Success. String is NUL-terminated.
 */
char *
s_prod_info(
        char* restrict                  buf,
        size_t                          bufsize,
        const prod_info* const restrict infop,
        const int                       doSignature)
{
    int    nbytes; // number of bytes encoded excluding terminating NUL
    size_t len = 0; // current number of formatted bytes in `buf` excluding NUL

    if (buf == NULL) {
        buf = tprintbuf;
        bufsize = sizeof(tprintbuf);
    }

    if (bufsize < LDM_INFO_MAX - (doSignature ? 0 : 33))
         return NULL;

    if (doSignature) {
        nbytes = sprint_signaturet(buf+len, bufsize, infop->signature);
        if (nbytes == 0)
            return NULL;
        len += nbytes;
        bufsize -= nbytes;
    }

    nbytes = snprintf(buf+len, bufsize, len ? " %10u " : "%10u ", infop->sz);
    if (nbytes < 0 || nbytes >= bufsize)
        return NULL;
    len += nbytes;
    bufsize -= nbytes;

    nbytes = sprint_timestampt(buf+len, bufsize, &infop->arrival);
    if (nbytes < 0 || nbytes >= bufsize)
        return NULL;
    len += nbytes;
    bufsize -= nbytes;

    nbytes = snprintf(buf+len, bufsize, " ");
    if (nbytes < 0 || nbytes >= bufsize)
        return NULL;
    len += nbytes;
    bufsize -= nbytes;

    nbytes = ft_format(infop->feedtype, buf+len, bufsize);
    if (nbytes < 0 || nbytes >= bufsize)
        return NULL;
    len += nbytes;
    bufsize -= nbytes;

    nbytes = snprintf(buf+len, bufsize, " %03u  %s", infop->seqno,
            infop->ident);
    if (nbytes < 0 || nbytes >= bufsize)
        return NULL;

    return buf;
}


char *
s_ldm_errt(ldm_errt code)
{
        switch (code) {
        case OK : return "OK";
        case SHUTTING_DOWN : return "SHUTTING_DOWN";
        case DONT_SEND : return "DONT_SEND";
        case RESTART : return "RESTART";
        case REDIRECT : return "REDIRECT";
        case RECLASS : return "RECLASS";
        }
        /* default */
        return "";
}


char *
s_ldmproc(unsigned long proc)
{
        switch (proc) {
        case 0  : return "NULLPROC";
        case FEEDME : return "FEEDME";
        case HIYA : return "HIYA";
        case NOTIFICATION : return "NOTIFICATION";
        case NOTIFYME : return "NOTIFYME";
        case COMINGSOON : return "COMINGSOON";
        case BLKDATA : return "BLKDATA";
        }
        /* default */
        {
                static char buf[24];
                (void)sprintf(buf, "%lu", proc);
                return buf;
        }
}

#ifdef TEST_S_FEEDTYPET
#include <stdio.h>

int
main()
{
    feedtypet in;

    while ( scanf("%12d", &in) != EOF ) {
        printf("%d\t0x%08x\t\t%s\n", in, in, s_feedtypet(in));
    }
    return 0;
}

#endif
