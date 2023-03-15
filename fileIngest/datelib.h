/* ------------------------------------------------------------------------------
 *
 * File Name:
 *	datelib.h
 *
 * Description:
 *
 *
 * 
 * Author:
 * 	brapp		Jun 27, 2012
 *
 * Modification History:
 *	Modified by		Date
 *	Description
 *
------------------------------------------------------------------------------ */


#ifndef DATELIB_H_
#define DATELIB_H_

#define RELT_NOW		"NOW"
#define RELT_TODAY		"TODAY"
#define RELT_YESTERDAY		"YESTERDAY"
#define RELT_TOMORROW		"TOMORROW"
#define MAX_DATE_FORMAT_LEN	128
#define DEFAULT_OUT_FORMAT	"%Y-%m-%d %H:%M:%S"

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
		      char *in_date, char *out_date, int max_out_size);

#endif /* DATELIB_H_ */
