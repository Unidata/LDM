/*
 * mystdlib.h
 *
 *  Created on: Mar 3, 2012
 *      Author: brapp
 */

#ifndef MYSTDLIB_H_
#define MYSTDLIB_H_

#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#define TRUE		(1)
#define FALSE		(0)
#define YES		(1)
#define NO		(0)
#define SUCCESS		(0)
#define ERROR		(0)

#define EVENT_SIZE	(sizeof (struct inotify_event))
#define EVENT_BUF_LEN	(1024 * (EVENT_SIZE + 16))
#define MAX_STR_LEN	1023

#ifdef __GNUC__
#define fatal(...)\
{\
    fprintf(stderr, "FATAL ERROR OCCURRED: ");\
    fprintf(stderr, __VA_ARGS__);\
    fprintf(stderr, ": %s\n", strerror(errno));\
    exit(-1);\
}

#else /* __GNUC__ */
void
fatal(const char * fmt, ...)
{
	va_list list;

	fprintf(stderr, "FATAL ERROR OCCURRED: ");
	va_start(list, fmt);
	vfprintf(stderr, fmt, list);
	va_end(list);
	fprintf(stderr, ": %s\n", strerror(errno));
	exit(-1);
}

#endif /* __GNUC__ */

#define isDirectory(fname)	(getFileType (fname) == S_IFDIR)
#define isRegularFile(fname)	(getFileType (fname) == S_IFREG)
#define isPipe(fname)		(getFileType (fname) == S_IFIFO)
#define isSymLink(fname)	(getFileType (fname) == S_IFLNK)
#define isSocket(fname)		(getFileType (fname) == S_IFSOCK)
#define isCharDev(fname)	(getFileType (fname) == S_IFCHR)
#define isBlockDev(fname)	(getFileType (fname) == S_IFBLK)

/* -----------------------------------------------------------------------------
 * Function Name
 * 	fileExists
 *
 * Format
 * 	int fileExists (const char *fname)
 *
 * Arguments
 * 	fname
 * 	A valid file specification (no wildcards). Read, write, or execute
 * 	permission of the named file is not required, but you must be able to
 * 	reach all directories listed in the file specification leading to the
 * 	file.
 *
 * Description
 * 	This function checks for the existence of the specified file, which can
 * 	be an absolute or relative reference.  If the user does not have access
 * 	rights to the file, the result is as though the file does not exist.
 *
 * Return Values
 * 	0	Specified file does not exist
 * 	1	Specified file does exist
 *
 * -------------------------------------------------------------------------- */

int fileExists (const char *fname);

/* ------------------------------------------------------------------------------

Function Name:
	getFileType

Prototype:
	int getFileType (const char *fname)

Arguments:

	fname
	Type:		char *
	Access:		read only

	nul-terminated string containing a file path.

Description:


Return Values:
	0		Unknown file type (file doesn't exist or isn't accessible)
	A value from stat.h:
	S_IFDIR		directory
	S_IFCHR		character device
	S_IFBLK		block device
	S_IFREG		regular file
	S_IFIFO		pipe
	S_IFLNK		symbolic link
	S_IFSOCK	socket

------------------------------------------------------------------------------ */

int getFileType (const char *fname);

/* ------------------------------------------------------------------------------

Function Name:
	getFileSize

Prototype:
	off_t getFileSize (const char *fname)

Arguments:

	fname
	Type:		char *
	Access:		read only

	nul-terminated string containing a file path.

Description:
	Returns size of named file in bytes or -1 on error.

Return Values:
	Size of file in bytes.

------------------------------------------------------------------------------ */

off_t getFileSize (const char *fname);

/* ------------------------------------------------------------------------------

Function Name:
	getFileLastAccess

Prototype:
	time_t getFileLastAccess (const char *fname)

Arguments:

	fname
	Type:		char *
	Access:		read only

	nul-terminated string containing a file path.

Description:


Return Values:
	Time of last file access as time_t value.

------------------------------------------------------------------------------ */

time_t getFileLastAccess (const char *fname);

/* ------------------------------------------------------------------------------

Function Name:
	getFileLastMod

Prototype:
	time_t getFileLastMod (const char *fname)

Arguments:

	fname
	Type:		char *
	Access:		read only

	nul-terminated string containing a file path.

Description:


Return Values:
	Time of last file modification as time_t value.

------------------------------------------------------------------------------ */

time_t getFileLastMod (const char *fname);

/* ------------------------------------------------------------------------------

Function Name:
	getFileLastStatus

Prototype:
	time_t getFileLastStatus (const char *fname)

Arguments:

	fname
	Type:		char *
	Access:		read only

	nul-terminated string containing a file path.

Description:


Return Values:
	Time of last file status change as time_t value.

------------------------------------------------------------------------------ */

time_t getFileLastStatus (const char *fname);

