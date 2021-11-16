#include "config.h"

#include <stdio.h>
#include <semaphore.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <log.h>
#include "globals.h"

#include <assert.h>

#include "queueManager.h"
#include "frameReader.h"
#include "frameWriter.h"
#include "blender.h"
#include "InetSockAddr.h"

const char* const COPYRIGHT_NOTICE  = "Copyright (C) 2021 "
            "University Corporation for Atmospheric Research";

bool 	debug = false;
// =====================================================================

extern FrameWriterConf_t* fw_setConfig(int, const char*);
extern FrameReaderConf_t* fr_setReaderConf(int, char**, int, int);
extern QueueConf_t*   setQueueConf(double, int);

extern void 		  fw_init(FrameWriterConf_t*);
extern void 		  queue_init(QueueConf_t*);
extern void 		  reader_init(FrameReaderConf_t*);

// =====================================================================
static 	const char* const*  serverAddresses;	///< list of servers to connect to
static InetSockAddr* srvrSockAddrs[MAX_HOSTS];

static double         waitTime 		= 1.0;		///< max time between output frames
static int            hashTableSize	= HASH_TABLE_SIZE; ///< hash table capacity in frames
static const char*	  namedPipe 	= NULL;		///< pathname of output FIFO
static const char*	  logfile		= "/tmp/blender.out";		///< pathname of output messages
static  sem_t   	  sem;

// =====================================================================

/**
 * Unconditionally logs a usage message.
 *
 * @param[in] progName   Name of the program.
 * @param[in] copyright  Copyright notice.
 */
static void 
usage(
    const char* const          progName,
    const char* const restrict copyright)
{
    log_notice(
"\n\t%s - version %s\n"
"\n\t%s\n"
"\n"
"Usage: %s [-v|-x] [-h tbleSize] [-l log] [-t sec] pipe host:port ... \n"
"where:\n"
"   -h tblSize  Hash table capacity. Default is 1500.\n"
"   -l log     Log to `log`. One of: \"\" (system logging daemon), \"-\"\n"
"               (standard error), or file `log`. Default is \"%s\"\n"
"   -t sec 		Timeout in (decimal) seconds. Default is '1.0'.\n"
"   -v          Log through level INFO.\n"
"   -x          Log through level DEBUG. Too much information.\n"
"    pipe       Named pipe per channel. Example '/tmp/noaaportIngesterPipe'.\n"
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
        int     const          argc,
        char* const*  const restrict argv
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
           (ch = getopt(argc, argv, "vxh:l:t:")) != -1)
    {
        switch (ch) {
            case 'v':
                    printf("set verbose mode");
                break;
            case 'x':
                    printf("set debug mode");
                break;
            case 'h':
				if (sscanf(optarg, "%d", &hashTableSize) != 1 || hashTableSize < 0) {
					   printf("Invalid hash table size value: \"%s\"", optarg);
					   status = EINVAL;
				}
				break;
            case 'l':
            	if (sscanf(optarg, "%s", &logfile) != 1 ) {
                       printf("Invalid log file name: \"%s\"", optarg);
                       status = EINVAL;
            	}
                break;
            case 't':
                if (sscanf(optarg, "%lf", &waitTime) != 1 || waitTime < 0) {
                       printf("Invalid frame latency time-out value (max_wait): \"%s\"", optarg);
                       status = EINVAL;
                }
                break;
            default:
                break;        
        }
    }


    if(optind >= argc)
    	usage(argv[0], COPYRIGHT_NOTICE);

	namedPipe = argv[optind++];

	if(optind >= argc)
    	usage(argv[0], COPYRIGHT_NOTICE);

	const int serverCount = argc - optind;

	serverAddresses = (const char* const *)( argv	+ optind); ///< list of servers to connect to

    return status;
}

static bool
validateHostsInput( char * const* hostsList, int serverCount)
{
	// argv has host:port list
	char *hostAndPort, *ptr;

	int resp, exactCount=0;
	bool isHostname = false;

    for(int i=0; i< serverCount; i++)
    {
    	hostAndPort = *(hostsList + i);

    	char tmp[20];
    	char *ipV6 = tmp, *pTemp;
    	pTemp = ipV6;
    	int i = 0;
    	// check if IPv6
    	if( hostAndPort[0] == '[' )
    	{
			printf("'%s'\n", ptr);
			++exactCount;
			continue;

    	/*	while ( *(++hostAndPort)  )
    		{
    			i++;
    			if(*hostAndPort != ']')
    			{
    				 *ipV6++ = *hostAndPort;
    			}
    		}
    		tmp[i]='\0';
    		*ipV6='\0';
    		ipV6 = tmp;
    		if( (resp = isHostValid( (ipV6) , &isHostname )) == 1)
    		{
    			printf("IPv6: %s is VALID\n", ipV6);
    		}
    		else
    			printf("IPv6: %s is INVALID\n", ipV6);
    	*/
    	}
    	else
    	{
			if( (ptr = strtok(hostAndPort, ":" )) != NULL )//<-- won't work with IPv6 strings fdfg:dfgdfg:4534:
			{
				resp = isHostValid( ptr , &isHostname );
				if( resp == 0 && !isHostname )
				{
					printf("Warning: Incorrect host:port specification. Skipping entry: '%s' ...\n", hostAndPort);
					continue;
				}

				if( (ptr = strtok(NULL, ":" )) != NULL)
				{
					++exactCount;
					continue;
				}
				else
				{
					printf("Warning: Incorrect host:port specification. "
							"Skipping entry: '%s' ...\n", hostAndPort);
					continue;
				}
			} // if IPv4
    	} // if IPv6
    } // for
    return (exactCount == serverCount);
}

