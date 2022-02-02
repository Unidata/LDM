#include "config.h"

#include "blender.h"
#include "frameReader.h"
#include "NbsFrame.h"
#include "noaaportFrame.h"
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
extern void	 	setFIFOPolicySetPriority(pthread_t, char*, int);
extern int   	tryInsertInQueue( uint32_t, uint16_t, const uint8_t*, uint16_t);
extern int 		nbs_logHeaders( const uint8_t* buf, size_t nbytes);
//  ========================================================================

/**
 * Utility function to read SBN actual data bytes from the connection
 *
 * @param[in]  buffer         Destination buffer for headers
 * @param[in]  clientSock	  Socket Id for this client reader
 * @param[in]  readByteStart  Offset of SBN data
 * @param[in]  dataBlockSize  Size of block of SBN data
 * @param[out] buffer  		  Buffer to contain SBN data read in
 *
 * @retval    totalBytesRead  Total bytes read
 * @retval    0        		  EOF
 * @retval    -1       		  Error
 * @retval    -2              Invalid frame
 */
static ssize_t
getProductHeaders(  uint8_t* const 	buffer,
					int 			clientSock,
                    uint16_t *		pDataBlockSize)
{
    uint8_t* cp = buffer;
    ssize_t  status = getBytes(clientSock, cp, 1);
    if (status != 1)
        return status;
    // Length of product-definition header in bytes
    const unsigned pdhLen = (buffer[0] & 0xf) * 4;
    ++cp;

    if (pdhLen < 16) {
        log_add("Product definition header is too small: %u bytes. "
                "Rest of frame wasn't read", pdhLen);
        return -2;
    }
    if (pdhLen > 16) {
        log_warning("Large product definition header: %u bytes", pdhLen);
    }

    status = getBytes(clientSock, cp, pdhLen-1);
    if( status <= 0)
    {
        if( status == 0) log_add("Client disconnected!");
        if( status <  0) log_add("read() failure");
        log_flush_warning();

        // clientSock gets closed in calling function
        return status;
    }
    cp += status;

    // Sum of product-definition and product-specific header lengths in bytes
    const unsigned pdhPshLen = ntohs(*(uint16_t*)(buffer+2));
    if (pdhPshLen < pdhLen) {
        log_add("Product definition header (%u bytes) is larger than itself "
                "plus product-specific header (%u bytes). "
                "Rest of frame wasn't read", pdhLen, pdhPshLen);
        return -2;
    }
    // Product-specific header length in bytes
    uint16_t pshLen = pdhPshLen - pdhLen;
    if (pdhLen > 16)
        log_notice("Product-specific header length is %u bytes");

    // Data Block Size: [8-9]
    *pDataBlockSize     = ntohs(*(uint16_t*)(buffer+8));

    /*
     * Transfer type:
     *    1 = Start of a new product
     *    2 = Product transfer still in progress
     *    4 = End (last packet) of this product
     *    8 = Product error
     *   32 = Product Abort
     *   64 = Option headers follow; e. g., product-specific header
     */
    const unsigned transferType = buffer[1];
    if (pshLen && ((transferType & 64) == 0)) {
        log_warning("Ignoring %u-byte product-specific header "
                "because associated transfer type bit is 0", pshLen);
    }
    else if (pshLen) {
        // Read product-specific header
        status = getBytes(clientSock, cp, pshLen);
        if( status <= 0)
        {
            if( status == 0) log_add("Client  disconnected!");
            if( status <  0) log_add("read() failure");
            log_flush_warning();

            // clientSock gets closed in calling function
            return status;
        }
        cp += status;
    }

    return cp - buffer;
}

/**
 * Utility function to read SBN actual data bytes from the connection
 *
 * @param[in]  clientSock	  Socket Id for this client reader
 * @param[in]  readByteStart  Offset of SBN data
 * @param[in]  dataBlockSize  Size of block of SBN data
 * @param[out] buffer  		  Buffer to contain SBN data read in

 * @retval    totalBytesRead  Total bytes read
 * @retval    -1       		  Error
 */
static int
readData(uint8_t* const buffer,
		 int 			clientSock,
		 uint16_t 		dataBlockSize)
{
    int totalBytesRead = getBytes(clientSock, buffer, dataBlockSize);

    if( totalBytesRead  <= 0 )
    {
        if( totalBytesRead == 0) log_add("Client  disconnected!");
        if( totalBytesRead <  0) log_add("read() failure");
        log_flush_warning();
        totalBytesRead = -1;
        //close(clientSock);
    }
    return totalBytesRead;
}

/**
 * Function to retrieve sequence number and the run number from the SBN structure buffer.
 * Does not return
 *
 * @param[in]  buffer		Incoming bytes
 * @param[out] pSeqNum  	Sequence number of this frame
 * @param[out] pRunNum 		Run number of this frame
 */