/* -----------------------------------------------------------------------------
 * Function Name
 *	splitFilenameExt
 *
 * Format
 *	int splitFilenameExt (const char *fname, char **ext)
 *
 * Arguments
 * 	fname
 *
 *
 * 	ext
 *
 * Description
 *	Separate filename from extension, where the extension is defined as the
 *	substring following the last '.'  This function will modify the input
 *	string, so use a copy if you need to save the original.  In fname, the
 *	final '.' will be replaced with a NUL character.  The ext argument will
 *	point to the first character of the extension on return.  If there is
 *	no extension, ext will be NULL.
 *
 *	This function does not perform any validation on the filename whatsoever.
 *
 * Return Values
 * 	0		success
 *	non-zero	failure
 *
 * -------------------------------------------------------------------------- */

int splitFilenameExt (const char *fname, char **ext);

/* -----------------------------------------------------------------------------
 * Function Name
 *	raiseCase
 *
 * Format
 *	char *raiseCase (char *string)
 *
 * Arguments
 *	string
 *	A nul-terminated string containing the text to be made upper case.
 *	This is a read/write argument with the result be written in place.
 *
 * Description
 *	This function takes a nul-terminated string and makes it all upper case.
 *
 * Return Values
 *	Pointer to string.
 *
 * -------------------------------------------------------------------------- */

char *raiseCase (char *string);

/* -----------------------------------------------------------------------------
 * Function Name
 *	lowerCcase
 *
 * Format
 *	char *lowerCase (char *string)
 *
 * Arguments
 *	string
 *	A nul-terminated string containing the text to be made lower case.
 *	This is a read/write argument with the result be written in place.
 *
 * Description
 *	This function takes a nul-terminated string and makes it all lower case.
 *
 * Return Values
 *	Pointer to string.
 *
 * -------------------------------------------------------------------------- */

char *lowerCase (char *string);

/* -----------------------------------------------------------------------------
 * Function Name
 *	isNumber
 *
 * Format
 *	int isNumber (char *string)
 *
 * Arguments
 *	string
 *	A nul-terminated string.
 *
 * Description
 *	This function examines every character in 'string' to see if it	is a
 *	numeric character as defined by the 'isdigit' macro.
 *
 * Return Values
 * 	TRUE	if all characters in 'string' are numeric.
 * 	FALSE	if any of the characters in 'string' are not numeric, or if
 * 		'string' is a NULL pointer or has zero length.
 *
 * -------------------------------------------------------------------------- */

int isNumber (char *string);

/* -----------------------------------------------------------------------------
 * Function Name
 *	moveFile
 *
 * Format
 *	int moveFile (char *inpath, char *outpath)
 *
 * Arguments
 *	inpath
 *	A nul-terminated string containing an absolute or relative path to a file
 *	or directory to be moved or renamed.
 *
 *	outpath
 *	A nul-terminated string containing an absolute or relative path serving
 *	as the destination for the 'inpath'.
 *
 * Description
 *	This function executes the Unix command "mv <inpath> <outpath>", with
 *	<inpath> and <outpath> being the two arguments to this function.
 *
 * Return Values
 * 	0		success
 *
 *	ENOENT		if 'inpath' or 'outpath' are NULL pointers or are zero-
 *			length, or if inpath does not exist.
 *
 *	EEXIST		destination already exists
 *
 *	Other values as returned by the 'mv' command.
 *
 * -------------------------------------------------------------------------- */

int moveFile (char *inpath, char *outpath, int overwrite);

/* -----------------------------------------------------------------------------
 * Function Name
 *	copyFile
 *
 * Format
 *	int copyFile (const char* source, const char* destination)
 *
 * Arguments
 *	path
 *	A nul-terminated string containing the new desired working directory.
 *
 *	create
 *	An integer value used to indicate the action to take if 'path' does not
 *	exist. *
 *
 * Description
 *	This function is used to create a copy of an existing file. While the
 *	file is being copied, the copy is set to write-only by the owner to
 *	prevent incomplete reading by a process that may be polling the directory
 *	for input.
 *
 * Return Values
 *	0		success
 *	-1		an error occurred.  'errno' will be set appropriately.
 *
 * -------------------------------------------------------------------------- */

int copyFile (const char* source, const char* destination);

/* -----------------------------------------------------------------------------
 * Function Name
 *	changeDirectory
 *
 * Format
 *	int changeDirectory (char *path, int create)
 *
 * Arguments
 *	path
 *	A nul-terminated string containing the new desired working directory.
 *
 *	create
 *	An integer value used to indicate the action to take if 'path' does not
 *	exist. *
 *
 * Description
 *	This function is used to change the current working directory.  If the
 *	specified directory does not exist and 'create' is non-zero, then the
 *	specified directory will be created.
 *
 * Return Values
 *	0		success
 *	-1		an error occurred.  'errno' will be set appropriately.
 *
 * -------------------------------------------------------------------------- */

int changeDirectory (char *path, int create);

