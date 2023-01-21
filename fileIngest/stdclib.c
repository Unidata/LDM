/* ------------------------------------------------------------------------------
 *
 * File Name:
 *	mystdlib.c
 *
 * Description:
 *	This file contains general-purpose functions for performing common tasks.
 *
 * Functions defined:
 * 	fileExists - determines if the requested file exists.
 *
 * 	raiseCase - force all letters within a string to all upper case.
 *
 * 	lowerCase - force all letters within a string to all lower case.
 *
 * 	isNumber - determines if a given string contains all numeric characters.
 *
 * 	moveFile - move or rename a file.
 *
 *	copyFile - copies a file from one place to another.
 *
 * 	changeDirectory - change the current working directory.
 *
 * 	makeDirectory - create a new directory.
 *
 * 	freeVector - free all memory allocated by a vector.
 *
 * 	printVector - print all members in a string vector.
 *
 *	getFileAge - get the age of a file or directory in minutes.
 *
 *	getBlockSize - find block size for a specific file
 *
 *	stripTrailingChar - strip a specific trailing character from a string
 *
 *	getFileLastAccess - returns last access time of a file as time_t
 *
 *	getFileLastMod - returns last modification for a file as time_t
 *
 *	getFileLastStatus - returns time of last status change of a file (name, perms, #links, etc)
 *
 *	splitFilenameExt - Separate filename from extension
 *
 *	getFileType - returns the type of a file using values from stat.h
 *
 *	reopenStdFile - close a specified file descriptor and reopen as a different file
 *
 *	removeExtension - remove extension from file name
 *
 * Author:
 * 	Brian Rapp		Mar 3, 2012
 *
 * Modification History:
 *	Modified by		Date
 *	Description
 *
 ------------------------------------------------------------------------------ */
#define _GNU_SOURCE

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#if defined(__APPLE__) || defined(__FreeBSD__)
#	include <copyfile.h>
#else
#	include <sys/sendfile.h>
#endif

#include "stdclib.h"

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

int fileExists (const char *fname) {

	struct stat	fattr;
	char		*full_name;
	int		retval;

	if ((fname == NULL) || (*fname == '\0')) {
		fprintf (stderr, "ERROR: %s - Invalid file name passed\n", __FUNCTION__);
		return FALSE;
	}

	full_name = strdup (fname);
	stripTrailingChar(full_name, '/');

	retval = (stat (full_name, &fattr) == 0);
	free (full_name);
	return retval;
}

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

int getFileType (const char *fname) {

	char		*full_name;
	struct stat	statbuf;
	int		retval;

	if ((full_name = strdup (fname)) == NULL) {
		return FALSE;
	}

	stripTrailingChar (full_name, '/');
	if (stat (full_name, &statbuf) == -1) {
		retval = FALSE;
	} else {
		retval = statbuf.st_mode & S_IFMT;
	}

	free (full_name);
	return retval;

}

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

off_t getFileSize (const char *fname) {

	char		*full_name;
	struct stat	statbuf;
	off_t		retval;

	if ((full_name = strdup (fname)) == NULL) {
		return FALSE;
	}

	stripTrailingChar (full_name, '/');
	if (stat (full_name, &statbuf) == -1) {
		retval = -1;
	} else {
		retval = statbuf.st_size;
	}

	free (full_name);
	return retval;

}

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

time_t getFileLastAccess (const char *fname) {

	char		*full_name;
	struct stat	statbuf;
	time_t		retval;

	if ((full_name = strdup (fname)) == NULL) {
		return FALSE;
	}

	stripTrailingChar (full_name, '/');
	if (stat (full_name, &statbuf) == -1) {
		retval = FALSE;
	} else {
#if defined __USE_XOPEN2K8
		retval = statbuf.st_atim.tv_sec;
#else
		retval = statbuf.st_atime;
#endif
	}

	free (full_name);
	return retval;

}

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

