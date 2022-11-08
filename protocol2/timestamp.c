/*
 *   Copyright 2011, University Corporation for Atmospheric Research
 *   See file COPYRIGHT in the top-level source-directory for copying and
 *   redistribution conditions.
 */

#include <config.h>

#include "log.h"
#include "timestamp.h"
#include "unistd.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#ifndef NDEBUG
#define pIf(a,b) (!(a) || (b))  /* a implies b */
#endif

const struct timeval TS_NONE = {-1, -1};
const struct timeval TS_ZERO = {0, 0};
const struct timeval TS_ENDT = {0x7fffffff, 999999};

int
set_timestamp(timestampt *tsp)
{
        int status = 0;
        if(gettimeofday(tsp, NULL) < 0)
        {
                /* should never happen ... */
                status = errno;
                log_error_q("gettimeofday: %s", strerror(status));
        }
        return status;
}

void
swap_timestamp(timestampt *fr, timestampt *to)
{
        timestampt tmp;
        tmp = *fr;
        *fr = *to;
        *to = tmp;
}


bool
xdr_timestampt(XDR *xdrs, timestampt *tvp)
{
        log_assert(pIf(xdrs->x_op == XDR_ENCODE,
                        (tvp->tv_sec  >= TS_ZERO.tv_sec
                        && tvp->tv_usec >= TS_ZERO.tv_usec
                        && tvp->tv_sec  <= TS_ENDT.tv_sec
                        && tvp->tv_usec <= TS_ENDT.tv_usec)));

        /*
         * TV_INT
         * On DEC alpha, tvp->tv_sec is an int.
         * On IRIX, tvp->tv_sec is an time_t, which is 32 bits,
         * which may be  a 'long' or an 'int'.
         * The use of intermediate variables is an attempt
         * to cover all bases.
         */
        /* TODO: use the preprocessor to determine this */
        
        if(xdrs->x_op == XDR_ENCODE)
        {
                long tv_sec = (long)tvp->tv_sec; /* const */
                if (!xdr_long(xdrs, &tv_sec)) {
                         return (FALSE);
                }
        }
        else
        {
                long tv_sec = TS_NONE.tv_sec;
                if (!xdr_long(xdrs, &tv_sec)) {
                        return (FALSE);
                }
                tvp->tv_sec = tv_sec; /* ignore any warning */
                
        }

        if(xdrs->x_op == XDR_ENCODE)
        {
                long tv_usec = (long)tvp->tv_usec; /* const */
                if (!xdr_long(xdrs, &tv_usec)) {
                         return (FALSE);
                }
        }
        else
        {
                long tv_usec = TS_NONE.tv_usec;
                if (!xdr_long(xdrs, &tv_usec)) {
                         return (FALSE);
                }
                tvp->tv_usec = tv_usec; /* ignore any warning */
        }

        return (TRUE);
}


timestampt
timestamp_add(const timestampt *const left, const timestampt *const rght)
{
        timestampt tv;
        tv = TS_ZERO;

        if(left == NULL || rght == NULL)
                return tv;

        tv.tv_sec = left->tv_sec + rght->tv_sec;
        tv.tv_usec = left->tv_usec + rght->tv_usec;
        if(tv.tv_usec >= 1000000)
        {
                tv.tv_sec += 1;
                tv.tv_usec -= 1000000;
        }
        return tv;
}


/*
 * Increment a timestamp
 */
void
timestamp_incr(timestampt *ts)
{
        /* log_assert(ts != NULL); */
        if(ts->tv_usec == 999999)
        {
#if 0
                if(ts->tv_sec == TS_ENDT.tv_sec)
                        return; /* clamp at TS_ENDT */
#endif
                ts->tv_usec = 0;
                ts->tv_sec++;
        }
        else
        {
                ts->tv_usec++;
        }
}


/*
 * decrement a timestamp
 */
void
timestamp_decr(timestampt *ts)
{
        /* log_assert(ts != NULL); */
        if(ts->tv_usec == 0)
        {
#if 0
                if(ts->tv_sec == TS_ZERO.tv_sec)
                        return; /* clamp at TS_ZERO */
#endif
                ts->tv_usec = 999999;
                ts->tv_sec--;
        }
        else
        {
                ts->tv_usec--;
        }
}


/*
 * Returns the non-negative difference between two timestamps; otherwise zero.
 *
 * N.B. Meaningful only if "afta" is later than "b4",
 * negative differences map to TS_ZERO
 */
