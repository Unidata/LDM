/*
 *   Copyright 1995, University Corporation for Atmospheric Research
 *   See ../COPYRIGHT file for copying and redistribution conditions.
 */
/* $Id: timestamp.h,v 1.12.18.1 2007/03/02 19:50:13 steve Exp $ */

#ifndef _TIMESTAMP_H
#define _TIMESTAMP_H

#include <rpc/xdr.h>
#include <stdbool.h>
#include <sys/time.h>

typedef struct timeval timestampt;

extern const struct timeval TS_NONE; /* an invalid time */
extern const struct timeval TS_ZERO; /* the beginning of time */
extern const struct timeval TS_ENDT; /* the end of time */

#ifndef TOFFSET_NONE
#define TOFFSET_NONE (-2147483647)
#endif

#define tvEqual(tvl, tvr) \
	((tvl).tv_sec == (tvr).tv_sec && (tvl).tv_usec == (tvr).tv_usec)

#define timerEqual(tvpl, tvpr) \
	((tvpl)->tv_sec == (tvpr)->tv_sec && (tvpl)->tv_usec == (tvpr)->tv_usec)

#define tvIsNone(tv) (tvEqual(tv, TS_NONE))

#define	tvCmp(tv, uv, cmp)	\
	((tv).tv_sec cmp (uv).tv_sec || (\
	 (tv).tv_sec == (uv).tv_sec && (tv).tv_usec cmp (uv).tv_usec))

#define TIMEVAL_FORMAT_TIME 28 // Includes terminating NUL
#define TIMEVAL_FORMAT_DURATION (25+20) // Fixed number + num_digits(long long)

#ifdef __cplusplus
extern "C" {
#endif

extern int
set_timestamp(timestampt *tsp);

extern void
swap_timestamp(timestampt *fr, timestampt *to);

extern bool
xdr_timestampt(XDR *, timestampt*);

extern timestampt
timestamp_add(const timestampt *const left, const timestampt *const rght);

/*
 * Increment a timestamp
 */
extern void
timestamp_incr(timestampt *ts);

/*
 * Decrement a timestamp
 */
extern void
timestamp_decr(timestampt *ts);

/*
 * take the difference between two timestamps
 *
 * N.B. Meaningful only if "afta" is later than "b4",
 * negative differences map to TS_ZERO
 */
extern timestampt
diff_timestamp(const timestampt *const afta, const timestampt *const b4);

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
extern double
d_diff_timestamp(const timestampt *const afta,
	 const timestampt *const b4);

/*
 * Formats a timestamp.
 *
 * Arguments:
 *	timestamp	Pointer to the timestamp to be formatted.
 * Returns:
 *	Pointer to a static buffer containing the formatted timestamp.
 */
char*
tsFormat(
    const timestampt* const	timestamp);

/*
 * Parses a timestamp.
 *
 * Arguments:
 *	timestamp	Pointer to the timestamp to be formatted.
 * Returns:
 *	-1		Error.  LOG_ADD_ERRNO() called.
 *	else		Number of bytes parsed.
 */
int
tsParse(
    const char* const	string,
    timestampt* const	timestamp);

/**
 * Returns the number of seconds that correspond to a time-value as a double.
 *
 * @param[in] timeval  Time-value
 * @return             Number of seconds corresponding to `timeval`
 */
double timeval_as_seconds(
        struct timeval* timeval);

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
        struct timeval* const        timeval,
        const struct timespec* const timespec);

#endif

struct timeval* timeval_init_from_difference(
        struct timeval* const       duration,
        const struct timeval* const after,
        const struct timeval* const before);

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
        char* const                 buf,
        const struct timeval* const timeval);

/**
 * Formats a duration as "P[<days>D]T[<hh>H][<mm>M]<ss>.<uuuuuu>S".
 *
 * @param[out] buf       Buffer to hold formatted duration. It is the caller's
 *                       responsibility to ensure that it can contain at least
 *                       `TIMEVAL_FORMAT_DURATION` bytes.
 * @param[in]  duration  Duration to be formatted
 * @return     `buf`
 */
char* timeval_format_duration(
        char*                       buf,
        const struct timeval* const duration);

#ifdef __cplusplus
}
#endif

#endif /*!_TIMESTAMP_H */