/* -----------------------------------------------------------------------------
 * Function Name
 *	dirtok_r
 *
 * Format
 *	char *dirtok_r (char *path, char **remdir)
 *
 * Arguments
 *	path
 *	On the first call, contains a nul-terminated string containing an
 *	absolute or relative path.  On subsequent calls, must be NULL.
 *
 *	remdir
 *	Pointer to string that will contain the remaining string.
 *
 * Description
 *	Path
 *
 * Return Values
 *	pointer to next directory string
 *	NULL on error or when no more subdirectories remaining.
 *
 * -------------------------------------------------------------------------- */

char *dirtok_r (char *path, char **rpath);

/* -----------------------------------------------------------------------------
 * Function Name
 *	makeDirectory
 *
 * Format
 *	int makeDirectory (char *path, int make_parent, unsigned in perms)
 *
 * Arguments
 *	path
 *	A nul-terminated string containing the new desired directory.
 *
 * Description
 *	This function is used to create a new directory.  If the specified
 *	already exists, then success is returned as if the directory was
 *	successfully created.
 *
 * Return Values
 *	0		success
 *	-1		an error occurred.  'errno' will be set appropriately.
 *
 * -------------------------------------------------------------------------- */

int makeDirectory (char *path, int make_parent, unsigned int perms);

/* -----------------------------------------------------------------------------
 * Function Name
 *	freeVector
 *
 * Format
 *	void freeVector (char **vector)
 *
 * Arguments
 *
 * Description
 *	A vector is an array of pointers of indeterminate size.  The end of the
 *	array is marked by the presence of a NULL pointer.
 *
 *	This function will free all memory in a dynamically allocated vector.
 *
 * Return Values
 *	This function does not return any values.
 *
 * -------------------------------------------------------------------------- */

void freeVector (char **vector);

/* -----------------------------------------------------------------------------
 * Function Name
 *	printStringVector
 *
 * Format
 *	void printStringVector (char **vector)
 *
 * Arguments
 *
 * Description
 *	A vector is an array of pointers of indeterminate size.  The end of the
 *	array is marked by the presence of a NULL pointer.
 *
 *	This function assumes the input vector contains strings.  It will
 *	output each element string to stdout and halt once the end is reached.
 *
 * Return Values
 *	This function does not return any values.
 *
 * -------------------------------------------------------------------------- */

void printVector (char **vector);

/* -----------------------------------------------------------------------------
 * Function Name
 *	getFileAge
 *
 * Format
 *	long getFileAge (char *filepath)
 *
 * Arguments
 *	filepath
 *	A nul-terminated string containing an absolute or relative file path.
 *
 * Description
 *	This function finds the age of a file in minutes.  It uses the 'stat'
 *	function to get file information and uses the mtime field to determine
 *	its last modification time, which is interpreted (for better or worse)
 *	as its creation time.
 *
 * Return Values
 *	-1		if an error occurs.
 *	non-negative	the age of the specified file in minutes.
 *
 * -------------------------------------------------------------------------- */

long getFileAge (char *filepath);

/* ------------------------------------------------------------------------------

Function Name:
	getBlockSize

Prototype:
	long getBlockSize (char *filename)

Arguments:

	filename
	Type:		char *
	Access:		read only

	nul-terminated string containing a file name

Description:

Return Values:

------------------------------------------------------------------------------ */

long getBlockSize (char *filename);

/* ------------------------------------------------------------------------------

Function Name:
	stripTrailingChar

Prototype:
	void stripTrailingChar (char *string, char theChar)

Arguments:

	string
	Type:		char *
	Access:		read/write

	nul-terminated string, possibly containing unwanted characters at the end.

	theChar
	Type:		char
	Access:		read only

	char to strip from the end of the 'string'.

Description:

Return Values:
	N/A

------------------------------------------------------------------------------ */

char *stripTrailingChar (char *string, char theChar);

/* -----------------------------------------------------------------------------
 * Function Name
 *	reopenStdFile
 *
 * Format
 *	int reopenStdFile (int fd, char *path)
 *
 * Arguments
 *	fd
 *	File descriptor to be reopened.
 *
 *	path
 *	Path of file to be reopened.
 *
 * Description
 *	This function is designed to be used to detach stdin, stdout, or
 *	stderr from their associated terminal and reallocate them to
 *	files.  For instance, to make stdout write to the file "myout.txt",
 *	use:
 *		reopenStdFile (1, "myout.txt");
 *
 * Return Values
 *	0	Success
 *	-1	opened file descriptor is not the same as the closed one.
 *	On open or close error, the value of errno is returned.
 *
 * -------------------------------------------------------------------------- */

 int reopenStdFile (int fd, char *path);

/* -----------------------------------------------------------------------------
 * Function Name
 *	removeExtension
 *
 * Format
 *	void removeExtension (char *fname)
 *
 * Arguments
 *	fname
 *	String containing file name
 *
 * Description
 * 	Removes the file extension from the passed file name string.
 *
 * Return Values
 * 	N/A
 *
 * -------------------------------------------------------------------------- */

void removeExtension (char *fname);

#endif /* MYSTDLIB_H_ */