time_t getFileLastMod (const char *fname) {

	char		*full_name;
	struct stat	statbuf;
	time_t		retval;

	if ((full_name = strdup (fname)) == NULL) {
		return FALSE;
	}

	stripTrailingChar (full_name, '/');
	if (stat (full_name, &statbuf) == -1) {
		retval = FALSE;
	} else {
#if defined __USE_XOPEN2K8
		retval = statbuf.st_mtim.tv_sec;
#else
		retval = statbuf.st_mtime;
#endif
	}

	free (full_name);
	return retval;

}

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

time_t getFileLastStatus (const char *fname) {

	char		*full_name;
	struct stat	statbuf;
	time_t		retval;

	if ((full_name = strdup (fname)) == NULL) {
		return FALSE;
	}

	stripTrailingChar (full_name, '/');
	if (stat (full_name, &statbuf) == -1) {
		retval = FALSE;
	} else {
#if defined __USE_XOPEN2K8
		retval = statbuf.st_ctim.tv_sec;
#else
		retval = statbuf.st_ctime;
#endif
	}

	free (full_name);
	return retval;

}

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

int splitFilenameExt (const char *fname, char **ext) {
	if (!fname || !*ext) {
		*ext = NULL;
		return 1;
	}

	if (*fname == '\0') {
		*ext = NULL;
		return 1;
	}

	if ((*ext = rindex (fname, '.')) != NULL) {
		**ext = '\0';
		(*ext)++;
	}

	return 0;
}

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

char *raiseCase (char *string) {
	int		i;

	if (string == NULL) {
		fprintf (stderr, "ERROR: %s - NULL pointer passed\n", __FUNCTION__);
		return NULL;
	}

	for (i = 0; i < strlen (string); i++) {
		string[i] = toupper (string[i]);
	}

	return string;
}

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

char *lowerCase (char *string) {

	int		i;

	if (string == NULL) {
		fprintf (stderr, "ERROR: %s - NULL pointer passed\n", __FUNCTION__);
		return NULL;
	}

	for (i = 0; i < strlen (string); i++) {
		string[i] = tolower (string[i]);
	}

	return string;
}

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

int isNumber (char *string) {

	int		i;

	if ((string == NULL) || (string[0] == '\0')) {
		fprintf (stderr, "ERROR: %s - NULL pointer passed\n", __FUNCTION__);
		return (FALSE);
	}

	for (i = 0; i < strlen (string); i++) {
		if (!isdigit(string[i])) {
			return (FALSE);
		}
	}

	return (TRUE);
}

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

int moveFile (char *inpath, char *outpath, int overwrite) {

	int	retval;
	char	*filename;
	char	*tmp_string;
	char	*fullname;

	if ((inpath == NULL) || (*inpath == '\0')) {
		fprintf (stderr, "ERROR: %s - Invalid source path passed\n", __FUNCTION__);
		return ENOENT;
	}

	if ((outpath == NULL) || (*outpath == '\0')) {
		fprintf (stderr, "ERROR: %s Invalid destination path passed\n", __FUNCTION__);
		return ENOENT;
	}

	if (!fileExists (inpath)) {
		return ENOENT;
	}

	if (isRegularFile (inpath) && isDirectory (outpath)) {	/* Extract file name and append to outpath */
		tmp_string = strdup (inpath);
		filename = basename (tmp_string);
		fullname = malloc (strlen (outpath) + strlen (filename) + 2);
		sprintf (fullname, "%s/%s", outpath, filename);
		free (tmp_string);
	} else {
		fullname = strdup (outpath);
	}

	if (isRegularFile (outpath)) {
		if (overwrite) {
			unlink (outpath);
		} else {
			free (fullname);
			return EEXIST;
		}
	}

	if ((retval = rename (inpath, fullname)) == -1) {
		perror ("In moveFile");
	}

	free (fullname);
	return retval;
}

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

