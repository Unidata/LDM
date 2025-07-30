/* -----------------------------------------------------------------------------
 *
 * File Name:
 *	mlogger.c
 *
 * Description:
 *	This file defines the mlogger logging system.  The 'm' is for "Multi"
 *	because it is designed to support multiple output types.  Currently, only
 *	file types are supported, but it has the beginnings of support for also
 *	logging to pipes, sockets, terminals, printers, and databases using the
 *	same common interface. To add the additional support types, XML files
 *	support will be added to simplify configuration.
 *
 * Public functions defined:
 *	 logOpen
 *	 logClose
 *	 logInitLogger
 *	 logCloseLogger
 *	 logShutdown
 *	 logSetLogLevel
 *	 logMsg
 *
 * Private functions defined:
 *	logOpenFile
 *	logOpenDB
 *	logOpenSocket
 *	logOpenPipe
 *	logOpenConsole
 *	logOpenPrinter
 *	logCloseFile
 *	logCloseDB
 *	logCloseSocket
 *	logClosePipe
 *	logCloseConsole
 *	logClosePrinter
 *	archiveLog
 *	checkLogRollover
 *
 * Author:
 * 	Brian M Rapp		17-Nov-2013
 *
 * Modification History:
 *	Modified by		Date
 *	Description
 *
 * --------------------------------------------------------------------------- */

#define _GNU_SOURCE
#define _XOPEN_SOURCE

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>

#include "../fileIngest/stdclib.h"
#include "../fileIngest/mlogger.h"

#define DIRECTORY_CREATE_PERMS		(S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH)
#define	SECONDS_PER_DAY			(24 * 60 * 60)

static LOGGER_HDR	loggerStore;
static char		*logSeverityStrings[6] = { "FATAL", "ERROR", "WARNING", "STATUS", "DEBUG", "TRACE" };
static char		*logVerbosityStrings[5] = { "ALWAYS", "ERROR", "INFO", "DEBUG", "TRACE" };

/* -----------------------------------------------------------------------------
 * Function Name
 *	logOpenFile
 *
 * Format
 *	static int logOpenFile (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logOpen to open a
 *	previously-defined file for appending.  The location must be
 *	accessible by the user context this is executed by.
 *
 * Return Values
 * 	0	Success
 * 	1	Error
 *
 * -------------------------------------------------------------------------- */

