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

#include <assert.h>

#include "queueManager.h"
#include "frameReader.h"
#include "frameWriter.h"
#include "blender.h"

const char* const COPYRIGHT_NOTICE  = "Copyright (C) 2021 "
            "University Corporation for Atmospheric Research";
const char* const PACKAGE_VERSION   = "0.1.0";

bool 	debug = false;
// =====================================================================

extern FrameWriterConf_t* fw_setConfig(int, char*);
extern FrameReaderConf_t* setFrameReaderConf(int, in_addr_t, in_port_t, int);
extern QueueConf_t*   setQueueConf(double, int);

extern void 		  fw_init(FrameWriterConf_t*);
extern void 		  queue_init(QueueConf_t*);
extern void 		  reader_init(FrameReaderConf_t*);

// =====================================================================

static char*          mcastSpec 		= NULL;
static char*          interface 		= NULL; // Listen on all interfaces unless specified on argv
static int            rcvBufSize 		= 0;
static int            socketTimeOut		= MIN_SOCK_TIMEOUT_MICROSEC;
static double         frameLatency 		= 0;
static int            hashTableSize		= HASH_TABLE_SIZE;
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
    /*
    int level = log_get_level();
    (void)log_set_level(LOG_LEVEL_NOTICE);

    log_notice_q(
    */
    printf(
"\n\t%s - version %s\n"
"\n\t%s\n"
"\n"
"Usage: %s [v|x] [-h tbleSize] [-l log] [-m addr] [-I ip_addr] [-R bufSize] [-s suffix] [-t sec]\n"
"where:\n"
"   -I ip_addr  Listen for multicast packets on interface \"ip_addr\".\n"
"               Default is system's default multicast interface.\n"
"   -h tblSize  Hash table capacity. Default is 1500.\n"
"   -l dest     Log to `dest`. One of: \"\" (system logging daemon), \"-\"\n"
"               (standard error), or file `dest`. Default is \"%s\"\n"
"   -m addr     Read data from IPv4 dotted-quad multicast address \"addr\".\n"
"               Default is to read from the standard input stream.\n"
"   -p pipe     named pipe per channel. Default is '/tmp/noaaportIngesterPipe'.\n"
"   -R bufSize  Receiver buffer size in bytes. Default is system dependent.\n"
"   -t sec 		Timeout in seconds. Default is '1.0'.\n"
"   -v          Log through level INFO.\n"
"   -x          Log through level DEBUG. Too much information.\n"
"\n",
        progName, PACKAGE_VERSION, copyright, progName);

//    (void)log_set_level(level);

    exit(0);
}

/**
 * Decodes the command-line.
 *
 * @param[in]  argc           Number of arguments.
 * @param[in]  argv           Arguments.

 * @param[out] mcastSpec      Specification of multicast group.
 * @param[out] interface      Specification of interface on which to listen.
 * @param[out] rcvBufSize     Receiver buffer size in bytes
 * @param[out] namedPipe      Name of namedPipe the noaaportIngester is listening to     
 *                            (Default is /tmp/noaaportIngester)
 * @retval     0              Success.
 * @retval     EINVAL         Error. `log_add()` called.
 */