/**
 * Validate host - whether IP address or a hostname
 *
 * @param[in]  hostOrIP    	IP address / hostname of server
 * @param[out] isHostname  	Set to true if hostOrIP is a hostname
 * @retval 		1			hostOrIP is an IP address (v4 or v6)
 * @retval		0			hostOrIP is hostname if isHostname is true
 */
static int
isHostValid( char* hostOrIP, bool* isHostname )
{
    int status = 0;
	*isHostname = false;

    struct in_addr inaddr;
    struct in6_addr in6addr;

	if ( inet_pton(AF_INET, hostOrIP, &in6addr) == 1
	|| ( inet_pton(AF_INET, hostOrIP, &inaddr)  == 1))
	{
		status = 1;
	}
	else
	{
		*isHostname = true;
	}
	return status;
}

void
setFIFOPolicySetPriority(pthread_t pThread, char *threadName, int newPriority)
{

    int prevPolicy, newPolicy, prevPrio, newPrio;
    struct sched_param param;
    memset(&param, 0, sizeof(struct sched_param));

    // set it
    newPolicy = SCHED_FIFO;
    //=============== increment the consumer's thread's priority ====
    int thisPolicyMaxPrio = sched_get_priority_max(newPolicy);
    
    if( param.sched_priority < thisPolicyMaxPrio - newPriority) 
    {
        param.sched_priority += newPriority;
    }
    else
    {
        printf("Could not set a new priority to frameConsumer thread! \n");
        printf("Current priority: %d, Max priority: %d\n",  
            param.sched_priority, thisPolicyMaxPrio);
       // exit(EXIT_FAILURE);
    }


    int resp;
    resp = pthread_setschedparam(pThread, newPolicy, &param);
    if( resp )
    {
        printf("WARNING: setFIFOPolicySetPriority() : pthread_setschedparam() failure: %s\n", strerror(resp));
        //exit(EXIT_FAILURE);
    }
    else
    {
    	newPrio = param.sched_priority;
    	printf("Thread: %s \tpriority: %d, policy: %s\n",
    	        threadName, newPrio, newPolicy == 1? "SCHED_FIFO": newPolicy == 2? "SCHED_RR" : "SCHED_OTHER");
    }
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
        case SIGTERM:
            sem_post(&sem);
                    
            break;
        case SIGUSR1:
            // .. add as needed
            break;

        case SIGUSR2:
            // (void)log_roll_level();
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

    /*
     * Don't restart the following.
     *
     * SIGTERM must be handled in order to cleanly shutdown the file descriptors
     * 
     */
    (void)sigaction(SIGTERM, &sigact, NULL);

    /* Restart the following */

    sigset_t sigset;
    sigact.sa_flags |= SA_RESTART;

    (void)sigemptyset(&sigset);
    (void)sigaddset(&sigset, SIGUSR1);
    (void)sigaddset(&sigset, SIGUSR2);
    (void)sigaddset(&sigset, SIGTERM);
    (void)sigaction(SIGUSR1, &sigact, NULL);
    (void)sigaction(SIGUSR2, &sigact, NULL);    
    (void)sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

int main(
    const int argc,           /**< [in] Number of arguments */
    char* const    argv[])         /**< [in] Arguments */
{
    int status;
    /*
     * Initialize logging. Done first in case something happens that needs to
     * be reported.
     */
    const char* const progname = basename(argv[0]);

    char** serverAddresses;
    int serverCount;

    if (log_init(progname)) 
    {
        log_syserr("Couldn't initialize logging module");
        status = -1;
    }
    else 
    {
        (void)log_set_level(LOG_LEVEL_WARNING);

        status = decodeCommandLine(argc, argv);
        
        if (status) 
        {
        	log_add("Couldn't decode command-line");
            log_flush_fatal();

            usage(progname, COPYRIGHT_NOTICE);
        }
        else 
        {
            log_notice("Starting up %s", PACKAGE_VERSION);
            log_notice("%s", COPYRIGHT_NOTICE);

            // Ensures client and server file descriptors are closed cleanly,
            // so that read(s) and accept(s) shall return error to exit the threads.
            set_sigactions();

            // These values can/will change from one reader to another (ipAddress in socat)
            int policy 			= SCHED_RR;
            in_addr_t ipAddress = htonl(INADDR_ANY);	// to be passed in as plain host address
            in_port_t ipPort  	= PORT;					// to be passed in as ns
            int frameSize 		= SBN_FRAME_SIZE;
            FrameReaderConf_t* 	readerConfig 	= fr_setReaderConf(policy,
														serverAddresses,
														serverCount,
														frameSize);

            QueueConf_t* queueConfig 				= setQueueConf(waitTime, hashTableSize);

            FrameWriterConf_t* 	writerConfig 	= fw_setConfig(frameSize, namedPipe);

            // Init all modules
            fw_init( 		writerConfig );
            queue_init(  	queueConfig 		);
            reader_init( 	readerConfig );

            // 
            int ret;
            if( (ret = sem_init(&sem,0,0))  != 0)
            {
                log_add("sem_init() failure: errno: %d\n", ret);
                log_flush_fatal();
            }
            
            if( sem_wait(&sem) == -1 )
            {
                log_add("sem_init() failure: errno: %d\n", ret);
                log_flush_fatal();
            }            
        }   /* command line decoded */

        if (status) 
        {
            log_add("Couldn't ingest NOAAPort data");
            log_flush_error();
        }
        
  }  // log_fini();
    
        return status ? 1 : 0;
}

//-----------------------------------------------------------------
// 										Beyond, not used
/*
static void
setTimerOnSocket(int *pSockFd, int microSec)
{
    // set a timeout on the receiving socket
    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = microSec;
    setsockopt(*pSockFd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
}
*/

