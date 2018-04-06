/**
 * C program to send a HUP signal to syslogd.
 * Install it setuid root.
 */

#include <config.h>
#if defined(SYSLOG_PIDFILE)
/*
 * On most systems we get the pid from
 * a file
 */

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

static char *
kerrstr(int errnum)
{
	switch (errnum) {
	case ESRCH :
		return "No such process" ;
	case EPERM :
		return "Permission denied" ;
	}
	/* default */
	return "" ;
}

/*ARGSUSED0*/
int main(int argc, char *argv[])
{
	int pid ;
	FILE *pidfile = fopen(SYSLOG_PIDFILE, "r") ;

	if(pidfile == NULL)
	{
		fprintf(stderr, "%s: couldn't open %s: %s\n", argv[0],
		        SYSLOG_PIDFILE, strerror(errno)) ;
		exit(1) ;
	}
	
	if(fscanf(pidfile, "%d", &pid) < 1)
		exit(2) ;

	if(kill(pid, SIGHUP) < 0)
	{
		fprintf(stderr, "%s: kill -HUP %d: %s\n", argv[0], pid, kerrstr(errno) ) ;
		exit(3) ;
	}

	return 0;
}

#else

/*
 * SGI version. The file /etc/syslog.pid doesn't exist,
 * but we have this convenient killall program.
 */
#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char *argv[])
{
 	const int status = system ("/etc/killall -HUP syslogd");
	if(status != 0)
	{
		fprintf(stderr,
		 "%s: system(\"/etc/killall -HUP syslogd\") returns %d\n",
		 argv[0], status);
		fprintf(stderr,
		 "(E.G. It didn't work. Check that this program is setuid)\n");
	}
	return status;
}
#endif

