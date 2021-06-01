// Client side implementation of UDP client-server model
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

#define PORT	 		8080
#define MAXLINE 		1024
#define	SAMPLE_SIZE 	1000
#define	SBN_FRAME_SIZE 	4000

// Build the i-th frame
// i is used to set the sequence number and can be any positive integer 
void buildFrameI(int i, unsigned char *frame, uint16_t run)
{
	// Build the frame:

	// byte[0]: HDLC address:
	frame[0]	= 255;

	// 7 bytes: 1 byte at a time
	for (int skipBit=1; skipBit <=7; skipBit++) 
	{
		frame[skipBit] = (unsigned char) 100;	// any value does it: it can be random too
	}

	// SBN sequence number: byte [8-11]
	// sequence: 1000, 1001, 1002, ...

    // assume a random frame is received (instead of buffer)

    // uint32_t sequence = rand() % 10;

	uint32_t sequence 	= 1000 + i;
	printf("Sequence: %lu\n",  sequence);
	*(uint32_t*)(frame+8) = (uint32_t) htonl(sequence); 

	// receiving
	//sequence = (uint32_t) ntohl(*(uint32_t*)(frame+8)); 
	//printf("----------> Sequence: %lu\n",  sequence);
	

	// SBN run number: byte [12-13]
	*(uint16_t*)(frame+12) = (uint16_t) htons(run); 
	printf("run: %u\n", run);


	// SBN checksum: on 2 bytes = unsigned sum of bytes 0 to 13
	uint16_t sum =0;
	for (int byteIndex = 0; byteIndex<14; byteIndex++)
	{
		sum += (unsigned char) frame[byteIndex];
	}
	printf("checksum: %d\n", sum);
	*(uint16_t*)(frame+14) = (uint16_t) htons(sum); 
	

}

int main(int argc, char** argv) {
	
	int sockfd;
	unsigned char buffer[MAXLINE];
	struct sockaddr_in	 servaddr;

	int n;
	
    char * line = NULL;
    char * buf[10000];
    size_t len = 0;
    ssize_t read;

	srand(time(NULL));

	memset(&servaddr, 0, sizeof(servaddr));
	
	// Filling server information
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(PORT);
	servaddr.sin_addr.s_addr = INADDR_ANY;

	// Creating socket file descriptor
	if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

    // Build the frame:
    unsigned char frame[SBN_FRAME_SIZE] = {};
    uint16_t run = 435;
    //for (int s=0; s< SAMPLE_SIZE; s++)
    //for (int s=0; ; s++)
    for (int s=0; s < 21; s++)
    {
    	// simulate a run# change:
    	if( s == 10) run = 436;


    	// build the s-th frame
    	(void) buildFrameI(s, frame, run);

    	
	    printf("\n\t--> Client: sent %d-th frame/checksum [14][15]to server: %x \n", s, (uint16_t*) (frame+14)) ;

		if( sendto(sockfd, (const char *)frame, sizeof(frame), 0, 
					(const struct sockaddr *) &servaddr,
					 sizeof(servaddr)) < 0)
	    {
	         perror("sendto()");
	         exit(1);
	    }


    }

	close(sockfd);
	return 0;
}
