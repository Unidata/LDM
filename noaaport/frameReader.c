#include "config.h"

#include "blender.h"
#include "frameReader.h"
#include "noaaportFrame.h"
#include "InetSockAddr.h"
#include "globals.h"

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <log.h>

//  ========================================================================
extern void	 		setFIFOPolicySetPriority(pthread_t, char*, int);
extern int   		tryInsertInQueue( uint32_t, uint16_t, unsigned char*, uint16_t);
//  ========================================================================
static pthread_t	inputClientThread[MAX_SERVERS];
//  ========================================================================


static ssize_t
getBytes(int fd, char* buf, int nbytes)
{
    int nleft = nbytes;
    while (nleft > 0)
    {
        ssize_t n = read(fd, buf, nleft);
        //int n = recv(fd, (char *)buf,  nbytes , 0) ;
        if (n < 0 || n == 0)
            return n;
        buf += n;
        nleft -= n;
    }
    return nbytes;
}

static int
extractSeqNumRunCheckSum(unsigned char*  buffer,
						 uint32_t *pSequenceNumber,
						 uint16_t *pRun,
						 uint16_t *pCheckSum)
{
    int status = 1; // success

    // receiving: SBN 'sequence': [8-11]
    *pSequenceNumber = (uint32_t) ntohl(*(uint32_t*)(buffer+8));

    // receiving SBN 'run': [12-13]
    *pRun = (uint16_t) ntohs(*(uint16_t*) (buffer+12));

    // receiving SBN 'checksum': [14-15]
    *pCheckSum =  (uint16_t) ntohs(*(uint16_t*) (buffer+14));

    // Compute SBN checksum on 2 bytes as an unsigned sum of bytes 0 to 13
    uint16_t sum = 0;
    for (int byteIndex = 0; byteIndex<14; ++byteIndex)
    {
        sum += (unsigned char) buffer[byteIndex];
    }

    //printf("Checksum: %lu, - sum: %lu\n", *pCheckSum, sum);
    if( *pCheckSum != sum)
    {
        status = -2;
    }
//    printf("sum: %u - checksum: %u  - runningSum: %u\n", sum, *pCheckSum, runningSum);
    return status;
}

static int
retrieveFrameHeaderFields(unsigned char   *buffer,
                          int             clientSock,
                          uint32_t        *pSequenceNumber,
                          uint16_t        *pRun,
                          uint16_t        *pCheckSum)
{
    int status = 1;     // success
   	uint16_t runningSum = 255;

    // check on 255
    int totalBytesRead;
    if( (totalBytesRead = getBytes(clientSock, buffer+1, 15)) <= 0 )
    {
        if( totalBytesRead == 0) printf("Client  disconnected!");
        if( totalBytesRead <  0) perror("read() failure");

        // clientSock gets closed in calling function
        return totalBytesRead;
    }

    return extractSeqNumRunCheckSum(buffer, pSequenceNumber, pRun, pCheckSum);
}

static int
retrieveProductHeaderFields(unsigned char* buffer,
							int clientSock,
                            uint16_t *pHeaderLength,
                            uint16_t *pDataBlockOffset,
                            uint16_t *pDataBlockSize)
{
    int totalBytesRead = getBytes(clientSock, buffer+16, 10);
    if( totalBytesRead <= 0)
    {
        if( totalBytesRead == 0) log_add("Client  disconnected!");
        if( totalBytesRead <  0) log_add("read() failure");
        log_flush_warning();

        // clientSock gets closed in calling function
        return totalBytesRead;
    }

    // skip byte: 16  --> version number
    // skip byte: 17  --> transfer type

    // header length: [18-19]
    *pHeaderLength      = (uint16_t) ntohs(*(uint16_t*)(buffer+18));

    //printf("header length: %lu\n", *pHeaderLength);

    // skip bytes: [20-21] --> block number

    // data block offset: [22-23]
    *pDataBlockOffset   = (uint16_t) ntohs(*(uint16_t*)(buffer+22));
    //printf("Data Block Offset: %lu\n", *pDataBlockOffset);

    // Data Block Size: [24-25]
    *pDataBlockSize     = (uint16_t) ntohs(*(uint16_t*)(buffer+24));
    //printf("Data Block Size: %lu\n", *pDataBlockSize);

    return totalBytesRead;
}

