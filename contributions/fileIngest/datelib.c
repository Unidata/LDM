/* ------------------------------------------------------------------------------
 *
 * File Name:
 *	datelib.c
 *
 * Description:
 *	This file contains functions for manipulating dates and times.
 *
 * Functions defined:
 * 	format_date_string
 * 
 * Author:
 * 	Brian Rapp		Jun 19, 2012
 *
 * Modification History:
 *	Modified by		Date
 *	Description
 *
 ------------------------------------------------------------------------------ */

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE
#endif
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <libgen.h>

#include "datelib.h"

/* ------------------------------------------------------------------------------

Function Name
	formatDateString
Synopsis
	Creates custom-formatted date strings.

Prototype
	int formatDateString (char *in_format, char *out_format, long adjustment,
			      char *in_date, char *out_date)

Arguments

	Name:		in_format
	Type:		char *
	Access:		read only

	Character string containing the format specification for the input time.
	See the man page for strftime for details.  This is only used when an
	absolute input date is provided.

	Name:		out_format
	Type:		char *
	Access:		read only

	Character string containing the Format specification for the output time.
	See the man page for strftime for details.  This argument is required.

	Name:		adjustment
	Type:		long
	Access:		read only

	A long integer containing an adjustment (in seconds) to be applied to
	the input time.  Negative values adjust the time prior to the input
	time.  Positive values adjust the time after the input time.  A value of
	zero indicates no adjustment is to be made to the input time.

	Name:		in_date
	Type:		char *
	Access:		read only

	Character string containing the input time.  Can be an absolute time in
	any format supported by the strftime function, or one of the following
	relative times:

		NOW		Current date and time
		TODAY		Today at midnight
		YESTERDAY	Yesterday at midnight
		TOMORROW	Tomorrow at midnight

	Name:		out_date
	Type:		char *
	Access:		write only

	A character string that is large enough to hold the output date provided
	in the out_format argument.  The string must be allocated by the caller.

	Name:		max_out_size
	Type:		int
	Access:		read only

	An integer containing the maximum size for out_date.  This value must
	include the trailing nul character.

Description
	This function creates a formatted date string optionally adjusted by
	a specified amount. The output format defaults to "YYYY-MM-DD HH:MM:SS",
	but can be fully specified with the 'out_format' argument.  See the
	man page for strftime for more information on formatting.

	An input time string is required, which can either be an absolute time
	of the form "YYYY-MM-DD HH:MM:SS" or one of the relative time strings
	"NOW", "YESTERDAY", "TODAY", or "TOMORROW".

	Another optional parameter provides the
	ability to adjust the time string by the specified number of seconds.

Return Values
	The number of characters placed in out_date, not including the terminating
	nul terminator, provided the string, including the terminating nul byte,
	fits.  Otherwise, it returns 0, and the contents of the array is undefined.

------------------------------------------------------------------------------ */

int formatDateString (const char *in_format, const char *out_format, long adjustment,
		      char *in_date, char *out_date, int max_out_size) {

	struct tm       *tp;
	struct tm	ts;
	time_t          the_time;
	char		*res;
	size_t		stat;
	time_t	time_adjust     = 0;
	char		new_out_format[MAX_DATE_FORMAT_LEN+1];

	if ((out_format == NULL) || (strlen (out_format) == 0)) {
		strcpy (new_out_format, DEFAULT_OUT_FORMAT);
	} else {
		strncpy (new_out_format, out_format, MAX_DATE_FORMAT_LEN);
	}

	if (!strcmp (in_date, RELT_NOW)) {
		time (&the_time);
	} else {
		if (!strcmp (in_date, RELT_TODAY))	{
			time (&the_time);
			tp = localtime (&the_time);
			tp->tm_hour = tp->tm_min = tp->tm_sec = 0;
			the_time = mktime (tp);
		} else {
			if (!strcmp (in_date, RELT_YESTERDAY)) {
				time (&the_time);
				tp = localtime (&the_time);
				tp->tm_hour = tp->tm_min = tp->tm_sec = 0;
				the_time = mktime (tp);
				the_time -= 86400;
			} else {
				if (!strcmp (in_date, RELT_TOMORROW)) {
					time (&the_time);
					tp = localtime (&the_time);
					tp->tm_hour = tp->tm_min = tp->tm_sec = 0;
					the_time = mktime (tp);
					the_time += 86400;
				} else {
					memset (&ts, 0, sizeof (struct tm));
					res = strptime (in_date, in_format, &ts); /* Convert time string to broken-down time -- dst may be wrong */
					if ((res == 0) || (*res != 0)) {
						return 0;
					}

					/* Convert broken-down time back to calendar time so we have a proper value for tm_isdst */
					the_time = mktime (&ts);	/* Convert broken-down time to calendar time */
					localtime_r (&the_time, &ts);	/* Convert calendar time back to broken-down time -- dst will be set properly here */

					res = strptime (in_date, in_format, &ts);

					if ((ts.tm_year == 0) || (ts.tm_mday == 0)) {
						return 0;
					}
					the_time = mktime (&ts);
				}
			}
		}
	}

	the_time += time_adjust;
	memset (&ts, 0, sizeof (struct tm));
	tp = localtime_r (&the_time, &ts);	/* This will set tm_isdst properly for the given time */
	stat = strftime (out_date, max_out_size, new_out_format, &ts);

	return stat;

}
