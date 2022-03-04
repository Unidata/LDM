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
extern int      rcvBufSize;
//  ========================================================================

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

    const uint8_t* 	buf;
    size_t         	size;
    const NbsFH*   	fh;
    const NbsPDH*  	pdh;
    const NbsPSH*  	psh;
    log_level_t 	level;

	int status;
    for(;;)
    {
		if ((status = nbs_getFrame( nbsReader, &buf, &size, &fh, &pdh, &psh ))
				== NBS_SUCCESS)
		{
			// buffer now contains frame data of size frameSize at offset 0
			// Insert in queue
			status = tryInsertInQueue( fh->seqno, fh->runno, buf, size);

			if (status == 0) {
			    if (pdh && (pdh->transferType & 1))
                {
                    if (psh )
                        log_info("Starting product {SeqNum=%u, RunNum=%u, Cat=%u, prodCode=%u, Type=%u}" ,
                                fh->seqno, fh->runno, psh->category, psh->prodCode, psh->type);
                    else
                        log_info("Starting product {SeqNum=%u, RunNum=%u}" ,
                                fh->seqno, fh->runno);
                }
			}
			else if (status == 1) {
			    log_add("Frame was too late");
			    log_flush_debug();
			}
			else if (status == 2) {
			    log_add("Frame is a duplicate");
			    log_flush_debug();
			}
			else {
			    break;
			}
		}
		else if( status == NBS_IO)
		{
			log_add_syserr("Read failure");
			// n == -1 ==> read error
		}
		else if( status == NBS_EOF)
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

		if( rcvBufSize > 0 )
		{
			int rc = setsockopt(socketClientFd, SOL_SOCKET, SO_RCVBUF,
			                	&rcvBufSize, sizeof(int));
			if(rc != 0 )
				log_warning("Could not set receive buffer to %d bytes", rcvBufSize);
		}
		int optval;
		int optlen = sizeof(optval);
		int rc = getsockopt(socketClientFd, SOL_SOCKET, SO_RCVBUF,
							&optval, &optlen);
		if(rc == 0 )
			log_notice("Current receive buffer: %d bytes", optval);
		else
			log_syserr("Could not get receive buffer size");

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