static void
getSeqNumRunNum( unsigned char* 	buffer,
				 uint32_t*      	pSeqNum,
				 uint16_t*      	pRunNum)
{
	// receiving: SBN 'sequence': [8-11]
	*pSeqNum = (uint32_t) ntohl(*(uint32_t*)(buffer+8));

	// receiving SBN 'run': [12-13]
	*pRunNum = (uint16_t) ntohs(*(uint16_t*) (buffer+12));
}

/**
 * Function to check the checksum in this SBN frame
 *
 * @param[in]  buffer			Incoming bytes
 * @retval     true  			CheckSum is invalid
 * @retval     false   		  	CheckSum is valid
 */
static bool
badCheckSum(unsigned char* buffer, int* invalidChkCounter)
{
	bool status;

	// receiving SBN 'checksum': [14-15]
	unsigned long checkSum =  (buffer[14] << 8) + buffer[15];
	//unsigned long checkSum =  (uint16_t) ntohs(*(uint16_t*) (buffer+14));

	// Compute SBN checksum on 2 bytes as an unsigned sum of bytes 0 to 13
	unsigned long sum = 0;
	for (int byteIndex = 0; byteIndex<14; ++byteIndex)
	{
		sum += buffer[byteIndex];
		/*
		log_debug("Buffer[%d] %lu, current sum: %lu (checkSum: %lu)",
				byteIndex, buffer[byteIndex], sum, checkSum);
		*/
	}

	status = (checkSum != sum);
	if( status ) ++(*invalidChkCounter);

	return status;

}

/**
 * Reads a NOAAPort frame into a buffer.
 *
 * @param[in]  clientSockFd  Socket descriptor
 * @param[in]  buffer        Frame buffer
 * @param[in]  bufSize       Size of frame buffer in bytes
 * @param[out] pFrameSize    Size of frame in bytes
 * @retval     0             Success
 * @retval     -1            System failure
 * @retval     -2            Frame is too large. Data wasn't read.
 */
static int
readFrame( 	int 			clientSockFd,
			uint8_t* const  buffer,
			const size_t    bufSize,
			uint16_t* const pFrameSize)
{
	uint8_t* cp = buffer + 16; // Skip frame header
	uint16_t dataBlockSize;
    int      ret = getProductHeaders(  cp, clientSockFd, &dataBlockSize);

    if(ret <= 0)
    {
        log_add("Error in retrieving product header.\n");
        log_flush_warning();
        return -1;
    }
    cp += ret;

    // Ensure that reading the data block will not overrun the buffer
    size_t nbytes = cp + dataBlockSize - buffer;
    if (nbytes > bufSize) {
        log_add("Frame is too large: %zu bytes. Data block won't be read",
                nbytes);
        return -2;
    }

    // Read the actual SBN frame data into 'buffer'
    ret = readData( cp, clientSockFd, dataBlockSize);
    if(ret == FIN || ret == -1)
    {
        log_debug("Error in reading data from socket. Closing socket...\n");
        log_add("Error in reading data from socket. Closing socket...\n");
        log_flush_warning();
        return -1;
    }
    cp += ret;
    *pFrameSize = cp - buffer;

    return SUCCESS;
}

/**
 * Processes a NOAAPort frame. Reads frames in and tries to insert them into the
 * queue.
 *
 * @param[in] clientSockFd  Socket descriptor
 * @param[in] buffer        Frame buffer
 * @param[in] bufSize       Size of frame buffer in bytes
 * @retval    0             Success
 * @retval    -1            System failure
 * @retval    -2            Frame is too large. Data wasn't read.
 */
static int
processFrame(int clientSockFd, unsigned char* buffer, const size_t bufSize)
{
	int 		status = SUCCESS;
	uint32_t 	sequenceNumber;
    uint16_t	runNumber;
    uint16_t 	frameSize; // Size of frame in bytes

    /*
     * Retrieve seqNum and runNum from buffer (already filled with 16-byte
     * frame header)
     */
	(void) getSeqNumRunNum( buffer, &sequenceNumber, &runNumber );


	// Read the rest of the frame into the buffer
	status = readFrame( clientSockFd, buffer, bufSize, &frameSize );
	if(status == -1)
	{
       	log_debug("readFrame() Failed!.");
		return status;
	}
	if(status == SUCCESS)
	{
		int status = nbs_logHeaders( buffer, frameSize);

		// buffer now contains frame data of size frameSize at offset 0
		// Insert in queue
		status = tryInsertInQueue(sequenceNumber, runNumber, buffer, frameSize);
	}
	return status;
}

/**
 * Function to read data bytes from the connection, rebuild the SBN frame,
 * and insert the data in a queue.
 * Never returns. Will terminate the process if a fatal error occurs.
 *
 * @param[in]  clientSockId  Socket Id for this client reader thread
 */
