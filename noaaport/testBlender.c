#include "config.h"

#include "globals.h"

// server program for TCP connection
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <libgen.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>
#include <math.h>

#include <log.h>

#define NUMBER_FRAMES_TO_SEND 	3
#define TEST_MAX_THREADS 		200
#define	SBN_FRAME_SIZE 			4000
#define	SBN_DATA_BLOCK_SIZE 	5000
#define MAX_FRAMES_PER_SEC		3500	// This comes from plot in rstat on oliver for CONDUIT
#define MAX_CLIENTS 			10
#define NUMBER_OF_RUNS           5
#define ONE_BILLION				1000000000

static int nbrFrames, nbrRuns;
static uint32_t framesCadence;
static uint32_t waitBetweenRuns;

const char* const COPYRIGHT_NOTICE  = "Copyright (C) 2021 "
            "University Corporation for Atmospheric Research";

typedef struct SocketAndSeqNum {
    uint32_t        seqNum;
    int             socketId;
    pthread_t 		threadId;
} SocketAndSeqNum_t;

static 		pthread_t 	frameSenderThreadArray[TEST_MAX_THREADS];
static 		in_port_t	port;

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
"Usage: %s [-v|-x] nbrFrames nbrRuns runAndWait snooze port \n"
"where:\n"
"   -v          Log through level INFO.\n"
"   -x          Log through level DEBUG. Too much information.\n"
"   nbrFrames	Number of frames to send per run.\n"
"   nbrRuns		Number of runs.\n"
"   runAndWait	Snooze time between 2 runs.\n"
"   snooze		Snooze time between 2 frames sent.\n"
"   port  		Server's port <port> that the blender uses to connect.\n"
"\n",
        progName, PACKAGE_VERSION, copyright, progName);

    exit(1);
}



/**
 * Decodes the command-line.
 *
 * @param[in]  argc           Number of arguments.
 * @param[in]  argv           Arguments.
 */
static void
decodeCommandLine(
        int     const          			argc,
        char* const*  const restrict 	argv
        )
{
    extern int          optind;
    extern int          opterr;
    extern char*        optarg;
    extern int          optopt;
    int ch;
    opterr = 0;                         /* no error messages from getopt(3) */
    while ((ch = getopt(argc, argv, "vx")) != -1)
    {
        switch (ch) {
            case 'v':
                    printf("set verbose mode");
                break;
            case 'x':
                    printf("set debug mode");
                break;

            default:
                break;
        }
    }
    if(optind >= argc)
    	usage(argv[0], COPYRIGHT_NOTICE);

	// nbrFrames
    if( sscanf(argv[optind++], "%d", &nbrFrames) != 1)
		usage(argv[0], COPYRIGHT_NOTICE);

	if(optind >= argc)
		usage(argv[0], COPYRIGHT_NOTICE);

	// nbrRuns
    if( sscanf(argv[optind++], "%d", &nbrRuns) != 1)
		usage(argv[0], COPYRIGHT_NOTICE);

	if(optind >= argc)
		usage(argv[0], COPYRIGHT_NOTICE);

	// runAndWait
    if( sscanf(argv[optind++], "%" SCNu32, &waitBetweenRuns) != 1)
		usage(argv[0], COPYRIGHT_NOTICE);

	if(optind >= argc)
		usage(argv[0], COPYRIGHT_NOTICE);

	// snoozeTime
	if( sscanf(argv[optind++], "%" SCNu32 , &framesCadence) != 1)
		usage(argv[0], COPYRIGHT_NOTICE);

	if(optind >= argc)
		usage(argv[0], COPYRIGHT_NOTICE);

	// port
	if( sscanf(argv[optind], "%" SCNu16, &port) != 1)
		usage(argv[0], COPYRIGHT_NOTICE);
}