static int
readFrameDataFromSocket(unsigned char* buffer,
						int clientSock,
						uint16_t readByteStart,
						uint16_t dataBlockSize)
{
    int totalBytesRead = getBytes(clientSock, buffer+readByteStart, dataBlockSize);

    if( totalBytesRead  <= 0 )
    {
        if( totalBytesRead == 0) log_add("Client  disconnected!");
        if( totalBytesRead <  0) log_add("read() failure");
        log_flush_warning();

        close(clientSock);
    }
    return totalBytesRead;
}

// to read a complete frame with its data.
static void *
buildFrameRoutine(void *arg)
{
    int clientSockFd    = (int) arg;

    unsigned char buffer[SBN_FRAME_SIZE] = {};

    uint16_t    checkSum;
    uint32_t    sequenceNumber;
    uint16_t    runNumber;
    time_t      epoch;

    int cancelState = PTHREAD_CANCEL_DISABLE;

    bool initialFrameRun_flag       = true;

    log_notice("In buildFrameRoutine()...\n");

    // TCP/IP receiver
    // loop until byte 255 is detected. And then process next 15 bytes
    for(;;)
    {
        int n = read(clientSockFd, (char *)buffer,  1 ) ;
        if( n <= 0 )
        {
            if( n <  0 ) log_syserr("InputClient thread: inputBuildFrameRoutine(): thread should die!");
            if( n == 0 ) log_syserr("InputClient thread: inputBuildFrameRoutine(): Client  disconnected!");
            close(clientSockFd);
            pthread_exit(NULL);
        }
        if(buffer[0] != 255)
        {
            continue;
        }

        // totalBytesRead may be > 15 bytes. buffer is guaranteed to contain at least 16 bytes
        int ret = retrieveFrameHeaderFields(  buffer, clientSockFd,
                                              &sequenceNumber, &runNumber,
                                              &checkSum);
        if(ret == FIN || ret == -1)
        {
            close(clientSockFd);
            pthread_exit(NULL);
        }

        if(ret == -2)
        {
            log_notice("retrieveFrameHeaderFields(): Checksum failed! (continue...)\n");
            continue;   // checksum failed
        }

        // Get product-header fields from (buffer+16 and on):
        // ===============================================
        uint16_t headerLength, totalBytesRead;
        uint16_t dataBlockOffset, dataBlockSize;
        ret = retrieveProductHeaderFields( buffer, clientSockFd,
                                            &headerLength, &dataBlockOffset, &dataBlockSize);

        if(ret == FIN || ret == -1)
        {
            log_add("Error in retrieving product header. Closing socket...\n");
            log_flush_warning();
            close(clientSockFd);
            pthread_exit(NULL);
        }

        // Where does the data start?
        // dataBlockOffset (2bytes) is offset in bytes where the data for this block
        //                           can be found relative to beginning of data block area.
        // headerLength (2bytes)    is total length of product header in bytes for this frame,
        //                           including options
        uint16_t dataBlockStart = 16 + headerLength + dataBlockOffset;
        uint16_t dataBlockEnd   = dataBlockStart + dataBlockSize;

        // Read frame data from entire 'buffer'
        ret = readFrameDataFromSocket( buffer, clientSockFd, dataBlockStart, dataBlockSize);
        if(ret == FIN || ret == -1)
        {
            log_add("Error in reading data from socket. Closing socket...\n");
            log_flush_warning();
            close(clientSockFd);
            pthread_exit(NULL);
        }

        // Store the relevant entire frame into its proper hashTable for this Run#:
        // Queue handles this task but hands it to the hashTableManager module

        tryInsertInQueue(sequenceNumber, runNumber, buffer, dataBlockEnd);

        // setcancelstate??? remove?
        pthread_setcancelstate(cancelState, &cancelState);
        //printf("\nContinue receiving..\n\n");

    } //for

    return NULL;
}

/**
 * Threaded function to initiate the frameReader running in its own thread
 *
 * @param[in]  frameReaderStruct  structure pointer that holds all
 *                                information for a given frameReader
 */