timestampt
diff_timestamp(const timestampt *const afta, const timestampt *const b4)
{
        timestampt diff;
        diff = TS_ZERO;

        diff.tv_sec = afta->tv_sec -  b4->tv_sec;
        diff.tv_usec = afta->tv_usec -  b4->tv_usec;

        if(diff.tv_usec < 0)
        {
                if(diff.tv_sec > 0)
                {
                        /* borrow */
                        diff.tv_sec--;
                        diff.tv_usec += 1000000;
                }
                else
                {
                        /* truncate to zero */
                        diff.tv_sec = diff.tv_usec = 0;
                }
        }

        return diff;
}


/*
 * Returns the difference, in seconds, between two timestamps.
 *
 * Arguments:
 *      afta    Pointer to the later timestamp
 *      b4      Pointer to the earlier timestamp
 * Returns:
 *      The value (afta - b4) in seconds.  Will be negative if "afta" is before
 *      "b4".
 */
double
d_diff_timestamp(const timestampt *const afta,
         const timestampt *const b4)
{
    return (afta->tv_sec - b4->tv_sec) + .000001*(afta->tv_usec - b4->tv_usec);
}


#define STRFTIME_FORMAT "%Y%m%dT%H%M%S."
#define USEC_FORMAT     "%06ld"


/**
 * Formats a timestamp.
 *
 * @param timestamp     Pointer to the timestamp to be formatted.
 * @retval NULL         The timestamp couldn't be formatted.
 * @return              Pointer to a static buffer containing the formatted
 *                      timestamp.
 */
char*
tsFormat(
    const timestampt* const     timestamp)
{
    static char         string[80];
    const struct tm*    tm = gmtime(&timestamp->tv_sec);
    size_t              nbytes = strftime(string, sizeof(string),
            STRFTIME_FORMAT, tm);

    if (nbytes == 0)
        return NULL;
    (void)snprintf(string+nbytes, sizeof(string)-nbytes, USEC_FORMAT,
            (long)timestamp->tv_usec);
    string[sizeof(string)-1] = 0;

    return string;
}


/*
 * Parses a string representation of a timestamp.  The format of the timestamp
 * must be YYYYMMDDThhmmss[.uuuuuu].
 *
 * Arguments:
 *      timestamp       Pointer to the timestamp to be parsed.
 * Returns:
 *      -1              Error.  "log_error_q()" called.
 *      else            Number of bytes parsed.
 */
int
tsParse(
    const char* const   string,
    timestampt* const   timestamp)
{
    int                 nbytes = -1;    /* failure */
    int                 year;
    int                 month;
    int                 day;
    int                 hour;
    int                 minute;
    int                 second;
    long                microseconds = 0;
    const char*         format = "%04d%02d%02dT%02d%02d%02d.%06ld";
    int                 nfields = sscanf(string, format, &year, &month, &day,
                            &hour, &minute, &second, &microseconds);

    if (6 > nfields) {
        log_error_q("Couldn't decode timestamp \"%s\" with format \"%s\"", string,
            format);
    }
    else if (month < 1 || month > 12 ||
            day < 1 || day > 31 ||
            hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 ||
            second < 0 || second > 61 ||
            microseconds < 0) {
        log_error_q("Invalid timestamp \"%s\"", string);
    }
    else {
        struct tm           tm;

        tzset();

        tm.tm_isdst = 0;
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hour;
        tm.tm_min = minute;
        tm.tm_sec = second - (int)timezone;
        timestamp->tv_sec = mktime(&tm);
        timestamp->tv_usec = microseconds;
        nbytes = 6 == nfields ? 15 : 22;
    }

    return nbytes;
}

static void timeval_normalize(
        struct timeval* const timeval)
{
    #define ONE_MILLION 1000000
    if (timeval->tv_usec >= ONE_MILLION || timeval->tv_usec <= -ONE_MILLION) {
        timeval->tv_sec += timeval->tv_usec / ONE_MILLION;
        timeval->tv_usec %= ONE_MILLION;
    }
    if (timeval->tv_sec > 0 && timeval->tv_usec < 0) {
        timeval->tv_sec--;
        timeval->tv_usec += ONE_MILLION;
    }
    else if (timeval->tv_sec < 0 && timeval->tv_usec > 0) {
        timeval->tv_sec++;
        timeval->tv_usec -= ONE_MILLION;
    }
}

/**
 * Returns the number of seconds that correspond to a time-value as a double.
 *
 * @param[in] timeval  Time-value
 * @return             Number of seconds corresponding to `timeval`
 */
double timeval_as_seconds(
        struct timeval* timeval)
{
    return timeval->tv_sec + timeval->tv_usec/1000000.0;
}