int copyFile (const char* source, const char* destination) {

	int		input;
	int		output;
	int		result;
	struct stat	fileinfo		= { 0 };

	if ((input = open (source, O_RDONLY)) == -1) {
		return -1;
	}
	if ((output = open (destination, O_RDWR | O_CREAT, S_IWUSR)) == -1) {
		close (input);
		return -1;
	}

	if ((result = fstat (input, &fileinfo)) != 0) {
		close (input);
		close (output);
		return result;
	}

	/* Here we use kernel-space copying for performance reasons */
#if defined(__APPLE__) || defined(__FreeBSD__)
	/* fcopyfile works on FreeBSD and OS X 10.5+ */
	result = fcopyfile (input, output, 0, COPYFILE_ALL);
#else
	/* sendfile will work with non-socket output (i.e. regular file) on Linux 2.6.33+ */
	off_t		bytesCopied		= 0;

	result = sendfile (output, input, &bytesCopied, fileinfo.st_size);
#endif

	close (input);
	close (output);

	if (result == 0) {
		result = chmod (destination, fileinfo.st_mode);
	} else {
		result = unlink (destination);
	}

	return result;
}

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

int changeDirectory (char *path, int create) {

	int retval = 0;

	if ((path == NULL) || (*path == '\0')) {
		fprintf (stderr, "ERROR: %s - Invalid path passed\n", __FUNCTION__);
		return (-1);
	}

	if (chdir (path) == -1) {
		if (errno == ENOENT) { /* Create the directory if it doesn't exist */
			if (create) {
				if (mkdir (path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) != 0) {
					fprintf (stderr, "ERROR: %s Error %d while creating directory %s\n", __FUNCTION__, errno, path);
					retval = -1;
				} else if (chdir (path) == -1) {
					fprintf (stderr, "ERROR: %s Error %d while changing directory to %s\n", __FUNCTION__, errno, path);
					retval = -1;
				}
			} else {
				fprintf (stderr, "ERROR: %s - Error %d while changing directory to %s\n", __FUNCTION__, errno, path);
				retval = -1;
			}
		} else {
			fprintf (stderr, "ERROR: %s - Error %d while changing directory to %s\n", __FUNCTION__, errno, path);
			retval = -1;
		}
	}

	return (retval);
}

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

char *dirtok_r (char *path, char **rpath) {

	char	*p;	/* Points to the beginning of the current subdir token */
	char	*t;	/* points to the remainder of the string. */

	if (path) {
		if (path[strlen(path)-1] == '/') {	/* Get rid of trailing '/' */
			path[strlen(path)-1] = '\0';
		}
		p = t = path;

		/* Find the beginning of a name */
		if (*t == '/') {
			t++;
		} else if (((*t == '.') || (*t == '~')) && (*(t+1) == '/')) {
			t += 2;
		} else if (((*t == '.') && (*(t+1) == '.') && (*(t+2) == '/'))) {
			t += 3;
		}

	} else {
		if (**rpath == '\0') {
			return NULL;
		}

		p = t = *rpath;
	}

	while ((*t != '/') && (*t != '\0'))
		t++;

	if (*t == '/') {
		*rpath = t+1;
		*t = '\0';
	} else {
		*rpath = t;
	}

	return p;
}

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

