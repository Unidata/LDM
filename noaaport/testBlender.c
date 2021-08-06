// server program for TCP connection
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <inttypes.h>
#include <time.h>
#include <assert.h>

#define PORT            9127
#define MAXLINE 		1024
#define	SAMPLE_SIZE 	1000
#define	SBN_FRAME_SIZE 	4000
#define	SBN_DATA_BLOCK_SIZE 	5000

#define MAX_FRAMES_PER_SEC	3500	// This comes from plot in rstat on oliver for CONDUIT
#define MAX_CLIENTS 		1

// Build the i-th frame
// i is used to set the sequence number and can be any positive integer 
void buildFrameI(uint32_t sequence, unsigned char *frame, uint16_t run)
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
	// printf("Sequence Number: %lu\n",  sequence);
	*(uint32_t*)(frame+8) = (uint32_t) htonl(sequence); 

	
	//sequence = (uint32_t) ntohl(*(uint32_t*)(frame+8)); 
	//printf("---------->received Sequence: %lu\n",  sequence);
	
	// SBN run number: byte [12-13]
	*(uint16_t*)(frame+12) = (uint16_t) htons(run); 
	
	// SBN checksum: on 2 bytes = unsigned sum of bytes 0 to 13
	uint16_t sum =0;
	for (int byteIndex = 0; byteIndex<14; byteIndex++)
	{
		sum += (unsigned char) frame[byteIndex];
	}

	//printf("checksum: %u \n", sum);
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
	//printf("Product Header Length: %lu\n",  headerLength);
	*(uint16_t*)(frame+18) = (uint16_t) htons(headerLength); 

	// dummy block number: [20-21]
	uint16_t blockNumber = 1001;
	*(uint16_t*)(frame+20) = (uint16_t) htons(blockNumber); 


	// Data  Block Offset: [22-23]
	uint16_t dataBlockOffset = 0;
	//printf("Data Block Offset: %lu\n",  dataBlockOffset);
	*(uint16_t*)(frame+22) = (uint16_t) htons(dataBlockOffset); 


	// Data  Block Size: [24-25]
	uint16_t dataBlockSize = 3000;
	// If block size > 4000 ==> scream (assert!)
	assert(dataBlockSize < SBN_DATA_BLOCK_SIZE);

	//printf("Data Block Size: %lu\n",  dataBlockSize);
	*(uint16_t*)(frame+24) = (uint16_t) htons(dataBlockSize);

	// beginning of data block: start at product-defi header: 
	// frame size: 16 + prodDefHeader.headerLength + prodDefHeader.dataBlockOffset + prodDefHeader.dataBlockSize 
	// data bloc Size: prodDefHeader.dataBlockSize

// C: Frame Data ===============================================================

    int begin 	= 16 + headerLength + dataBlockOffset;
  
    // init to a dummy data: (0xBD is 189, 0x64 is 100)
    memset(frame+begin, 0x64, dataBlockSize);	

}
// Driver code
int main()
{
	char buffer[100];

	int len;
	int serverSockfd, clientSocket;

//======================================

	struct sockaddr_in 
	  			servaddr= { .sin_family       = AF_INET, 
                          .sin_addr.s_addr  = htonl(INADDR_ANY),  //INADDR_LOOPBACK), 
                          .sin_port         = htons(PORT)
                        }, 
                        cliaddr;
    
    // Creating socket file descriptor
    if ( (serverSockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        perror("socket creation failed!");
        exit(EXIT_FAILURE);
    }


	// bind server address to socket descriptor
    if (bind(serverSockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) )
    {
        close(serverSockfd);
        perror("socket bind failed!");
        exit(EXIT_FAILURE);
    }

    //  listen to All blender clients: 1 for now
    listen(serverSockfd, MAX_CLIENTS);


/* Infinite server loop */  
    // accept new client connection in its own thread
    int c = sizeof(struct sockaddr_in);


    // Compute frame rate:
    
    float frameRate 	= 1 / MAX_FRAMES_PER_SEC;

	int numberOfFramesSent = 0;
    int max_connections_to_receive = 0;
    printf("\nTCPServer: simulating 'listening to incoming TCP connections from NOAAPORT socat' ...\n\n");
    printf("\nTCPServer: \t Build the frames here and send them to listening client (the blender_clnt)' ...\n\n");

    unsigned char frame[SBN_FRAME_SIZE] = {};
    uint32_t sequenceNum = 0;
    while(1)
    {
    	uint16_t run = 435;
    	

   	   	printf("accept(): blocking on incoming client requests");
        clientSocket = accept(serverSockfd, (struct sockaddr *)&cliaddr, (socklen_t *) &c);
        if(clientSocket < 0) 
        {
            continue;
        }
        
        printf("\t-> TCPServer: Client connection (from blender_clnt) accepted!\n");


	    //for (int s=0; s< SAMPLE_SIZE; s++)
	    //for (int s=0; ; s++)
	    for (int s=0; s < 31; s++)
	    {

	        // We simulate a socat server sending frames to the blender which acts as a client
			// frames are constructed here for testing purposes      

	//=================================== the frame data is simulated here ========================

	    	// simulate a run# change every 10 frames:
	    	/*
	    	if( !( (s) %3) ) 
			{
				run++;
				sequenceNum = 0;	// reset after run# change
				printf("\nNew run#: %d   -- resetting seq Num to %d\n", run, sequenceNum);
				sleep(1);
			}
			*/

	    	// build the s-th frame
	    	(void) buildFrameI(sequenceNum, frame, run);

	    	uint16_t chksum = *(uint16_t*)(frame+14);
		    printf("\n --> TCPServer sent %d-th frame: seqNum: %u, run: %u) to blender_clnt.\n", 
		    	s, sequenceNum, run) ;


			int written = write(clientSocket, (const char *)frame, sizeof(frame));
			if(written != sizeof(frame))
		    {
		    	printf("Write failed...\n");
		    	perror("Error");
		    	exit(0);
		    }

		    frameRate = 0.2;
		    // printf("Sleeping for %f\n", frameRate);
		    //sleep(frameRate);
		    const struct timespec t = {.tv_sec=frameRate, .tv_nsec=0};
		    //nanosleep(&t, NULL);

			printf("Continuing...\n");
			
			numberOfFramesSent++;
		    ++sequenceNum;

		} // for

		close(clientSocket);

	} //while(1)
	printf("numberOfFramesSent: %d\n", numberOfFramesSent);
	return 0;
}
// ==================================================











/*

        ++totalTCPconnectionReceived;
        printf("Processing TCP client...received %d connection so far\n", 
            totalTCPconnectionReceived);
    
    } // for
}

*/