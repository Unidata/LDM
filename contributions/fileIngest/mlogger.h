/*
 * mlogger.h
 *
 *  Created on: Nov 17, 2013
 *      Author: brapp
 */

#ifndef MLOGGER_H_
#define MLOGGER_H_

/* Options for logger optionMask */
#define O_FLUSH_AFTER_EACH		(1 << 0)	/* Flush buffer after each write */
#define O_TIMED_FLUSH			(1 << 1)	/* Flush buffer periodically */
#define O_ARCHIVE			(1 << 2)	/* When log reaches maximum size, or date changes, archive the current log */
#define O_TIMESTAMP			(1 << 3)	/* Display current time stamp at the beginning of each message "YYYY-MM-DD HH:MM:SS" */
#define O_CONCURRENT			(1 << 4)	/* Future - must add mutex and shared memory */
#define O_KEEP_OPEN			(1 << 5)	/* Open log during initialization */
#define O_SHOW_LEVEL			(1 << 6)	/* Display the log level in the message */
#define O_SHOW_SEVERITY			(1 << 7)	/* Display the severity of the message */
#define O_ADD_NEWLINE			(1 << 8)	/* Terminate each line with an ASCII <lf> character */
#define O_LOG_INIT			(1 << 9)	/* Write to log whenever the logger is initialized or shut down */
#define NODE_IN_USE			(1 << 31)	/* Used internally to track which logger nodes are allocated */

/* Various size and format definitions */
#define LOG_MAX_PATH_LEN		256
#define LOG_BUFFER_DEFAULT_SIZE		1024
#define LOG_FORMAT_BUF_SIZE		512
#define	LOG_MAX_FILENAME_LEN		128
#define LOG_MAX_MSG_LEN			1024
#define LOG_DATE_LEN			24			/* YYYY-MM-DD HH:MM:SS.cccc */
#define LOG_DEFAULT_DATE_FORMAT		"%Y-%m-%d %H:%M:%S"
#define LOG_DEFAULT_MAX_LOG_SIZE	(4 * 1024 * 1024)
#define LOG_MAX_FILE_SIZE		(1024 * 1024 * 1024)
#define LOG_MAXHOSTNAMELEN		64
#define LOG_SIZE_ARCHIVE_DATE		5
#define LOG_SIZE_LONG_DATE		10
#define DEFAULT_DISK_BLOCK_SIZE		4096
#define LOG_DEFAULT_FLUSH_INTERVAL	2

/* Logging facilities */
#define F_MIN				1
#define	F_FILE				1
#define F_DB				2
#define F_SOCKET			3
#define F_PIPE				4
#define F_CONSOLE			5
#define F_PRINTER			6
#define F_MAX				6

/* Verbosity Levels */
#define V_MIN				0
#define V_ALWAYS			0
#define V_ERROR				1
#define V_INFO				2
#define V_DEBUG				3
#define V_TRACE				4
#define V_MAX				4

/* Severity Levels */
#define S_MIN				0
#define S_FATAL				0	/* Results in program termination */
#define S_ERROR				1	/* Serious correctable error */
#define S_WARNING			2	/* Minor correctable error */
#define S_STATUS			3	/* Informational or status message */
#define S_DEBUG				4	/* Debugging data showing pertinent data values */
#define S_TRACE				5	/* Traces functions */
#define S_MAX				5

// ------------------------------------------
//for later when individual facilities are defined
//
//
//typedef struct	logger_file {
//	long long	logSize;	/* Current size of log file - only works with single process per log */
//	long long	rollOverSize;	/* Maximum file size before rollover.  If 0, no rollover. */
//	FILE		*logFd;		/* File descriptor for the log file.  When null, file is closed. */
//	char		*logPath;	/* Absolute log file base path */
//	char		*logName;	/* Name of log file */
//	char		*fullLogName;	/* Full path plus name */
//	char		*archivePath;	/* Absolute path to archives, NULL if no archives kept */
//	time_t		lastLogTime;	/* Time of last log update */
//} LOGGER_FILE;
//

typedef struct	logger_struct {
	long		logSize;	/* Current size of log file - only works with single process per log */
	long		rollOverSize;	/* Maximum file size before rollover.  If 0, no rollover. */
	int		index;		/* Array index in loggerStore */
	int		bufferSize;	/* Size of output buffer */
	int		optionMask;	/* Most significant bit is the in_use flag */
	int		verbosity;	/* Logging level that will be written to this logger */
	int		facility;	/* Logging medium mask: database, socket, file, console, pipe, etc. */
	char		*name;		/* User-defined name for this log */
	FILE		*logFd;		/* File descriptor for the log file.  When null, file is closed. */
	char		*logPath;	/* Absolute log file base path */
	char		*logName;	/* Name of log file */
	char		*fullLogName;	/* Full path plus name */
	char		*archivePath;	/* Absolute path to archives, NULL if no archives kept */
	char		*buffer;	/* Output buffer */
	char		*formatBuf;	/* Buffer for format string */
	char		*timeFormat;	/* Format of date string - default is "%Y-%m-%d %H:%M:%S" */
	time_t		lastLogTime;	/* Time of last log update */
	time_t		lastLogDay;	/* Epoch / 86400 */
	int		needFlush;	/* Bytes have been written since last flush */
	int		flushInterval;	/* How often to flush to disk if O_TIMED_FLUSH is set */
	int		flushBufSize;	/* Maximum size of the flush buffer before forcing flush */
	int		flushBufBytes;	/* Number of bytes written since last flush */
	timer_t		timerID;	/* Flush timer ID */
	int		timerSignal;	/* Signal to use with O_TIMED_FLUSH */
	int		amArchiving;	/* Set when archiving to prevent infinite recursion */
} LOGGER;

