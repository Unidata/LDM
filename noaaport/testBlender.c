#include "config.h"

// server program for TCP connection
#include <stdio.h>
#include <stdlib.h>
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
#include "globals.h"

#define NUMBER_FRAMES_TO_SEND 	15
#define TEST_MAX_THREADS 		2
#define PORT            		9127
#define	SBN_FRAME_SIZE 			4000
#define	SBN_DATA_BLOCK_SIZE 	5000
#define MAX_FRAMES_PER_SEC		3500	// This comes from plot in rstat on oliver for CONDUIT
#define MAX_CLIENTS 			1
#define RUN_THRESHOLD           4

typedef struct SocketAndSeqNum {
    uint32_t        seqNum;
    int             socketId;
    pthread_t 		threadId;
} SocketAndSeqNum_t;


static void
sendFramesToBlender(int, uint32_t, int);


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


static void *
sendFramesToBlenderRoutine(void *clntSocketAndSeqNum)
{
	SocketAndSeqNum_t *ssNum = (SocketAndSeqNum_t *) clntSocketAndSeqNum;

    int 		clientSock 		= (ptrdiff_t) ssNum->socketId;
    uint32_t 	sequenceNumber 	= (uint32_t)  ssNum->seqNum;
    pthread_t   threadId 		= (pthread_t) ssNum->threadId;

	sendFramesToBlender(clientSock, sequenceNumber, (int)threadId); 
}

// Driver code
int main(int argc, char** argv)
{
	char buffer[100];

	int len;
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

	printf("Hello\n");
	log_notice("Hello\n");
	struct sockaddr_in 
	  			servaddr= { .sin_family      = AF_INET, 
                            .sin_addr.s_addr = htonl(INADDR_ANY),  //INADDR_LOOPBACK), 
                            .sin_port        = htons(PORT)
                          }, 
                        cliaddr;
    
    // Creating socket file descriptor
    if ( (serverSockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
		log_add("socket creation failed!\n");
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

    //  listen to All blender clients: 1 for now
    listen(serverSockfd, MAX_CLIENTS);


/* Infinite server loop */  
    // accept new client connection in its own thread
    int c = sizeof(struct sockaddr_in);


    // Compute frame rate:
    float 		frameRate 					= 1 / MAX_FRAMES_PER_SEC;
	uint32_t 	sequenceNum 				= 0;
	int 		numberOfFramesSent 			= 0;
    int 		max_connections_to_receive 	= 0;
    log_add("testBlender (socat): simulating 'listening to incoming TCP connections from NOAAPORT socat' ...\n\n");
    log_add("\ntestBlender (socat): \t Build the frames here and send them to listening client (the blender)' ...\n\n");
    log_flush_info();

	pthread_t frameSenderThreadArray[TEST_MAX_THREADS], aThreadId;
	SocketAndSeqNum_t socketIdAndSeqNum = { .socketId = 0, .seqNum = 0};

	for (int threadIndex = 0; threadIndex < TEST_MAX_THREADS; threadIndex++)
	{
		log_add("accept(thread: %d): blocking on incoming client requests", threadIndex);
		log_flush_info();
		clientSocket = accept(serverSockfd, (struct sockaddr *)&cliaddr, (socklen_t *) &c);
		if(clientSocket < 0)
		{
			log_add("\t-> testBlender (socat): Client connection NOT accepted!\n");
			log_flush_warning();
			sleep(1);
			continue;
		}

		log_add("\t-> testBlender (socat): Client connection (from blender) accepted!\n");
		log_flush_info();

		socketIdAndSeqNum.seqNum 	= threadIndex * NUMBER_FRAMES_TO_SEND;
		socketIdAndSeqNum.socketId 	= clientSocket;
		socketIdAndSeqNum.threadId 	= threadIndex;

		aThreadId = frameSenderThreadArray[threadIndex];
		if(pthread_create(&aThreadId,
			NULL,
			sendFramesToBlenderRoutine,
			(void *) &socketIdAndSeqNum) < 0)
		{
			log_add("Could not create a thread!\n");
			log_flush_error();
			close(clientSocket);

			exit(EXIT_FAILURE);
		}

		pthread_join(aThreadId, NULL);

		    // OR 
		/*
			if( pthread_detach(aThreadId) )
	    	{
	        	perror("Could not detach a newly created thread!\n");
	        	close(clientSocket);
	        
	        	exit(EXIT_FAILURE);            
	    	}
		 */
    } // for


	log_add("numberOfFramesSent: %d\n", numberOfFramesSent);
	log_flush_info();

	return 0;
}
// ==================================================

static void
sendFramesToBlender(int clientSocket, uint32_t sequenceNum, int threadId)
{

    	unsigned char 	frame[SBN_FRAME_SIZE] 	= {};
		uint16_t 		run 					= 0;

    	srandom(0);
    	int lowerLimit = 0, upperLimit = 50;   

	    for (int s=0; s < NUMBER_FRAMES_TO_SEND; s++)
	    {

	        // We simulate a socat server sending frames to the blender which acts as a client
			// frames are constructed here for testing purposes      

	//================== the frame data is simulated here =======

	    	// simulate a run# change every RUN_THRESHOLD (i.e. 60) frames:
	    	if( !( (s) % RUN_THRESHOLD) )
			{
				run++;
				sequenceNum = 0;	// reset after run# change
				log_add("\nNew run#: %d   -- resetting seq Num to %d\n", run, sequenceNum);
				log_flush_info();
				sleep(3);
			}

	    	// build the s-th frame
	    	(void) buildFrameI(sequenceNum, frame, run, clientSocket);

	    	// send it after x sec:
	    	float snooze =  lowerLimit + random() % (upperLimit - lowerLimit);

	    	//usleep( ( snooze / 100 ) * 1000000 );  // from 0 to 1/2 sec
	    	double frac;
	    	double sec = modf(snooze, &frac);
	    	struct timespec duration = {
	    			.tv_sec 	= (time_t) sec,
	    			.tv_nsec 	= (long) (frac * 1000000000)
	    	};
	    	nanosleep( &duration, NULL); // [0 - 1/10sec]
	//================== the frame data is simulated here =======

	    	//if(s % 100 == 0)
		    log_add("\n --> testBlender (thread# %d) sent %d-th frame: seqNum: %u, run: %u) to blender. \n",
		    		threadId, s, sequenceNum, run) ;
			log_flush_info();

			int written = write(clientSocket, (const char *)frame, sizeof(frame));
			if(written != sizeof(frame))
		    {
				log_add("Write failed...\n");
				log_flush_error();
		    	exit(EXIT_FAILURE);
		    }

			if(s % 20000 == 0)
			{
				log_add("Continuing... %d\n", s);
				log_flush_info();
			}
			
	//		numberOfFramesSent++;
		    ++sequenceNum;

		} // for
}

/*
        ++totalTCPconnectionReceived;
        log_add("Processing TCP client...received %d connection so far\n",
            totalTCPconnectionReceived);
    } // for
}
*/
