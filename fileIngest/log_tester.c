/*
 * log_tester.c
 *
 *  Created on: Nov 29, 2013
 *      Author: brapp
 */


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
#include <time.h>
#include <signal.h>
#include "stdclib.h"
#include "mlogger.h"

#define DEF_LOG_SIZE			(4 * 1024 * 1024)
#define TRACE_LOG_SIZE			(100 * 1024 * 1024)
#define LOG_BUFFER_SIZE			1024
#define COMMON_OPTS			(O_ARCHIVE | O_TIMESTAMP | O_KEEP_OPEN | O_ADD_NEWLINE | O_FLUSH_AFTER_EACH)
#define PROD_LOG_OPTS			(COMMON_OPTS)
#define ERR_LOG_OPTS			(COMMON_OPTS | O_LOG_INIT | O_SHOW_SEVERITY)

LOGGER		*eLog;
LOGGER		*pLog;
int		Done;
int		ei	= 0;
int		pi	= 0;

static void setExitFlag (int signum) {

	/* set global flag for termination	*/
	Done = TRUE;

	logMsg (eLog, V_INFO, S_STATUS,
		"Received signal %d, setting exit flag", signum);
}

static int initLogs () {

	pLog = logInitLogger ("Product Log", F_FILE, PROD_LOG_OPTS, V_ERROR,
			"/home/brapp/test", "test.product.log", TRACE_LOG_SIZE, LOG_BUFFER_SIZE);
	if (!pLog) {
		fprintf (stderr, "FATAL: %s - could not open transaction log\n", __FUNCTION__);
		return 1;
	}

	eLog = logInitLogger ("Error Log", F_FILE, ERR_LOG_OPTS, V_DEBUG,
			"/home/brapp/test", "test.error.log", TRACE_LOG_SIZE, LOG_BUFFER_SIZE);
	if (!eLog) {
		fprintf (stderr, "FATAL: %s - could not open transaction log\n", __FUNCTION__);
		return 1;
	}

	return 0;
}

static void setupSigHandler () {
	struct sigaction act;
	static const char fname[40+1]="setupSigHandler";

	/* SIGUSR1 will exit after sending the current product	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = setExitFlag;
	act.sa_flags=0;
	if (sigaction (SIGUSR1, &act, 0) == -1) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Sigaction FAIL sig=%d, act=sigsetexitflag, %s\n",
			fname, SIGUSR1, strerror(errno));
	}

	/* SIGTERM will exit after sending the current product	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = setExitFlag;
	act.sa_flags=0;
	if (sigaction (SIGTERM, &act, 0) == -1) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Sigaction FAIL sig=%d, act=sigsetexitflag, %s\n",
			fname, SIGTERM, strerror(errno));
	}

	/* SIGHUP will exit the process	*/
	sigemptyset (&act.sa_mask);
	act.sa_handler = setExitFlag;
	act.sa_flags=0;
	if (sigaction (SIGHUP, &act, 0) == -1) {
		logMsg (eLog, V_ERROR, S_ERROR,
			"(%s) - Sigaction FAIL sig=%d, act=sigexitnow, %s\n",
			fname, SIGHUP, strerror(errno));
	}

	return;
}


static void atExitHandler () {

	logShutdown ();
	puts ("Shutting Down.");
}

int main (int argc, char **argv) {

	Done = FALSE;

	initLogs ();

	atexit (&atExitHandler);
	setupSigHandler ();

	while (!Done) {
		logMsg (eLog, V_ALWAYS, S_STATUS, "Error Message #%d", ++ei);
//		logMsg (pLog, V_ALWAYS, S_STATUS, "Product Message #%d", ++pi);
//		usleep (100000);
	}

	return 0;
}
