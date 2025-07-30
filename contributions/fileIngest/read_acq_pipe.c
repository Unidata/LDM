/* -----------------------------------------------------------------------------
 *
 * File Name:
 *	read_acq_pipe.c
 *
 * Description:
 *
 * Build Instructions:
 *
 * Functions defined:
 *
 * Author:
 * 	brapp		Jun 27, 2015
 *
 * Modification History:
 *	Modified by		Date
 *	Description
 *
 * -------------------------------------------------------------------------- */

#define _GNU_SOURCE

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include "stdclib.h"

#define	MAX_ACQ_PATH_LEN		128
#define OUTFILE_FINAL_PERMS		(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

typedef  struct	pipe_prod_name_hdr  {				/* prod filename hdr for pipe 	     */
	int		pipe_insert_time;			/* time product added to pipe	     */
	int		pipe_prod_NCF_rcv_time;			/* time product received at NCF	     */
	ushort		pipe_prod_type;				/* product category                  */
								/*     TYPE_GOES, TYPE_NWSTG, etc    */
	ushort		pipe_prod_cat;				/* product category                  */
								/*     CAT_IMAGE, etc		     */
	ushort		pipe_prod_code;				/* product code 1,2,3,etc            */
	ushort		pipe_prod_flag;				/* product flag (error & status)     */
								/*     error_mask & prod_done,etc    */
	ushort		reserve1;				/* reserved	                     */
	ushort		reserve2;				/* reserved	                     */
	uint		pipe_prod_orig_prod_seqno;		/* original prod seqno to retransmit */
	int		pipe_prod_orig_NCF_rcv_time;		/* NCF receive time binary(GMT) */
		/* Retransmit info */
	ushort		pipe_prod_run_id;			/* Unique run identification    */
								/*    for product stream    */
								/*    parm for retransmission   */
	ushort		pipe_prod_orig_run_id;			/* Unique orig run identification    */
	char		pipe_prod_filename[MAX_ACQ_PATH_LEN];	/* UNIX filename for prod */
} PIPE_PROD_NAME_HDR;

typedef struct	file_node {
	char		*fptr;		/* File name (without path) */
	time_t		mtime;		/* File last modification time */
	off_t		fsize;		/* File size in bytes */
}	FILE_NODE;

typedef struct	file_list_hdr {
	FILE_NODE	*fileNodes;	/* Pointer to array of FILE_NODEs */
	int		count;		/* Number of files in list */
}	FILE_LIST;

char 		*pipeFile		= "/dev/p_INET";
int		Done			= FALSE;