// Build the i-th frame
// i is used to set the sequence number and can be any positive integer 
void buildFrameI(uint32_t sequence, unsigned char *frame, uint16_t run, int clientSocket)
{

	// Build the frame: Frame Header and Product Header
	//==================================================

// A: Frame Header ===============================================================

	// byte[0]: HDLC address:
	frame[0]	= 255;

	// 7 bytes: 1 byte at a time
	for (int skipBit=1; skipBit <=7; skipBit++) 
	{
		frame[skipBit] = (unsigned char) 100;	// any value does it: it can be random too
	}

	// SBN sequence number: byte [8-11]
	// sequence: 1000, 1001, 1002, ...
	// log_add("Sequence Number: %lu\n",  sequence);
	*(uint32_t*)(frame+8) = (uint32_t) htonl(sequence); 

	
	//sequence = (uint32_t) ntohl(*(uint32_t*)(frame+8)); 
	//log_add("---------->received Sequence: %lu\n",  sequence);
	
	// SBN run number: byte [12-13]
	*(uint16_t*)(frame+12) = (uint16_t) htons(run); 
	
	// SBN checksum: on 2 bytes = unsigned sum of bytes 0 to 13
	uint16_t sum =0;
	for (int byteIndex = 0; byteIndex<14; byteIndex++)
	{
		sum += (unsigned char) frame[byteIndex];
	}

	//log_add("checksum: %u \n", sum);
	*(uint16_t*)(frame+14) = (uint16_t) htons(sum); 
	


// B: Product Header ===============================================================

    // skip byte: 16  --> version number 
    // skip byte: 17  --> transfer type
	for (int skipBit=16; skipBit <=17; skipBit++) 
	{
		frame[skipBit] = (unsigned char) 100;	// any value does it: it can be random too
	}

	// header length: [18-19]
	// total length of product header in bytes for this frame, including options
	uint16_t headerLength = 16;  // 16 bytes, when NO options exist 
	//log_add("Product Header Length: %lu\n",  headerLength);
	*(uint16_t*)(frame+18) = (uint16_t) htons(headerLength); 

	// dummy block number: [20-21]
	uint16_t blockNumber = 1001;
	*(uint16_t*)(frame+20) = (uint16_t) htons(blockNumber); 


	// Data  Block Offset: [22-23]
	uint16_t dataBlockOffset = 0;
	//log_add("Data Block Offset: %lu\n",  dataBlockOffset);
	*(uint16_t*)(frame+22) = (uint16_t) htons(dataBlockOffset); 


	// Data  Block Size: [24-25]
	uint16_t dataBlockSize = 3000;
	// If block size > 4000 ==> scream (assert!)
	assert(dataBlockSize < SBN_DATA_BLOCK_SIZE);

	//log_add("Data Block Size: %lu\n",  dataBlockSize);
	*(uint16_t*)(frame+24) = (uint16_t) htons(dataBlockSize);

	// beginning of data block: start at product-defi header: 
	// frame size: 16 + prodDefHeader.headerLength + prodDefHeader.dataBlockOffset + prodDefHeader.dataBlockSize 
	// data bloc Size: prodDefHeader.dataBlockSize

// C: Frame Data ===============================================================

    int begin 	= 16 + headerLength + dataBlockOffset;
  
    // init to a dummy data: (0xBD is 189, 0x64 is 100)
    memset(frame+begin, 0x64, dataBlockSize);

}


static void*
sendFramesToBlender(void* arg)
{
	int clientSocket = (int) arg;

	unsigned char 	frame[SBN_FRAME_SIZE] 	= {};

    // Compute frame rate:
	uint32_t 	sequenceNum 				= 0;
	int 		numberOfFramesSent 			= 0;
    int 		max_connections_to_receive 	= 0;

	srandom(0);
	int lowerLimit = 0, upperLimit = 50;
	// make these as arguments from CLI alongside NUMBER_OF_FRAMES, NUMBER_OF_RUNS, snooze time before, on blender side: hashTable capacity
	/*
	 int nbrFrames, nbrRuns, snoozeTime (upperLimit)
	 */
	for (int r=0; r < nbrRuns; r++)
	{
		// We simulate a socat server sending frames to the blender which acts as a client
		// frames are constructed here for testing purposes
		for (int s=0; s < nbrFrames; s++)
		{
			// build the s-th frame
			(void) buildFrameI(s, frame, r, clientSocket);

			// sleep between frames sent
			struct timespec duration = {
					.tv_sec 	= framesCadence / 1000000,	// e.g. 1 uSec.
					.tv_nsec 	= (framesCadence % 1000000) * 1000		// nSec
			};
			nanosleep( &duration, NULL);

			log_info(" --> testBlender sent frame: seqNum: %u, run: %u) to blender. \n", s, r) ;

			int written = write(clientSocket, (const char *)frame, sizeof(frame));
			if(written != sizeof(frame))
			{
				log_add("Write failed...\n");
				log_flush_error();
				exit(EXIT_FAILURE);
			}


			log_add("Number of frames Sent: %d, Run#: %" SCNu16 "\n", ++numberOfFramesSent, r);
			log_flush_info();
		} // for frames

		// sleep between run switches
		struct timespec duration = {
				.tv_sec 	= waitBetweenRuns / 1000000,
				.tv_nsec 	= (waitBetweenRuns % 1000000) * 1000
		};
		nanosleep( &duration, NULL);

	} // for runs

	return NULL;
}