static int 
decodeCommandLine(
        int                    argc,
        char**  const restrict argv,
        char**  const restrict mcastSpec,
        char**  const restrict imr_interface,
        int*    const restrict sockTimeOut,
        int*    const restrict rcvBufSize,
        char**  const restrict namedPipe,
        double*  const restrict frameLatency,
		int*    const restrict hashTableSize
        )
{
    int                 status = 0;
    extern int          optind;
    extern int          opterr;
    extern char*        optarg;
    extern int          optopt;
    
    
    int ch;
    
    opterr = 0;                         /* no error messages from getopt(3) */
    /* Initialize the logger. */
/*    if (log_init(argv[0])) {
        log_syserr("Couldn't initialize logging module");
        exit(1);
    }
*/
    while (0 == status &&
           (ch = getopt(argc, argv, "vxI:h:l:m:p:R:r:t:")) != -1)
    {
        switch (ch) {
            case 'v':
                    printf("set verbose mode");
                break;
            case 'x':
                    printf("set debug mode");
                break;
            case 'I':
                    *imr_interface = optarg;
                break;
            case 'h':
					if (sscanf(optarg, "%d", hashTableSize) != 1 || *hashTableSize < 0) {
					   printf("Invalid hash table size value: \"%s\"", optarg);
					   status = EINVAL;
					}
					break;
            case 'l':
                    printf("logger spec");
                break;
            case 'm':
                    *mcastSpec = optarg;
                break;
            case 'p':
                    *namedPipe = optarg;
                break;
            case 'R':
                if (sscanf(optarg, "%lf", rcvBufSize) != 1 || *rcvBufSize <= 0) {
                       printf("Invalid receive buffer size: \"%s\"", optarg);
                       //log_add("Invalid receive buffer size: \"%s\"", optarg);
                       status = EINVAL;
                }
                break;
            case 'r':
                if (sscanf(optarg, "%d", sockTimeOut) != 1 || *sockTimeOut < 0) {
                       printf("Invalid socket time-out value: \"%s\"", optarg);
                }
                break; 
            case 't':
                if (sscanf(optarg, "%f", frameLatency) != 1 || *frameLatency < 0) {
                       printf("Invalid frame latency time-out value (max_wait): \"%s\"", optarg);
                       //log_add("Invalid receive buffer size: \"%s\"", optarg);
                       status = EINVAL;
                   }
                break;
            default:
                break;        
        }
    }

    if (argc - optind != 0)
        usage(argv[0], COPYRIGHT_NOTICE);

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
    char*     argv[])         /**< [in] Arguments */
{
    int status;
    char *namedPipe = NULL;
    /*
     * Initialize logging. Done first in case something happens that needs to
     * be reported.
     */
    const char* const progname = basename(argv[0]);
/*    
    if (log_init(progname)) 
    {
        log_syserr("Couldn't initialize logging module");
        status = -1;
    }
    else 
    {
        (void)log_set_level(LOG_LEVEL_WARNING);

*/         
        status = decodeCommandLine(argc, argv, 
                    &mcastSpec, 
                    &interface, 
                    &socketTimeOut, 
                    &rcvBufSize, 
                    &namedPipe,
                    &frameLatency,
					&hashTableSize);
        
        if (status) 
        {
            printf("Couldn't decode command-line\n");
/*          log_add("Couldn't decode command-line");
            log_flush_fatal();
*/                
            usage(progname, COPYRIGHT_NOTICE);
        }
        else 
        {
            printf("\n\tStarted (v%s)\n", PACKAGE_VERSION);
            printf("\n\t%s\n\n", COPYRIGHT_NOTICE);
/*          log_notice("Starting up %s", PACKAGE_VERSION);
            log_notice("%s", COPYRIGHT_NOTICE);
*/
            // Ensures client and server file descriptors are closed cleanly,
            // so that read(s) and accept(s) shall return error to exit the threads.
            set_sigactions();

            // These values can/will change from one reader to another (ipAddress in socat)
            int policy 			= SCHED_RR;
            in_addr_t ipAddress = htonl(INADDR_ANY);	// to be passed in as plain host address
            in_port_t ipPort  	= PORT;			// to be passed in as ns
            int frameSize 		= SBN_FRAME_SIZE;
            FrameReaderConf_t* 	aFrameReaderConfig 	= setFrameReaderConf(policy,
            											ipAddress,
														PORT,
														frameSize);

            QueueConf_t* aQueueConfig 				= setQueueConf(frameLatency, hashTableSize);

            FrameWriterConf_t* 	aFrameWriterConfig 	= fw_setConfig(frameSize, namedPipe);

            // Init all modules
            fw_init( 		aFrameWriterConfig );
            queue_init(  	aQueueConfig 		);
            reader_init( 	aFrameReaderConfig );

            // 
            int ret;
            if( (ret = sem_init(&sem,0,0))  != 0)
            {
                printf("sem_init() failure: errno: %d\n", ret);
                exit(EXIT_FAILURE);
            }
            
            if( sem_wait(&sem) == -1 )
            {
                printf("sem_wait() failure: errno: %d\n", errno);
                exit(EXIT_FAILURE);
            }            

        }   /* command line decoded */

        if (status) 
        {
            printf("Couldn't ingest NOAAPort data");
/*                  log_add("Couldn't ingest NOAAPort data");
            log_flush_error();
*/              
        }
        
  //}   // log_fini();
    
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