typedef struct	logger_hdr_struct {
	int		count;		/* Number of logger structures allocated */
	LOGGER		**loggers;	/* Pointer to array of LOGGER pointers */
} LOGGER_HDR;


/* -----------------------------------------------------------------------------
 * Function Name
 *	logInitLogger
 *
 * Format
 *	LOGGER *logInitLogger (char *name, int facility, int options, int verbosity, char *logPath,
 *		char *logName, long rollOverSize, long bufferSize)
 *
 * Arguments
 *	name
 *	Text string containing printable log name
 *
 *	facility
 *	Type of log:
 *		F_FILE
 *		F_DB
 *		F_SOCKET
 *		F_PIPE
 *		F_CONSOLE
 *		F_PRINTER
 *	Currently, only F_FILE is supported.
 *
 *	options
 *	Bit-masked options to use for this logger:
 *		O_FLUSH_AFTER_EACH	- Flush after each message
 *		O_TIMED_FLUSH		- Flush periodically (flushInterval)
 *		O_ARCHIVE		- Create daily archives
 *		O_TIMESTAMP		- Include timestamp in each message (microseconds)
 *		O_CONCURRENT		- Reserved
 *		O_KEEP_OPEN		- Log file always kept open
 *		O_SHOW_LEVEL		- Write Visibility/Verbosity of each message
 *		O_SHOW_SEVERITY		- Write Severity of each message
 *		O_ADD_NEWLINE		- Put a \n at the end of each line
 *		O_LOG_INIT		- Include log event messages
 *
 *	verbosity
 *	Maximum level of messages that will be logged.  Verbosity/visibility levels from
 *	most restrictive to least:
 *		V_ALWAYS
 *		V_ERROR
 *		V_INFO
 *		V_DEBUG
 *		V_TRACE
 *
 *	logPath
 *	Path specification for the log; does not include file name.
 *
 *	logName
 *	Name of log file.  Concatenated to logPath.
 *
 *	rollOverSize
 *	Maximum log size before rolling over to a new log file.
 *
 *	bufferSize
 *	Size to reserve internally for individual log messages.
 *
 * Description
 *	Initializes a new logger with the provided parameters.  This function
 *	must be called before a logger can be used.
 *
 * Return Values
 *	NULL		- Error
 *	non-NULL	- Success
 *
 * -------------------------------------------------------------------------- */

LOGGER *logInitLogger (char *name, int facility, int options, int verbosity, char *logPath,
			char *logName, long rollOverSize, long bufferSize);

/* -----------------------------------------------------------------------------
 * Function Name
 *	logCloseLogger
 *
 * Format
 *	int logCloseLogger (LOGGER *pl)
 *
 * Arguments
 *	pl
 *	Pointer to logger
 *
 * Description
 *	Closes a logger and releases all of its resources.  Only use if this
 *	logger will no longer be used.
 *
 * Return Values
 *	0	- Success
 *	1	- Error
 *
 * -------------------------------------------------------------------------- */

int logCloseLogger (LOGGER *pl);

/* -----------------------------------------------------------------------------
 * Function Name
 *	logShutdown
 *
 * Format
 *	void logShutdown
 *
 * Arguments
 *	N/A
 *
 * Description
 *	Convenience function to shut down all open loggers.  Usually called
 *	from an exit handler prior to program termination.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

void logShutdown ();

/* -----------------------------------------------------------------------------
 * Function Name
 *	logSetLogLevel
 *
 * Format
 *	int logSetLogLevel (LOGGER *pl, int new_verb)
 *
 * Arguments
 *	pl
 *	Pointer to logger
 *
 *	new_verb
 *	New log verbosity level
 *
 * Description
 *	This function changes the verbosity level of an existing logger.
 *
 * Return Values
 *	0	- Success
 *	1	- Error
 *
 * -------------------------------------------------------------------------- */

int logSetLogLevel (LOGGER *pl, int new_verb);

/* -----------------------------------------------------------------------------
 * Function Name
 *	logMsg
 *
 * Format
 *	int logMsg (LOGGER *pl, int visibility, int severity, const char *format, ...)
 *
 * Arguments
 *	pl
 *	Pointer to initialized logger.
 *
 *	visibility
 *	Visibility level of this message.  The logger's verbosity level must
 *	be greater than or equal to the message visibility for the message to be
 *	written to the log.  Will be displayed in the log if the O_SHOW_LEVEL
 *	option is set.  Valid values are:
 *		V_ALWAYS
 *		V_ERROR
 *		V_INFO
 *		V_DEBUG
 *		V_TRACE
 *
 *	severity
 *	Message severity level.  Will be displayed in a log message if the
 *	O_SHOW_SEVERITY option is set.  Can be any of:
 *		S_FATAL
 *		S_ERROR
 *		S_WARNING
 *		S_STATUS
 *		S_DEBUG
 *		S_TRACE
 *
 *	format
 *	Message format string.  Uses same characters as printf.
 *
 *	...
 *	Additional parameters corresponding to the tokens within the format
 *	string.
 *
 * Description
 *	Function used to write messages to a log.  Uses the same format as
 *	printf.
 *
 * Return Values
 *	0	- Success
 *	1	- Error
 *
 * -------------------------------------------------------------------------- */

int logMsg (LOGGER *pl, int visibility, int severity, const char *format, ...);

#endif /* MLOGGER_H_ */