static void*
inputClientRoutine(void* id)
{
	const char* serverId = (char*) id;

    struct sched_param param;
	int policy	= SCHED_RR;
    int response 	= pthread_getschedparam(pthread_self(), &policy, &param);
    if( response )
    {
        log_add("get in inputClientRoutine()  : pthread_getschedparam() failure: %s\n", strerror(response));
        log_flush_fatal();
    }

	// Create a socket file descriptor for the blender/frameReader(s) client
    int socketClientFd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketClientFd < 0)
	{
		log_add("socket creation failed\n");
		log_flush_fatal();
		exit(EXIT_FAILURE);
	}

	// id is host+port
    char *hostId;
    in_port_t port;

    if( sscanf(serverId, "%m[^:]:%" SCNu16, &hostId, &port) != 2)
    {
    	log_add("Invalid server specification %s\n", serverId);
    	log_flush_fatal();
    	exit(EXIT_FAILURE);
    }

	// The address family must be compatible with the local host

	struct addrinfo hints = { .ai_flags = AI_ADDRCONFIG, .ai_family = AF_INET };
	struct addrinfo* addrInfo;

	// We consider dealing with an IP v4 or an IP v6 remote server.
	// argument 'ipV6Target' on command line sets the selection (hard coded for now)
	bool ipV6Target = false;
	if( ipV6Target )
	{
		hints.ai_family = AF_INET6;
	}

	if( getaddrinfo(hostId, NULL, &hints, &addrInfo) !=0 )
	{
		log_add_syserr("getaddrinfo() failure on %s", hostId);
		log_flush_fatal();
		exit(EXIT_FAILURE);
	}
    struct sockaddr_in sockaddr 	= *(struct sockaddr_in  * ) (addrInfo->ai_addr);
	sockaddr.sin_port 				= htons(port);
	//struct sockaddr_in6 sockaddr_v6 = *(struct sockaddr_in6 * ) (addrInfo->ai_addr);
	//sockaddr_v6.sin6_port 			= htons(port);

    freeaddrinfo(addrInfo);
    free(hostId);

    log_info("\nInputClientRoutine: connecting to TCPServer server to read frames...(PORT: , address: )\n");

	// accept new client connection in its own thread
	/*if( ipV6Target )
	{
		if( connect(socketClientFd, (const struct sockaddr *) &sockaddr_v6, sizeof(sockaddr_v6)) )
		{
			log_add("Error connecting to server %s: %s\n", serverId, strerror(errno));
			log_flush_fatal();
			exit(EXIT_FAILURE);
		}
	}*/
	if( connect(socketClientFd, (const struct sockaddr *) &sockaddr, sizeof(sockaddr)) )
	{
		log_add("Error connecting to server %s: %s\n", serverId, strerror(errno));
		log_flush_fatal();
		exit(EXIT_FAILURE);
	}
	log_notice("InputClientRoutine: CONNECTED!");

	// inputBuildFrameRoutine thread shall read one frame at a time from the server
	// and pushes it to the frameFifoAdapter function for proper handling
	pthread_t inputFrameThread;
	if(pthread_create(&inputFrameThread, NULL, buildFrameRoutine, (void *) socketClientFd) < 0)
	{
		log_add("Could not create a thread!\n");
		close(socketClientFd);
		log_flush_fatal();
		exit(EXIT_FAILURE);
	}

	if( pthread_detach(inputFrameThread) )
	{
		log_add("Could not detach the created thread!\n");
		close(socketClientFd);
		log_flush_fatal();
		exit(EXIT_FAILURE);
	}
}

int
reader_start( char* const* serverAddresses, int serverCount )
{
	if(serverCount > MAX_SERVERS || !serverCount)
	{
		log_error("Too many servers");
		return -1;
	}
	for(int i=0; i< serverCount; ++i)
	{
		log_notice("Server to connect to: %s\n", serverAddresses[i]);

		const char* id = serverAddresses[i]; // host+port
		if(pthread_create(  &inputClientThread[i], NULL, inputClientRoutine, (void*) id) < 0)
	    {
	        log_add("Could not create a thread!\n");
	        log_flush_error();
	    }
	    setFIFOPolicySetPriority(inputClientThread[i], "inputClientThread", 1);
	}
	return 0;
}


/*
 *
 in_addr_t 	ipAddress,	// in network byte order
 in_port_t 	ipPort,		// in host byte order

	aReaderConfig->ipAddress 	= ipAddress;
	aReaderConfig->ipPort 		= ipPort;
i*/