static int logOpenFile (LOGGER *p_logger) {

	if (p_logger == NULL) {
		fprintf (stderr, "ERROR: %s - NULL logger provided\n", __FUNCTION__);
		return ERROR;
	}

	if (!(p_logger->optionMask & NODE_IN_USE)) {
		fprintf (stderr, "ERROR: %s - Logger not in use\n", __FUNCTION__);
		return ERROR;
	}

	if ((p_logger->fullLogName == NULL) || (p_logger->fullLogName[0] == '\0')) {
		fprintf (stderr, "ERROR: %s - NULL log name\n", __FUNCTION__);
		return ERROR;
	}

	/* If it's not already open, open it */
	if (p_logger->logFd == NULL) {
		if ((p_logger->logFd = fopen (p_logger->fullLogName, "a")) == NULL) {
			fprintf (stderr, "ERROR: %s - Could not open log file %s\n", __FUNCTION__, p_logger->fullLogName);
			return ERROR;
		}
	}

	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logOpenDB
 *
 * Format
 *	static int logOpenDB (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logOpen to open a
 *	previously-defined database log for appending.  The location must be
 *	accessible by the user context this is executed by.
 *
 * Return Values
 * 	0	Success
 * 	1	Error
 *
 * -------------------------------------------------------------------------- */

static int logOpenDB (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logOpenSocket
 *
 * Format
 *	static int logOpenSocket (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logOpen to open a
 *	previously-defined socket for writing.
 *
 * Return Values
 * 	0	Success
 * 	1	Error
 *
 * -------------------------------------------------------------------------- */

static int logOpenSocket (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logOpenPipe
 *
 * Format
 *	static int logOpenPipe (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logOpen to open a
 *	previously-defined named pipe for appending.  The location must be
 *	accessible by the user context this is executed by and there must
 *	be a pipe-reading process.
 *
 * Return Values
 * 	0	Success
 * 	1	Error
 *
 * -------------------------------------------------------------------------- */

static int logOpenPipe (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logOpenConsole
 *
 * Format
 *	static int logOpenConsole (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logOpen to open a
 *	previously-defined console device for appending.  The device must be
 *	accessible by the user context this is executed by.
 *
 * Return Values
 * 	0	Success
 * 	1	Error
 *
 * -------------------------------------------------------------------------- */

static int logOpenConsole (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logOpenPrinter
 *
 * Format
 *	static int logOpenPrinter (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logOpen to open a
 *	previously-defined printer for appending.  The device must be
 *	accessible by the user context this is executed by.
 *
 * Return Values
 * 	0	Success
 * 	1	Error
 *
 * -------------------------------------------------------------------------- */

static int logOpenPrinter (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 * 	logOpen
 *
 * Format
 *	static int logOpen (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	Open all devices for this logger for output.
 *
 * Return Values
 *	0 - Success
 *	1 - Error
 *
 * -------------------------------------------------------------------------- */

static int logOpen (LOGGER *p_logger) {

	int	retval	= SUCCESS;

	if (p_logger == NULL) {
		fprintf (stderr, "ERROR: %s - NULL logger provided\n", __FUNCTION__);
		return ERROR;
	}

	if (!(p_logger->optionMask & NODE_IN_USE)) {
		fprintf (stderr, "ERROR: %s - Logger not in use\n", __FUNCTION__);
		return ERROR;
	}

	switch (p_logger->facility) {
		case F_FILE:
			retval = logOpenFile (p_logger);
			break;

		case F_DB:
			retval = logOpenDB (p_logger);
			break;

		case F_SOCKET:
			retval = logOpenSocket (p_logger);
			break;

		case F_PIPE:
			retval = logOpenPipe (p_logger);
			break;

		case F_CONSOLE:
			retval = logOpenConsole (p_logger);
			break;

		case F_PRINTER:
			retval = logOpenPrinter (p_logger);
			break;

		default:	/* Shouldn't ever get here... */
			break;
	}

	return retval;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logCloseFile
 *
 * Format
 *	static int logCloseFile (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logClose to close a
 *	file defined in the specified logger.  If the file is not open,
 *	it will simply return SUCCESS.
 *
 * Return Values
 * 	0	- Success
 * 	1	- Error
 *
 * -------------------------------------------------------------------------- */

static int logCloseFile (LOGGER *p_logger) {

	if (p_logger == NULL) {
		fprintf (stderr, "ERROR: %s - NULL logger provided\n", __FUNCTION__);
		return ERROR;
	}

	if (!(p_logger->optionMask & NODE_IN_USE)) {
		fprintf (stderr, "ERROR: %s - Logger not in use\n", __FUNCTION__);
		return ERROR;
	}

	if (p_logger->logFd == NULL) {
		return SUCCESS;
	}

	if (fclose (p_logger->logFd)) {
		fprintf (stderr, "ERROR: %s - %d returned by fclose\n", __FUNCTION__, errno);
		return ERROR;
	}

	p_logger->logFd = NULL;

	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logCloseDB
 *
 * Format
 *	static int logCloseDB (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logClose to close a
 *	database connection defined in the specified logger.  If the connection
 *	is not open, it will simply return SUCCESS.
 *
 * Return Values
 *
 * -------------------------------------------------------------------------- */

static int logCloseDB (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logCloseSocket
 *
 * Format
 *	static int logCloseSocket (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logClose to close a
 *	socket defined in the specified logger.  If the socket is not open,
 *	it will simply return SUCCESS.
 *
 * Return Values
 * 	0	- Success
 * 	1	- Error
 *
 * -------------------------------------------------------------------------- */

static int logCloseSocket (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logClosePipe
 *
 * Format
 *	static int logClosePipe (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logClose to close a
 *	named pipe defined in the specified logger.  If the pipe is not open,
 *	it will simply return SUCCESS.
 *
 * Return Values
 * 	0	- Success
 * 	1	- Error
 *
 * -------------------------------------------------------------------------- */

static int logClosePipe (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logCloseConsole
 *
 * Format
 *	static int logCloseConsole (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logClose to close a
 *	console device defined in the specified logger.  If the device is
 *	not open, it will simply return SUCCESS.
 *
 * Return Values
 * 	0	- Success
 * 	1	- Error
 *
 * -------------------------------------------------------------------------- */

static int logCloseConsole (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	logClosePrinter
 *
 * Format
 *	static int logClosePrinter (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	This static function is called internally by logClose to close a
 *	printer device defined in the specified logger.  If the printer
 *	device is not open, it will simply return SUCCESS.
 *
 * Return Values
 * 	0	- Success
 * 	1	- Error
 *
 * -------------------------------------------------------------------------- */

static int logClosePrinter (LOGGER *p_logger) {

	fprintf (stderr, "INFO: %s - function not yet implemented\n", __FUNCTION__);
	return SUCCESS;
}

/* -----------------------------------------------------------------------------
 * Function Name
 * 	logClose
 *
 * Format
 *	static int logClose (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	Close all devices for this logger for output.
 *
 * Return Values
 *	0 - Success
 *	1 - Error
 *
 * -------------------------------------------------------------------------- */

static int logClose (LOGGER *p_logger) {

	int	retval	= SUCCESS;

	if (p_logger == NULL) {
		fprintf (stderr, "ERROR: %s - NULL logger provided\n", __FUNCTION__);
		return ERROR;
	}

	if (!(p_logger->optionMask & NODE_IN_USE)) {
		fprintf (stderr, "ERROR: %s - Logger not in use\n", __FUNCTION__);
		return ERROR;
	}

	switch (p_logger->facility) {
		case F_FILE:
			retval = logCloseFile (p_logger);
			break;

		case F_DB:
			retval = logCloseDB (p_logger);
			break;

		case F_SOCKET:
			retval = logCloseSocket (p_logger);
			break;

		case F_PIPE:
			retval = logClosePipe (p_logger);
			break;

		case F_CONSOLE:
			retval = logCloseConsole (p_logger);
			break;

		case F_PRINTER:
			retval = logClosePrinter (p_logger);
			break;

		default:	/* Shouldn't ever get here... */
			break;
	}

	return retval;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	archiveLog
 *
 * Format
 *	static int archiveLog (LOGGER *p_logger, char *archiveDate)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 *	archiveDate
 *	Date string to be used as subdirectory for the archived file
 *
 * Description
 *	Move the current log file to the archive directory, creating it if it
 *	does already exist.
 *
 * Return Values
 *	0	- Success
 *	1	- Error
 *
 * -------------------------------------------------------------------------- */

static int archiveLog (LOGGER *p_logger, char *archiveDate) {

	int		retval	= SUCCESS;
	char		archivePath[LOG_MAX_PATH_LEN+1];
	char		*fname;
	char		*ext;
	char		timeStr[64];	/* .HH.MM.SS */
	char		newName[LOG_MAX_FILENAME_LEN+1] = {0};
	struct tm	*lt;

	if (p_logger == NULL) {
		fprintf (stderr, "ERROR: %s - NULL logger provided\n", __FUNCTION__);
		return ERROR;
	}

	if (!(p_logger->optionMask & NODE_IN_USE)) {
		fprintf (stderr, "ERROR: %s - Logger not in use\n", __FUNCTION__);
		return ERROR;
	}

	sprintf (archivePath, "%s/%s", p_logger->archivePath, archiveDate);
	if (!fileExists (archivePath)) {
		if (makeDirectory (archivePath, YES, DIRECTORY_CREATE_PERMS)) {
			fprintf (stderr, "ERROR: %s - could not create directory %s\n", __FUNCTION__, archivePath);
			retval = ERROR;
		}
	}

	fname = strdup (p_logger->logName);
	splitFilenameExt (fname, &ext);		/* Remove the extension, if there is one */

	lt = localtime (&p_logger->lastLogTime);
	sprintf (timeStr, "%02d.%02d.%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);

	snprintf (newName, sizeof(newName)-1, "%s/%s.%s", archivePath, fname, timeStr);
	free (fname);

	if (p_logger->optionMask & O_LOG_INIT) {
		p_logger->amArchiving = TRUE;
		logMsg (p_logger, V_ALWAYS, S_STATUS, "%s ARCHIVED TO %s", p_logger->name, newName);
		p_logger->amArchiving = FALSE;
	}

	logClose (p_logger);

	if (moveFile (p_logger->fullLogName, newName, NO)) {
		fprintf (stderr, "ERROR: %s - could not move %s to %s\n", __FUNCTION__, p_logger->fullLogName, archivePath);
		retval = ERROR;
	}

	return retval;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	checkLogRollover
 *
 * Format
 *	static void checkLogRollover (LOGGER *p_logger)
 *
 * Arguments
 *	p_logger
 *	Pointer to initialized logger
 *
 * Description
 *	If the date has changed since the last log message or if the current
 *	log file has exceeded the maximum allowable size, roll it over to a
 *	new file.  If the O_ARCHIVE option is set for this logger,
 *	then call archiveLog to write it to the archive directory.  Otherwise,
 *	move it to "<log>.old".
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void checkLogRollover (LOGGER *p_logger) {

	char		archiveDate[LOG_SIZE_ARCHIVE_DATE+1];
	time_t		now;

	if (p_logger == NULL) {
		fprintf (stderr, "ERROR: %s - NULL logger provided\n", __FUNCTION__);
		return;
	}

	if (!(p_logger->optionMask & NODE_IN_USE)) {
		fprintf (stderr, "ERROR: %s - Logger not in use\n", __FUNCTION__);
		return;
	}

	if ((p_logger->lastLogTime == 0) || (p_logger->logSize == 0))
		return;

	if ((p_logger->optionMask & O_ARCHIVE)) {
		now = time (NULL);
		/* Check if date changed since last run or log reached rollover size and archive it if so */
		if (((now / 86400) != p_logger->lastLogDay) ||
		   (p_logger->rollOverSize && (p_logger->logSize >= p_logger->rollOverSize))) {
			strftime (archiveDate, LOG_SIZE_ARCHIVE_DATE+1, "%b%d", localtime (&p_logger->lastLogTime));
			if (archiveLog(p_logger, archiveDate) == 0) {
				p_logger->lastLogDay = p_logger->lastLogTime = 0;
				p_logger->logSize = 0;
			}

			if (p_logger->optionMask & O_KEEP_OPEN) {
				logOpen (p_logger);
			}
		}
	} else {
		if (p_logger->rollOverSize && (p_logger->logSize >= p_logger->rollOverSize)) {
			char	*oldExt = ".old";
			char	*fname = strdup (p_logger->logName);
			char	*newName = malloc (strlen (p_logger->fullLogName) + strlen (oldExt) + 1);	/* Reserve characters for old extension */
			char	*ext;

			splitFilenameExt (fname, &ext);
			sprintf (newName, "%s/%s.%s", p_logger->logPath, fname, oldExt);

			if (p_logger->optionMask & O_LOG_INIT) {
				p_logger->amArchiving = TRUE;
				logMsg (p_logger, V_ALWAYS, S_STATUS, "%s TERMINATED %s FILE SIZE %d RENAMED TO %s",
					p_logger->name, p_logger->fullLogName, p_logger->logSize, newName);
				p_logger->amArchiving = FALSE;
			}

			logClose (p_logger);
			if (moveFile (p_logger->fullLogName, newName, TRUE)) {
				fprintf (stderr, "ERROR: %s - could not move %s to %s\n", __FUNCTION__, p_logger->fullLogName, newName);
			}
			p_logger->lastLogDay = p_logger->lastLogTime = 0;
			p_logger->logSize = 0;

			if (p_logger->optionMask & O_KEEP_OPEN) {
				logOpen (p_logger);
			}

			free (newName);
			free (fname);
		}
	}
}

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
			char *logName, long rollOverSize, long bufferSize) {

	LOGGER 		*pl;
	int		i;
	struct stat	sb;

	if ((logPath == NULL) || (logPath[0] == '\0')) {
		fprintf (stderr, "ERROR: %s - Invalid logPath passed\n", __FUNCTION__);
		return NULL;
	}

	if ((logName == NULL) || (logName[0] == '\0')) {
		fprintf (stderr, "ERROR: %s - Invalid logName passed\n", __FUNCTION__);
		return NULL;
	}

	if ((facility < F_MIN) || (facility > F_MAX)) {
		fprintf (stderr, "ERROR: %s - Unknown facility %d\n", __FUNCTION__, facility);
		return NULL;
	}

	if (facility != F_FILE) {
		fprintf (stderr, "ERROR: %s - The only facility current supported is F_FILE (%d)\n",
			__FUNCTION__, F_FILE);
		return NULL;
	}

	if ((options & O_FLUSH_AFTER_EACH) && (options & O_TIMED_FLUSH)) {
		fprintf (stderr, "ERROR: %s - O_FLUSH_AFTER_EACH and O_TIMED_FLUSH cannot both be set\n",
			__FUNCTION__);
		return NULL;
	}

	verbosity = (verbosity < V_MIN) ? V_MIN : verbosity;
	verbosity = (verbosity > V_MAX) ? V_MAX : verbosity;

	if (loggerStore.count == 0) {
		i = 0;
		if ((loggerStore.loggers = (LOGGER **) malloc (sizeof (LOGGER *))) == NULL) {
			fprintf (stderr, "ERROR: %s - Could not malloc space for loggerStore array\n", __FUNCTION__);
			return NULL;
		}

		if ((loggerStore.loggers[0] = (LOGGER *) malloc (sizeof (LOGGER))) == NULL) {
			fprintf (stderr, "ERROR: %s - Could not malloc space for logger node\n", __FUNCTION__);
			return NULL;
		}
	} else {	/* Try to find an allocated logger that's not being used */
		for (i = 0; i < loggerStore.count; i++) {
			if (!(loggerStore.loggers[i]->optionMask & NODE_IN_USE)) {
				break;
			}
		}

		if (i >= loggerStore.count) {
			if ((loggerStore.loggers = (LOGGER **) realloc (loggerStore.loggers, sizeof (LOGGER *) * (loggerStore.count+1))) == NULL) {
				fprintf (stderr, "ERROR: %s - Could not realloc loggerStore\n", __FUNCTION__);
				return NULL;
			}

			if ((loggerStore.loggers[i] = (LOGGER *) malloc (sizeof (LOGGER))) == NULL) {
				fprintf (stderr, "ERROR: %s - Could not malloc space for logger node\n", __FUNCTION__);
				return NULL;
			}
		}

	}

	pl = loggerStore.loggers[i];
	pl->index = i;

	loggerStore.count++;
	pl->name = strdup (name);
	options |= NODE_IN_USE;
	pl->optionMask		= options;		/* Set the LOG_IN_USE bit and the flags passed in */
	pl->verbosity		= verbosity;
	pl->facility		= facility;		/* Only supports LOG_FILE now...how to implement others? */
	pl->bufferSize		= bufferSize;
	pl->logFd		= NULL;
	pl->needFlush		= FALSE;
	pl->flushInterval	= LOG_DEFAULT_FLUSH_INTERVAL;
	pl->timerSignal		= SIGRTMIN;
	pl->flushBufSize	= 0;
	pl->amArchiving		= FALSE;

//	switch (p_logger->facility) {
//		case F_FILE:
//			retval = logInitFile (p_logger);
//			break;
//
//		case F_DB:
//			retval = logInitDB (p_logger);
//			break;
//
//		case F_SOCKET:
//			retval = logInitSocket (p_logger);
//			break;
//
//		case F_PIPE:
//			retval = logInitPipe (p_logger);
//			break;
//
//		case F_CONSOLE:
//			retval = logInitConsole (p_logger);
//			break;
//
//		case F_PRINTER:
//			retval = logInitPipe (p_logger);
//			break;
//
//		default:	/* Shouldn't ever get here... */
//			fprintf ("ERROR: %s - hit default case\n", __FUNCTION__);
//			break;
//	}

	/* Force rollover size to LOG_MAX_FILE_SIZE if user leaves it set to 0 */
	pl->rollOverSize = rollOverSize ? rollOverSize : LOG_MAX_FILE_SIZE;

	if ((pl->logPath = strdup (logPath)) == NULL) {
		fprintf (stderr, "ERROR: %s - Could not strdup logPath\n", __FUNCTION__);
		return NULL;
	}

	if ((pl->logName = strdup (logName)) == NULL) {
		fprintf (stderr, "ERROR: %s - Could not strdup logName\n", __FUNCTION__);
		return NULL;
	}

	if ((pl->fullLogName = malloc (strlen (logPath) + strlen (logName) + 2)) == NULL) {
		fprintf (stderr, "ERROR: %s - Could not malloc fullLogName\n", __FUNCTION__);
		return NULL;
	}

	sprintf (pl->fullLogName, "%s/%s", logPath, logName);

	/* This is kludged for now, allow it to be specified later */
	if ((pl->archivePath = malloc (strlen (logPath) + strlen ("/ARCHIVE/MmmDd") + 1)) == NULL) {
		fprintf (stderr, "ERROR: %s - Could not malloc archivePath\n", __FUNCTION__);
		return NULL;
	}

	sprintf (pl->archivePath, "%s/ARCHIVE", logPath);

	if ((pl->timeFormat = strdup (LOG_DEFAULT_DATE_FORMAT)) == NULL) {	/* Hand jam this for now. In future make it configurable */
		fprintf (stderr, "ERROR: %s - Could not strdup date format string\n", __FUNCTION__);
		return NULL;
	}

	if ((pl->buffer = malloc (bufferSize)) == NULL) {
		fprintf (stderr, "ERROR: %s - Could not malloc buffer\n", __FUNCTION__);
		return NULL;
	}

	if ((pl->formatBuf = malloc (LOG_FORMAT_BUF_SIZE)) == NULL) {
		fprintf (stderr, "ERROR: %s - Could not malloc format buffer\n", __FUNCTION__);
		return NULL;
	}

	pl->lastLogDay = pl->lastLogTime = 0;

	if (!fileExists (pl->logPath)) {
		if (makeDirectory (pl->logPath, YES, DIRECTORY_CREATE_PERMS) < 0) {
			fprintf (stderr, "ERROR: %s - Could not make directory for logPath\n", __FUNCTION__);
			return NULL;
		}
	}

	/* Cheat to get the device block size by checking on the block size of the logPath directory file */
	pl->flushBufSize = (stat (pl->logPath, &sb) == 0) ? sb.st_blksize : DEFAULT_DISK_BLOCK_SIZE;

	if (fileExists (pl->fullLogName)) {
		pl->lastLogTime = getFileLastMod (pl->fullLogName);
		pl->lastLogDay = pl->lastLogTime / SECONDS_PER_DAY;
		pl->logSize = getFileSize (pl->fullLogName);
		checkLogRollover (pl);		/* Rollover the log if necessary */
	} else {
		pl->lastLogDay = pl->lastLogTime = 0;
		pl->logSize = 0;
	}

	if (options & O_LOG_INIT) {
		logMsg (pl, V_INFO, S_STATUS, "%s INITIALIZED", pl->name);
		if (!(options & O_KEEP_OPEN)) {
			logClose (pl);
		}
	}

	if (options & O_KEEP_OPEN) {
		logOpen (pl);
	}

	return pl;
}

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

int logCloseLogger (LOGGER *pl) {

	if (pl == NULL) {
		fprintf (stderr, "ERROR: %s - NULL logger provided\n", __FUNCTION__);
		return ERROR;
	}

	if (!(pl->optionMask & NODE_IN_USE)) {
		fprintf (stderr, "ERROR: %s - Logger not in use\n", __FUNCTION__);
		return ERROR;
	}

	if (pl->optionMask & O_LOG_INIT) {
		logMsg (pl, V_INFO, S_STATUS, "%s HALTED", pl->name);
	}

	logClose (pl);

	free (pl->archivePath);
	free (pl->buffer);
	free (pl->fullLogName);
	free (pl->logName);
	free (pl->logPath);
	free (pl->timeFormat);
	free (pl->formatBuf);
	free (pl->name);

	pl->logSize		= 0;
	pl->lastLogTime		= 0;
	pl->lastLogDay		= 0;
	pl->rollOverSize	= 0;
	pl->optionMask		= 0;	/* This turns off the IN_USE flag */
	pl->verbosity		= 0;

	loggerStore.count--;
	/* If this is the last logger in the loggerStore array, free it */
	if (pl->index == loggerStore.count) {
		if (loggerStore.count > 0) {
			if ((loggerStore.loggers = (LOGGER **) realloc (loggerStore.loggers, sizeof (LOGGER *) * (loggerStore.count))) == NULL) {
				fprintf (stderr, "ERROR: %s - Could not realloc loggerStore\n", __FUNCTION__);
				return ERROR;
			}
		} else {
			free (loggerStore.loggers);
		}
	}

	free (pl);

	return SUCCESS;
}

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

void logShutdown () {
	int	i;
	int	count = loggerStore.count;	/* Must use a temporary variable since loggerStore.count gets decremented */

	for (i = 0; i < count; i++) {
		if (loggerStore.loggers[i]->optionMask & NODE_IN_USE) {
			logCloseLogger (loggerStore.loggers[i]);
		}
	}
}

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

int logSetLogLevel (LOGGER *pl, int new_verb) {

	int		old_verb;

	if (pl == NULL) {
		fprintf (stderr, "ERROR: %s - NULL logger provided\n", __FUNCTION__);
		return SUCCESS;
	}

	if (!(pl->optionMask & NODE_IN_USE)) {
		fprintf (stderr, "ERROR: %s - Logger not in use\n", __FUNCTION__);
		return SUCCESS;
	}

	old_verb = pl->verbosity;
	pl->verbosity = new_verb;

	return old_verb;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	flushHandler
 *
 * Format
 *	static void flushHandler (int sig, siginfo_t *si, void *uc)
 *
 * Arguments
 *	sig
 *	Signal number that triggered this handler.
 *
 *	si
 *	Pointer to siginfo structure
 *
 *	uc
 *	User context
 *
 * Description
 *	Signal handler for timed flushes (O_TIMED_FLUSH) option.
 *
 * Return Values
 *	0	- Success
 *	1	- Error
 *
 * -------------------------------------------------------------------------- */

static void flushHandler (int sig, siginfo_t *si, void *uc) {

	timer_t		*tp;
	int		i;

	tp = si->si_value.sival_ptr;

	/* Find the logger this alarm is for */
	for (i = 0; i < loggerStore.count; i++) {
		if ((loggerStore.loggers[i]->optionMask & NODE_IN_USE) &&
		    (loggerStore.loggers[i]->timerSignal == sig) &&
		    (loggerStore.loggers[i]->timerID == *tp)) {
			break;
		}
	}

	if (i < loggerStore.count) {
		if (loggerStore.loggers[i]->logFd != 0) {
			if (fflush (loggerStore.loggers[i]->logFd) != 0) {
				fprintf (stderr, "ERROR: %s - fflush returned error code %d\n", __FUNCTION__, errno);
			}
		}

		loggerStore.loggers[i]->flushBufBytes = 0;
		loggerStore.loggers[i]->needFlush = FALSE;

	} else {
		fprintf (stderr, "Unknown timer expired");
	}
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	setFlushTimer
 *
 * Format
 *	static int setFlushTimer (char *name, timer_t *tid, int sigNo, int expireSec)
 *
 * Arguments
 *	name
 *	String container name of logger.
 *
 *	tid
 *	Pointer to timer_t passed by reference.
 *
 *	sigNo
 *	Signal number to use for the timer
 *
 *	expireSec
 *	Duration of timer in seconds.
 *
 * Description
 *	Sets a timer that will call the flushHandler signal handler when it
 *	expires.  This is only used when the O_TIMED_FLUSH option is used.
 *
 * Return Values
 *	0	- Success
 *	1	- Error
 *
 * -------------------------------------------------------------------------- */

static int setFlushTimer (char *name, timer_t *tid, int sigNo, int expireSec) {

	struct sigevent se = {};
	struct itimerspec its;
	struct sigaction sa;

	/* Set up signal handler. */
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = flushHandler;
	sigemptyset (&sa.sa_mask);
	if (sigaction (sigNo, &sa, NULL) == -1) {
		fprintf (stderr, "%s Failed to setup signal handling for %s.\n", __FUNCTION__, name);
		return (-1);
	}

	/* Set and enable alarm */
	se.sigev_notify = SIGEV_SIGNAL;
	se.sigev_signo = sigNo;
	se.sigev_value.sival_ptr = tid;
	timer_create (CLOCK_REALTIME, &se, tid);

	its.it_interval.tv_sec = 0;
	its.it_interval.tv_nsec = 0;
	its.it_value.tv_sec = expireSec;
	its.it_value.tv_nsec = 0;
	timer_settime (*tid, 0, &its, NULL);

	return (0);
}

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

int logMsg (LOGGER *pl, int visibility, int severity, const char *format, ...) {

	va_list		args;
	sigset_t	mask;
	sigset_t	orig_mask;
	int		retval 		= SUCCESS;
	char		*p;

	if (pl == NULL) {
		fprintf (stderr, "ERROR: %s - NULL logger provided\n", __FUNCTION__);
		return ERROR;
	}

	if (!(pl->optionMask & NODE_IN_USE)) {
		fprintf (stderr, "ERROR: %s - Logger not in use\n", __FUNCTION__);
		return ERROR;
	}

	p = pl->formatBuf;
	visibility = (visibility < V_MIN) ? V_MIN : visibility;
	visibility = (visibility > V_MAX) ? V_MAX : visibility;
	severity = (severity < S_MIN) ? S_MIN : severity;
	severity = (severity > S_MAX) ? S_MAX : severity;

	if (visibility > pl->verbosity) {
		return SUCCESS;
	}

	if (pl->optionMask & O_TIMED_FLUSH) {
		/* Block the timer signal to prevent race condition when flushHandler runs */
		sigemptyset (&mask);
		sigaddset (&mask, pl->timerSignal);
		if (sigprocmask (SIG_BLOCK, &mask, &orig_mask) < 0) {
			fprintf (stderr,
			                "%s - Call to sigprocmask failed with eror %d - %s",
			                __FUNCTION__, errno, strerror (errno));
			retval = ERROR;
		}
	}

	if (!pl->amArchiving) {
		checkLogRollover (pl);
	}

	va_start(args, format);

	if ((pl->optionMask & O_TIMESTAMP)) {
		char			datebuf[LOG_DATE_LEN+1];		/* YYYY-MM-DD HH:MM:SS.cccc */
		struct timespec		hrt;

		if (clock_gettime (CLOCK_REALTIME, &hrt)) {
			logMsg (pl, V_ALWAYS, S_FATAL, "Call to clock_gettime failed - exiting");
			exit (errno);
		}

		strftime (datebuf, LOG_DATE_LEN+1, LOG_DEFAULT_DATE_FORMAT, localtime (&hrt.tv_sec));
		p = datebuf + strlen (datebuf);
		sprintf (p, ".%06ld", hrt.tv_nsec / 1000);	/* Report in microseconds */
		sprintf (pl->formatBuf, "%s ", datebuf);
		p = pl->formatBuf + strlen (pl->formatBuf);
	}

	if (pl->optionMask & O_SHOW_LEVEL) {
		sprintf (p, "<%s> ", logVerbosityStrings[visibility]);
		p = pl->formatBuf + strlen (pl->formatBuf);
	}

	if (pl->optionMask & O_SHOW_SEVERITY) {
		sprintf (p, "[%s]: ", logSeverityStrings[severity]);
		p = pl->formatBuf + strlen (pl->formatBuf);
	}

	sprintf (p, "%s%c", format, (pl->optionMask & O_ADD_NEWLINE) ? '\n' : '\0');

	vsnprintf (pl->buffer, pl->bufferSize, pl->formatBuf, args);

	if (logOpen (pl)) {
		fprintf (stderr, "ERROR: %s - logOpen failed\n", __FUNCTION__);
	} else {
		int	msgSize = strlen (pl->buffer);
		int	writeSize;

		if ((writeSize = fprintf (pl->logFd, pl->buffer, args)) < msgSize) {
			fprintf (stderr, "ERROR: %s - only wrote %d bytes of %d byte message\n", __FUNCTION__, writeSize, msgSize);
		}

		if (writeSize > 0) {
			pl->logSize += writeSize;
			pl->lastLogTime = time (NULL);
			pl->lastLogDay = pl->lastLogTime / SECONDS_PER_DAY;
		}

		if (pl->optionMask & O_FLUSH_AFTER_EACH) {
			if (fflush (pl->logFd) != 0) {
				fprintf (stderr, "ERROR: %s - fflush returned error code %d\n", __FUNCTION__, errno);
			}
		}

		if (pl->optionMask & O_TIMED_FLUSH) {
			pl->flushBufBytes += writeSize;
			if (pl->flushBufBytes >= pl->flushBufSize) {
				if (fflush (pl->logFd) != 0) {
					fprintf (stderr, "ERROR: %s - fflush returned error code %d\n", __FUNCTION__, errno);
				}

				// printf ("%s - Manually flushing %s flushBufSize: %d flushBufBytes: %d\n", __FUNCTION__, pl->name, pl->flushBufSize, pl->flushBufBytes);
				pl->flushBufBytes = 0;
				pl->needFlush = FALSE;
			} else {
				if (!pl->needFlush) {
					pl->needFlush = TRUE;

					/* Set flush timer */
					setFlushTimer (pl->name, &pl->timerID, pl->timerSignal, pl->flushInterval);
				}
			}

			if (sigprocmask (SIG_SETMASK, &orig_mask, NULL) < 0) {
				fprintf (stderr, "%s - Call to sigprocmask failed with error %d - %s",
					__FUNCTION__, errno, strerror (errno));
				retval = ERROR;
			}
		}
	}

	va_end (args);

	return retval;
}