/* -----------------------------------------------------------------------------
 * Function Name
 *	atExitHandler
 *
 * Format
 *	void adExitHandler ()
 *
 * Arguments
 *	N/A
 *
 * Description
 *	Handler called when the 'exit()' system call is issued to terminate
 *	this program.  It shuts down the logging facilities, and 'free's up
 *	some dynamically allocated memory.
 *
 *	If LDM support is compiled in, the LDM product queue is closed and
 *	the MD5 context is freed up.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void atExitHandler () {

	puts ("Done.");
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	sigsetexitflag
 * Format
 *	void sigsetexitflag (int signum)
 *
 * Arguments
 *	int signum
 *	Signal that triggered execution of this trap.
 *
 * Description
 *	Handler function for signals that should terminate the process when
 *	the current product has been processed.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void sigsetexitflag (int signum) {

	/* set global flag for termination	*/
	Done = TRUE;

	fprintf (stderr,
		"Received signal %d, setting exit flag\n",
		signum);
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	siglogandcontinue
 *
 * Format
 *	void siglogandcontinue (int signum)
 *
 * Arguments
 *	int signum
 *	Signal that triggered execution of this trap.
 *
 * Description
 *	Handler function for signals that should be ignored.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void siglogandcontinue (int signum) {

	fprintf (stderr,
		"Received signal %d, ignored\n",
		signum);
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	sigexitnow
 *
 * Format
 *	void sigexitnow (int signum)
 *
 * Arguments
 *	int signum
 *	Signal that triggered execution of this trap.
 *
 * Description
 *	Handler function for signals that should terminate the process immediately.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

static void sigexitnow (int signum) {

	fprintf (stderr,
		"Received signal %d, exit process immediately\n",
		signum);

	exit (0);
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	setupSignalHandlers
 *
 * Format
 *	void setupSignalHandlers ()
 *
 * Arguments
 *	N/A
 *
 * Description
 *	Set up initial signal handlers for the process.
 *
 * Return Values
 *
 * -------------------------------------------------------------------------- */

static void setupSigHandler () {
	struct sigaction act;
	static const char fname[40+1]="setupSigHandler";

	/* SIGUSR1 will exit after sending the current product	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = sigsetexitflag;
	act.sa_flags=0;
	if (sigaction (SIGUSR1, &act, 0) == -1) {
		fprintf (stderr,
			"(%s) - Sigaction FAIL sig=%d, act=sigsetexitflag, %s\n",
			fname, SIGUSR1, strerror(errno));
	}

	/* SIGTERM will exit after sending the current product	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = sigsetexitflag;
	act.sa_flags=0;
	if (sigaction (SIGTERM, &act, 0) == -1) {
		fprintf (stderr,
			"(%s) - Sigaction FAIL sig=%d, act=sigsetexitflag, %s\n",
			fname, SIGTERM, strerror(errno));
	}

	/* SIGHUP will exit the process	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = sigexitnow;
	act.sa_flags=0;
	if (sigaction (SIGHUP, &act, 0) == -1) {
		fprintf (stderr,
			"(%s) - Sigaction FAIL sig=%d, act=sigexitnow, %s\n",
			fname, SIGHUP, strerror(errno));
	}

	/* SIGINT will be handled	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = sigsetexitflag;
	act.sa_flags=0;
	if (sigaction (SIGINT, &act, 0) == -1) {
		fprintf (stderr,
			"(%s) - Sigaction FAIL sig=%d, act=siglogandcontinue, %s\n",
			fname, SIGCHLD, strerror(errno));
	}

	/* SIGPIPE will be ignored	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = siglogandcontinue;
	act.sa_flags=0;
	if (sigaction (SIGPIPE, &act, 0) == -1) {
		fprintf (stderr,
			"(%s) - Sigaction FAIL sig=%d, act=siglogandcontinue, %s\n",
			fname, SIGCHLD, strerror(errno));
	}

	/* SIGALRM will be ignored	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = siglogandcontinue;
	act.sa_flags=0;
	if (sigaction (SIGALRM, &act, 0) == -1) {
		fprintf (stderr,
			"(%s) - Sigaction FAIL sig=%d, act=siglogandcontinue, %s\n",
			fname, SIGALRM, strerror(errno));
	}

//	/* SIGIO will be handled	*/
//	sigemptyset (&act.sa_mask);
//	act.sa_handler = sighandleIO;
//	act.sa_flags=SA_SIGINFO;
//	if (sigaction (SIGIO, &act, 0) == -1) {
//		fprintf (stderr,
//			"%s: Sigaction FAIL sig=%d, act=sighandleIO, %s\n",
//			fname, SIGIO, strerror(errno));
//	}

	return;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	addFileToList
 *
 * Format
 *	int addFileToList (FILE_LIST *flist, char *fname, time_t ftime, off_t fsize)
 *
 * Arguments
 *	FILE_LIST	*flist
 *	Pointer to FILE_LIST structure that will hold information about matching files.
 *
 *	char		*fname
 *	Name of file to insert
 *
 *	time_t		ftime
 *	File last modification time
 *
 *	off_t		fsize
 *	Size of file in bytes
 *
 * Description
 * 	Adds a new FILE_NODE to the flist FILE_LIST.
 *
 * Return Values
 *	0	Success
 *	1	Failure
 *
 * -------------------------------------------------------------------------- */

int addFileToList (FILE_LIST *flist, char *fname, time_t ftime, off_t fsize) {

	if (flist->count == 0) {
		if ((flist->fileNodes = malloc (sizeof (FILE_NODE))) == NULL) {
			fprintf (stderr,
				"(%s) - could not malloc memory for file node list\n",
				__FUNCTION__);
			return 1;
		}
	} else {
		if ((flist->fileNodes = realloc ((void *) flist->fileNodes, sizeof (FILE_NODE) * flist->count)) == NULL) {
			fprintf (stderr,
				"(%s) - could not realloc memory for file node list\n",
				__FUNCTION__);
			return 1;
		}
	}

	if ((flist->fileNodes[flist->count-1].fptr = strdup (fname)) == NULL) {
		fprintf (stderr,
			"(%s) - could not malloc memory for file name\n",
				__FUNCTION__);
		return 1;
	}

	flist->count++;
	flist->fileNodes[flist->count-1].mtime = ftime;
	flist->fileNodes[flist->count-1].fsize = fsize;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	freeFileList
 *
 * Format
 *	void freeFileList (FILE_LIST *fl)
 *
 * Arguments
 *	FILE_LIST	*fl
 *	Pointer to FILE_LIST
 *
 * Description
 *	This function will free all memory in a FILE_LIST.
 *
 * Return Values
 *	N/A
 *
 * -------------------------------------------------------------------------- */

void freeFileList (FILE_LIST *fl) {

	int	i;

	if (fl == NULL) {
		return;
	}

	for (i = 0; i < fl->count; i++) {
		fprintf (stderr,
			"(%s) - Freeing file node: %s\n",
			__FUNCTION__, fl->fileNodes[i].fptr);
		free ((void *) fl->fileNodes[i].fptr);
	}

	free ((void *) fl->fileNodes);
	fl->count = 0;
}

/* -----------------------------------------------------------------------------
 * Function Name
 *	readAcqPipe
 *
 * Format
 *	int readAcqPipe (int pipefd, FILE_LIST *fileList)
 *
 * Arguments
 *	int		pipefd
 *	File descriptor for already-open pipe
 *
 *	FILE_LIST	*fileList
 *	If successful, will contain a FILE_LIST element for a single file.
 *
 * Description
 *	Read a PIPE_PROD_NAME_HDR structure from the pipe.
 *
 * Return Values
 *	0	success
 *	1	failure
 *
 * -------------------------------------------------------------------------- */

#define RETRIES		3


int readAcqPipe(int pipefd, FILE_LIST *fileList) {
	int rtn_value; /* return value from read */
	int total_read; /* total read bytes */
	int read_size; /* bytes to read */
	int request_size;
	int numb_retries; /* numb of retries */
	struct pipe_prod_name_hdr prod_entry;
	char *p_readbuff; /* ptr to read buffer */

	total_read = 0;
	request_size = read_size = sizeof(PIPE_PROD_NAME_HDR);
	p_readbuff = (char *) &prod_entry;
	numb_retries = 0;

	while (total_read != request_size) {

		rtn_value = read(pipefd, p_readbuff, read_size);

		if (rtn_value < 0) {
			if (errno == EINTR) {
				fprintf (stderr, "(%s) Interrupt received\n", __FUNCTION__);
				return 1;
			}

			fprintf(stderr, "%s ERROR read(%d) fd[%d] %s\n", __FUNCTION__, rtn_value, pipefd, pipeFile);
			/* Return error to caller */
			return 1;
		}

		if (rtn_value == 0) {
			/* Check if need to continue */
			numb_retries++;
			/* Note caller should determine number of retries */
			/*  based on type of input */
			if (numb_retries < RETRIES) {
				if ((total_read > 0) && (numb_retries > 0)) {
					fprintf(stderr, "(%s) pipe read returned 0, sleeping for 1 second\n", __FUNCTION__);
					sleep(1);
				}

				continue;
			}

			/* Assume have end of pipe */
			fprintf(stderr, "(%s) quit read %d vs %d bytes retried=%d\n", __FUNCTION__, total_read, request_size,
					numb_retries);
			break;
		}

		total_read += rtn_value;
		if (rtn_value != read_size) {
			fprintf(stderr, "(%s) %d vs %d bytes fd=%d %s\n", __FUNCTION__, rtn_value, read_size, pipefd, pipeFile);
			read_size = request_size - total_read;
			p_readbuff += rtn_value;
		}
	} /* end while loop for read */

	if (rtn_value > 0) {
		fprintf(stderr, "%s ok read (fd=%d) %d of %d bytes retried=%d\n", __FUNCTION__, pipefd, total_read, request_size,
				numb_retries);
	}

	if (total_read == request_size) { /* Read an entire product */
		fprintf(stderr, "(%s) Read entry for %s\n", __FUNCTION__, prod_entry.pipe_prod_filename);
	}

	return 0;

} /* end do_read */


int main (int argc, char **argv) {

	int		readPipe;
	FILE_LIST	fileList;

	atexit (&atExitHandler);
	setupSigHandler ();

	if (!fileExists(pipeFile)) {
		if (mknod(pipeFile, S_IFIFO | OUTFILE_FINAL_PERMS, 0)) {
			fprintf (stderr, "(%s) - Error (%d) \"%s\" creating pipe %s\n",
				__FUNCTION__, errno, strerror(errno), pipeFile);
			return 1;
		}
	} else if (getFileType(pipeFile) != S_IFIFO) {
		fprintf (stderr, "(%s) - %s must be a pipe\n", __FUNCTION__, pipeFile);
		return 1;
	}

	if ((readPipe = open(pipeFile, O_RDONLY, 0)) == -1) {
		fprintf (stderr, "(%s) - Error (%d) \"%s\" opening pipe %s\n",
			__FUNCTION__, errno, strerror(errno), pipeFile);
		return 1;
	}

	while (!Done) {
		readAcqPipe (readPipe, &fileList);

		freeFileList (&fileList);
	}

	return 0;
}