static void
start_newThread(int threadNum, int clientSocket)
{
	// create a thread
	if(pthread_create(  &frameSenderThreadArray[threadNum],
			NULL, sendFramesToBlender, (void*) clientSocket) < 0)
	{
		log_add("testBlender(): Could not create a thread!\n");
		log_flush_error();
	}

	if( pthread_detach(frameSenderThreadArray[threadNum]) )
	{
		log_add("Could not detach the created thread!\n");
		close(clientSocket);
		log_flush_fatal();
		exit(EXIT_FAILURE);
	}
}

// Driver code
int main(int argc, char** argv)
{
	char buffer[100];

	int len, status;
	int serverSockfd, clientSocket;

    /*
     * Initialize logging. Done first in case something happens that needs to
     * be reported.
     */
    const char* const progname = basename(argv[0]);

    if (log_init(progname))
    {
        log_syserr("Couldn't initialize logging module");
        exit(EXIT_FAILURE);
    }//======================================

    (void)log_set_level(LOG_LEVEL_WARNING);
    //(void)log_set_level(LOG_LEVEL_INFO);


    (void) decodeCommandLine(argc, argv);

    log_info("NB_FRAMES_PER_RUN: %d, NB_RUNS: %d, TimeWaitBetRuns: % "PRIu32", snoozeTime: % "PRIu32" usec", nbrFrames, nbrRuns, waitBetweenRuns, framesCadence);
	struct sockaddr_in 
	  			servaddr= { .sin_family      = AF_INET, 
                            .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
                            .sin_port        = htons(port)
                          }, 
                cliaddr;
    
//	servaddr.sin_addr.s_addr = inet_pton(hostId);

    // Creating socket file descriptor
    if ( (serverSockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
		log_add("socket creation failed!\n");
		log_flush_error();
        exit(EXIT_FAILURE);
    }

    int on = 1;
    if( setsockopt(serverSockfd, SOL_SOCKET, SO_REUSEADDR, (char *) &on, sizeof(on)))
    {
        close(serverSockfd);
   		log_add("setsockopt() failed!\n");
   		log_flush_error();
           exit(EXIT_FAILURE);
    }

	// bind server address to socket descriptor
    if (bind(serverSockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) )
    {
        close(serverSockfd);
		log_add("socket bind failed!\n");
		log_flush_error();
        exit(EXIT_FAILURE);
    }

    //  listen to All blender clients
    listen(serverSockfd, MAX_CLIENTS);

    // accept new client connection in its own thread
    int c = sizeof(struct sockaddr_in);

    log_info("testBlender (socat): simulating 'listening to incoming TCP connections from NOAAPORT socat' ...");
    log_info("testBlender (socat): \t Build the frames here and send them to listening client (the blender)' ...\n");

	int connectionsAccepted = 0;
	for (;;)
	{
		log_add("accept(): blocking on incoming client requests\n");
		log_flush_info();
		clientSocket = accept(serverSockfd, (struct sockaddr *)&cliaddr, (socklen_t *) &c);
		if(clientSocket < 0)
		{
			log_add("\t-> testBlender (socat): Client connection NOT accepted!\n");
			log_flush_warning();
			exit(EXIT_FAILURE);
			//sleep(1);
			//continue;
		}

		log_add("\t-> testBlender (socat): Client connection (from blender) accepted!\n");
		log_add("\t   (Each connection will be used in its own thread)\n");
		log_flush_info();
		start_newThread(connectionsAccepted++, clientSocket);

    } // for



	return 0;
}