int makeDirectory (char *path, int make_parent, unsigned int perms) {

	int	retval = 0;
	mode_t	oldmask;

	if ((path == NULL) || (*path == '\0')) {
		fprintf (stderr, "ERROR: %s - Invalid path passed\n", __FUNCTION__);
		return (-1);
	}

	if (strcmp (path, "/") == 0) {
		fprintf (stderr, "ERROR: %s - Tried to create directory '/'\n", __FUNCTION__);
		return (-1);
	}

	if (perms == 0) {
		fprintf (stderr, "ERROR: %s - Invalid directory permissions (0) passed\n", __FUNCTION__);
		return (-1);
	}

	/* oldmask = umask (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH); */
	oldmask = umask (0);

	if (make_parent) {
		char	*tmpPath;
		char	*subDir;
		char	*remPath;
		char	*buildPath = NULL;
		int	retval;

		if ((tmpPath = strdup (path)) == NULL) {
			fprintf (stderr, "ERROR: %s - Call to strdup failed\n", __FUNCTION__);
			umask (oldmask);
			return (-1);
		}

		subDir = dirtok_r (tmpPath, &remPath);
		while (subDir) {
			if (!buildPath) {
				buildPath = strdup (subDir);
			} else {
				buildPath = realloc (buildPath, strlen (buildPath) + strlen (subDir) + 2);
				strcat(buildPath, "/");
				strcat(buildPath, subDir);
			}

			retval = fileExists (buildPath);
			if (retval != 1) {
				if (mkdir (buildPath, perms) != 0) {
					fprintf (stderr, "ERROR: %s - Error (%d) %s while creating directory %s\n", __FUNCTION__, errno, strerror (errno), buildPath);
					free (buildPath);
					free (tmpPath);
					umask (oldmask);
					return -1;
				}
			}

			subDir = dirtok_r (NULL, &remPath);
		}

		free (buildPath);
		free (tmpPath);
	} else {

		if (fileExists (path)) {
			retval = -1;
		} else if (mkdir (path, perms) != 0) {
			fprintf (stderr, "ERROR: %s - Error %d while creating directory %s\n", __FUNCTION__, errno, path);
			retval = -1;
		}
	}

	umask (oldmask);
	return (retval);
}
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

void freeVector (char **vector) {

	char	**pvtr;

	if (vector == NULL) {
		return;
	}

	for (pvtr = vector; *pvtr; pvtr++) {
		free (*pvtr);
	}

	free (vector);
}

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

void printVector (char **vector) {

	char	**pvtr;

	if (vector == NULL) {
		return;
	}

	for (pvtr = vector; *pvtr; pvtr++) {
		printf ("%s\n", *pvtr);
	}
}

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

long getFileAge (char *filepath) {
	struct stat		statbuf;			/* Structure for storing file attributes */
	long			file_age;

	if ((filepath == NULL) || (*filepath == '\0')) {
		fprintf (stderr, "ERROR: %s - Invalid file name passed\n", __FUNCTION__);
		return (-1);
	}

	if (stat (filepath, &statbuf) == -1) {
		fprintf (stderr, "ERROR: %s - Error %d while trying to stat %s\n", __FUNCTION__, errno, filepath);
		return (-1);
	}

	file_age = (long) difftime (time (NULL), statbuf.st_mtime);
	return (file_age / 60);
}

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

long getBlockSize (char *filename) {

	struct stat		statbuf;

	if ((filename == NULL) || (strlen (filename) == 0)) {
		return -1;
	}

	if (stat (filename, &statbuf) == -1) {
		return -1;
	}

	return (long) statbuf.st_blksize;
}

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

char *stripTrailingChar (char *string, char theChar) {

	while (string[strlen(string)-1] == theChar) {
		string[strlen(string)-1] = '\0';
	}

	return string;
}

#if 0
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

 int reopenStdFile (int fd, char *path) {


#define OPENPERMS		O_WRONLY | O_CREAT | O_TRUNC
#define OPENMODE		S_IRWXU | S_IRGRP | S_IROTH

	int	rstat;

	if ((rstat = close (fd)) == -1) {
		return errno;
	}

	if ((rstat = open (path, OPENPERMS, OPENMODE)) == -1) {
		return errno;
	}

	return (rstat == fd) ? 0 : -1;
}
#endif

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

void removeExtension (char *fname) {
	char	*lastdot;
	char	*lastsep;

	if (fname == NULL)
		return;

	lastdot = strrchr (fname, '.');
	lastsep = strrchr (fname, '/');

	/* If it has an extension separator. */

	if (lastdot != NULL) {	/* and it's before the extension separator */
		if (lastsep != NULL) {
			if (lastsep < lastdot) {
				*lastdot = '\0';
			}
		} else {	/* has extension separator with no path separator. */
			*lastdot = '\0';
		}
	}
}
