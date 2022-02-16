#include "config.h"

#include "queueManager.h"
#include "frameReader.h"
#include "blender.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <limits.h>
#include <log.h>

const char* const COPYRIGHT_NOTICE  = "Copyright (C) 2021 "
            "University Corporation for Atmospheric Research";

// =====================================================================
static	int				serverCount;
static	char*	const*	serverAddresses;
static  char 			blenderArguments[PATH_MAX]="";

static double	waitTime = 1.0;					///< max time between output frames
static char*	logfile	 = "/tmp/blender.log";	///< pathname of output messages
// =====================================================================

/**
 * Unconditionally logs a usage message.
 *
 * @param[in] progName   Name of the program.
 * @param[in] copyright  Copyright notice.
 */
static void 
usage( const char* const          progName,
       const char* const restrict copyright)
{
    log_notice(
"\n\t%s - version %s\n"
"\n\t%s\n"
"\n"
"Usage: %s [-v|-x] [-l log] [-t sec] host:port ... \n"
"where:\n"
"   -l log      Log to `log`. One of: \"\" (system logging daemon), \"-\"\n"
"               (standard error), or file `log`. Default is \"%s\"\n"
"   -t sec 		Timeout in (decimal) seconds. Default is '1.0'.\n"
"   -v          Log through level INFO.\n"
"   -x          Log through level DEBUG. Too much information.\n"
"    host:port  Server(s) host <host>, port <port> that the blender reads its data from.\n"
"\n",
        progName, PACKAGE_VERSION, copyright, progName, logfile);

    exit(1);
}

/**
 * Decodes the command-line.
 *
 * @param[in]  argc           Number of arguments.
 * @param[in]  argv           Arguments.
 */
static int 
decodeCommandLine(
        int     const  			argc,
        char* const*  const 	argv
        )
{
    int                 status = 0;
    extern int          optind;
    extern int          opterr;
    extern char*        optarg;
    extern int          optopt;
    int ch;
    opterr = 0;                         /* no error messages from getopt(3) */

    while (0 == status &&
           (ch = getopt(argc, argv, ":vxl:t:")) != -1)
    {
        switch (ch) {
            case 'v':
                (void)log_set_level(LOG_LEVEL_INFO);
                strcat(blenderArguments, " -v ");
                break;
            case 'x':
        	   	(void)log_set_level(LOG_LEVEL_DEBUG);
        	   	strcat(blenderArguments, " -x ");
                break;
            case 'l':
              	if (sscanf(optarg, "%ms", &logfile) != 1 ) {
              		log_add("Invalid log file name: \"%s\"", optarg);
                    status = EINVAL;
                    break;
                }
              	strcat(blenderArguments, " -l ");
              	strcat(blenderArguments, logfile);
              	strcat(blenderArguments, " ");
              	log_set_destination(logfile);
              	free(logfile);
                break;
            case 't':
                if (sscanf(optarg, "%lf", &waitTime) != 1 || waitTime < 0) {
                	log_add("Invalid frame latency time-out value (max_wait): \"%s\"", optarg);
                    status = EINVAL;
                }
              	strcat(blenderArguments, " -t ");
              	strcat(blenderArguments, optarg);
              	strcat(blenderArguments, " ");
                break;
            case '?': {
                log_add("Unknown option: \"%c\"", ch);
                usage(argv[0], COPYRIGHT_NOTICE);
                break;
            }
            case ':': {
                log_add("Option \"%c\" is missing its argument", ch);
                usage(argv[0], COPYRIGHT_NOTICE);
                break;
            }
        }
        log_flush_warning();
    }

    if(optind >= argc)
    	usage(argv[0], COPYRIGHT_NOTICE);

	// RIP: namedPipe = argv[optind++];

	serverCount 	= argc - optind;
	serverAddresses = &argv[optind]; ///< list of servers to connect to
	for(int i=0; i<serverCount; i++)
	{
		strcat(blenderArguments, serverAddresses[i]);
		strcat(blenderArguments, " ");
	}

    return status;
}


/**
 * Handles a signal.
 *
 * @param[in] sig  The signal to be handled.
 */
static void 
signal_handler( const int sig)
{
    switch (sig) {
        case SIGUSR1:
            // .. add as needed
            break;
        case SIGUSR2:
            (void)log_roll_level();
            break;
    }
    return;
}

/**
 * Registers the signal handler for most signals.
 */
static void 
set_sigactions(void)
{
    struct sigaction sigact;
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;

    /* Handle the following */
    sigact.sa_handler = signal_handler;

    /* Restart the following */

    sigset_t sigset;
    sigact.sa_flags |= SA_RESTART;

    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaction(SIGUSR1, &sigact, NULL);
    (void)sigaction(SIGUSR2, &sigact, NULL);    
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

int main(
    const int argc,           /**< [in] Number of arguments */
    char* const    argv[])    /**< [in] Arguments */
{
    int status;
    /*
     * Initialize logging. Done first in case something happens that needs to
     * be reported.
     */
    const char* const progname = basename(argv[0]);

    if (log_init(progname)) 
    {
        log_syserr("Couldn't initialize logging module");
        status = -1;
    }
    else 
    {
        status = decodeCommandLine(argc, argv);
        if (status) 
        {
        	log_add("Couldn't decode command-line");
            log_flush_fatal();

            usage(progname, COPYRIGHT_NOTICE);
        }
        else 
        {
            log_notice("Starting up v%s blender %s", PACKAGE_VERSION, blenderArguments );
            log_notice("%s", COPYRIGHT_NOTICE);

            // Ensures client and server file descriptors are closed cleanly,
            // so that read(s) and accept(s) shall return error to exit the threads.
            set_sigactions();

            // Start all modules
            queue_start( waitTime );

            if( reader_start(serverAddresses, serverCount ) )
            {
            	exit(EXIT_FAILURE);
            }

            for(;;)
            	pause();
            // 
        }   /* command line decoded */
  }  // log_fini();
    
        return status ? 1 : 0;
}