/**
 * Initializes a time-value from the difference between two time-values.
 *
 * @param[out] duration  Duration equal to `after - before`
 * @param[in]  after     Later time-value
 * @param[in]  before    Earlier time-value
 * @return               `duration`
 */
struct timeval* timeval_init_from_difference(
        struct timeval* const restrict       duration,
        const struct timeval* const restrict later,
        const struct timeval* const restrict earlier)
{
    duration->tv_sec = later->tv_sec - earlier->tv_sec;
    duration->tv_usec = later->tv_usec - earlier->tv_usec;
    timeval_normalize(duration);
    return duration;
}

/**
 * Formats a time-value as "<YYYY>-<MM>-<DD>T<hh>:<mm>:<ss>.<uuuuuu>Z".
 *
 * @param[out] buf      Buffer to hold formatted string. It is the caller's
 *                      responsibility to ensure that the buffer can contain
 *                      at least `TIMEVAL_FORMAT_TIME` bytes.
 * @param[in]  timeval  Time-value to be formatted
 * @retval     NULL     `timeval` couldn't be formatted
 * @return              `buf`
 */
char* timeval_format_time(
        char* const restrict                 buf,
        const struct timeval* const restrict timeval)
{
    char* string = NULL;
    const struct tm*    tm = gmtime(&timeval->tv_sec);
    size_t              size = TIMEVAL_FORMAT_TIME;
    size_t              nbytes = strftime(buf, size, "%Y-%m-%dT%H:%M:%S.", tm);
    if (nbytes != 0) {
        if (snprintf(buf+nbytes, size-nbytes, "%06ldZ", (long)timeval->tv_usec)
                > 0) {
            buf[TIMEVAL_FORMAT_TIME-1] = 0;
            string = buf;
        }
    }
    return string;
}

/**
 * Formats a duration as "P[<days>D]T[<hours>H][<minutes>M]<seconds>.<uuuuuu>S",
 * where <days>, <hours>, <minutes>, and <seconds> have the minimum number of
 * necessary numerals.
 *
 * @param[out] buf       Buffer to hold formatted duration. It is the caller's
 *                       responsibility to ensure that it can contain at least
 *                       `TIMEVAL_FORMAT_DURATION` bytes.
 * @param[in]  duration  Duration to be formatted
 * @return     `buf`
 */
char* timeval_format_duration(
        char* restrict                       buf,
        const struct timeval* const restrict duration)
{
    int           value;
    int           nchar;
    int           tPrinted = 0;
    long          seconds = duration->tv_sec;
    char*         cp = buf;
    size_t        size = TIMEVAL_FORMAT_DURATION;

    *cp++ = 'P';
    size--;

    value = seconds / 86400;
    if (value != 0) {
        nchar = snprintf(cp, size, "%dD", value);
        cp += nchar;
        size -= nchar;
        seconds -= 86400 * value;
    }

    value = seconds / 3600;
    if (value != 0) {
        nchar = snprintf(cp, size, "T%dH", value);
        tPrinted = 1;
        cp += nchar;
        size -= nchar;
        seconds -= 3600 * value;
    }

    value = seconds / 60;
    if (value != 0) {
        if (!tPrinted) {
            (void)strncpy(cp, "T", size);
            cp++;
            size--;
            tPrinted = 1;
        }
        nchar = snprintf(cp, size, "%dM", value);
        cp += nchar;
        size -= nchar;
        seconds -= 60 * value;
    }

    if (!tPrinted) {
        (void)strncpy(cp, "T", size);
        cp++;
        size--;
    }
    nchar = snprintf(cp, size, "%ld", seconds);
    cp += nchar;
    size -= nchar;
    (void)snprintf(cp, size, ".%06ldS", (long)duration->tv_usec);

    buf[TIMEVAL_FORMAT_DURATION-1] = 0;

    return buf;
}

#if defined(_POSIX_TIMERS) && _POSIX_TIMERS > 0
#include <time.h>

/**
 * Initializes a time-value from a time-specification. The intialized value is
 * the closest one to the time-specification.
 *
 * @param[out] timeval   Time-value
 * @param[in]  timespec  Time-specification
 * @return               `timeval`
 */
struct timeval* timeval_init_from_timespec(
        struct timeval* const restrict        timeval,
        const struct timespec* const restrict timespec)
{
    timeval->tv_sec = timespec->tv_sec;
    timeval->tv_usec = (timespec->tv_nsec + 500) / 1000;
    timeval_normalize(timeval);
    return timeval;
}

#endif