static void
buildFrameRoutine(int clientSockFd)
{
    log_notice("In buildFrameRoutine() waiting to read from "
    		"(fanout) server socket...\n");

    NbsReader* nbsReader = nbs_newReader(clientSockFd);

    const uint8_t* buf;
    size_t         size;
    const NbsFH*   fh;
    const NbsPDH*  pdh;
    const NbsPSH*  psh;

    for(;;)
    {
		int status  = NBS_SUCCESS;
		if( (status = nbs_getFrame( nbsReader, &buf, &size, &fh, &pdh, &psh ) )
				== NBS_SUCCESS)
		{
			//status = nbs_logHeaders( buf, size); // This just calls `log_add()`
			//log_flush_debug();

			// buffer now contains frame data of size frameSize at offset 0
			// Insert in queue
			status = tryInsertInQueue( fh->seqno, fh->runno, buf, size);
		}

		if( status == NBS_IO)
		{
			log_add_syserr("Read failure");
			// n == -1 ==> read error
		}
		if( status == NBS_EOF)
		{
			log_add("End of file");
		}

    } // for

    nbs_freeReader(nbsReader);

}

/**
 * Threaded function to initiate the frameReader running in its own thread.
 * Never returns. Will terminate the process if a fatal error occurs.
 *
 * @param[in]  id  String identifier of server's address and port number. E.g.,
 *                     <hostname>:<port>
 *                     <nnn.nnn.nnn.nnn>:<port>
 */
static void*
inputClientRoutine(void* id)
{
	const char* serverId = (char*) id;

    for(;;)
    {
		// Create a socket file descriptor for the blender/frameReader(s) client
		int socketClientFd = socket(AF_INET, SOCK_STREAM, 0);
		if(socketClientFd < 0)
		{
			log_add("socket creation failed\n");
			log_flush_fatal();
			exit(EXIT_FAILURE);
		}

		// id is host+port
		char*     hostId;
		in_port_t port;

		if( sscanf(serverId, "%m[^:]:%" SCNu16, &hostId, &port) != 2)
		{
			log_add("Invalid server specification %s\n", serverId);
			log_flush_fatal();
			exit(EXIT_FAILURE);
		}

		// The address family must be compatible with the local host

		struct addrinfo hints = {
		        .ai_flags = AI_ADDRCONFIG,
		        .ai_family = AF_INET };
		struct addrinfo* addrInfo;

		if( getaddrinfo(hostId, NULL, &hints, &addrInfo) !=0 )
		{
			log_add_syserr("getaddrinfo() failure on %s", hostId);
			log_flush_warning();
		}
		else {
            struct sockaddr_in sockaddr 	= *(struct sockaddr_in  * )
                    (addrInfo->ai_addr);
            sockaddr.sin_port 				= htons(port);

            log_info("Connecting to TCPServer server:  %s:%" PRIu16 "\n", hostId, port);

            if( connect(socketClientFd, (const struct sockaddr *) &sockaddr,
                    sizeof(sockaddr)) )
            {
                log_add("Error connecting to server %s: %s\n", serverId,
                        strerror(errno));
                log_flush_warning();
            }
            else {
                log_notice("CONNECTED!");

                buildFrameRoutine(socketClientFd);
                log_add("Lost connection with fanout server. Will retry after 60 sec. (%s:%" PRIu16 ")", hostId, port);
                log_flush_warning();
            } // Connected

            freeaddrinfo(addrInfo);
            free(hostId);
		} // Got address information

        close(socketClientFd);
        sleep(60);
    } // for

	return 0;
}


/**
 * Function to create client reader threads. As many threads as there are hosts.
 *
 * @param[in]  serverAddresses	List of hosts
 * @param[in]  serverCount  	Number of hosts
 *
 * @retval    0  				Success
 * @retval    -1       		  	Error
 */

int
reader_start( char* const* serverAddresses, int serverCount )
{
	if(serverCount > MAX_SERVERS || !serverCount)
	{
		log_error("Too many servers (max. handled: %d) OR "
				"none provided (serverCount: %d).", MAX_SERVERS, serverCount);
		return -1;
	}
	for(int i=0; i< serverCount; ++i)
	{
		pthread_t inputClientThread;
		log_notice("Server to connect to: %s\n", serverAddresses[i]);

		const char* id = serverAddresses[i]; // host+port
		if(pthread_create(  &inputClientThread, NULL, inputClientRoutine,
							(void*) id) < 0)
	    {
	        log_add("Could not create a thread for inputClient()!\n");
	        log_flush_error();
	    }
	    setFIFOPolicySetPriority(inputClientThread, "inputClientThread", 1);

		if( pthread_detach(inputClientThread) )
		{
			log_add("Could not detach the created thread!\n");
			log_flush_fatal();
			exit(EXIT_FAILURE);
		}
	}
	return 0;
}
