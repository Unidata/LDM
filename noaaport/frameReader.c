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
#include <netinet/in.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <assert.h>
#include <log.h>

//  ========================================================================
extern void	 	setFIFOPolicySetPriority(pthread_t, char*, int);
extern int   	tryInsertInQueue( const NbsFH*, const NbsPDH*, const uint8_t*, uint16_t);
extern int      rcvBufSize;
//  ========================================================================

static void 	createThreadAndDetach(const char*);

/**
 * Returns an identifier for the remote endpoint of a socket.
 * @param[in]  sd      Socket
 * @param[out] peerId  An identifier for the remote endpoint of the socket
 * @param[in]  idLen   Size of the identifier buffer in bytes
 */
static void getPeerId(
        const int    sd,
        char*        peerId,
        const size_t idLen)
{
    assert(idLen);

    struct sockaddr_storage storage = {};
    socklen_t               storageLen = sizeof(storage);

    getpeername(sd, (struct sockaddr*)&storage, &storageLen);

    if (storage.ss_family == AF_INET) {
        inet_ntop(storage.ss_family, &((const struct sockaddr_in*)&storage)->sin_addr, peerId,
                idLen);
    }
    else if (storage.ss_family == AF_INET6) {
        inet_ntop(storage.ss_family, &((const struct sockaddr_in6*)&storage)->sin6_addr, peerId,
                idLen);
    }
    else {
        strncpy(peerId, "<not an IP address>", idLen);
    }

    peerId[idLen-1] = 0;
}

/**
 * Function to read data bytes from the connection, rebuild the SBN frame, and insert the data in a
 * queue.
 *
 * @param[in]  	clientSockId  Socket Id for this client reader thread
 * @retval      NBS_EOF       End-of-file. `log_add()` called.
 * @retval	  	NBS_IO        I/O failure. `log_add()` called.
 * @retval      NBS_SYSTEM    System failure. `log_add()` called.
 */
static int
buildFrameRoutine(int clientSockFd)
{
	int        status;
    NbsFH*     fh;
    NbsPDH*    pdh;
    uint8_t*   frame;
    size_t     frameSize;
    NbsReader* reader = nbs_new(clientSockFd);

    if (reader == NULL) {
        log_syserr("Couldn't allocate reader for socket %d", clientSockFd);
        status = NBS_SYSTEM;
    }
    else {
        char peerId[INET6_ADDRSTRLEN];
        getPeerId(clientSockFd, peerId, sizeof(peerId));

        log_debug("Waiting to read from (fanout) server socket...");

        for(;;)
        {
            if ((status = nbs_getFrame(reader, &frame, &frameSize, &fh, &pdh)) == NBS_SUCCESS)
            {
                // buffer now contains frame data
                if (fh->command == NBS_FH_CMD_DATA) {
                    // PDH exists. Insert data-transfer frame in queue
                    status = tryInsertInQueue( fh, pdh, frame, frameSize);

                    if (status == 0) {
                        if (pdh->transferType & 1) {
                            log_info("Starting product {fh->source=%u, fh->runno=%u, fh->seqno=%u, "
                                    "pdh->prodSeqNum=%u}", fh->source, fh->runno, fh->seqno,
                                    pdh->prodSeqNum);
                        }
                    }
                    else if (status == 1) {
                        log_warning("Frame from %s arrived too late to be included. Increase time "
                                "window?", peerId);
                    }
                    else if (status == 2) {
                        log_debug("Frame from %s is a duplicate", peerId);
                    }
                    else {
                        log_add("Couldn't add frame due to system failure", status);
                        status = NBS_SYSTEM;
                        break;
                    }
                }
                else if (fh->command != 5 && fh->command != 10) {
                    log_notice("Ignoring frame with command=%u", fh->command);
                }
            }
            else {
                if( status == NBS_IO)
                {
                    log_add("Read failure");
                    // n == -1 ==> read error
                }
                else if( status != NBS_EOF)
                {
                    log_add("Unknown return status from nbs_getFrame(): %d", status);
                }

                break;
            } // `status` != 0
        } // for loop

        nbs_free(reader);
    }

    return status;
}

/**
 * Threaded function to initiate the frameReader running in its own thread.
 * Never returns. Will terminate the thread if a fatal error occurs.
 *
 * @param[in]  id  String identifier of server's address and port number. E.g.,
 *                     <hostname>:<port>
 *                     <nnn.nnn.nnn.nnn>:<port>
 */
static void*
inputClientRoutine(void* id)
{
	const char* serverId = (char*) id;
	int socketClientFd;

    for(;;)
    {
		// Create a socket file descriptor for the blender/frameReader(s) client
		socketClientFd = socket(AF_INET, SOCK_STREAM, 0);
		if(socketClientFd < 0)
		{
			log_fatal("socket creation failed\n");
			exit(EXIT_FAILURE);
		}

		if( rcvBufSize > 0 )
		{
			int rc = setsockopt(socketClientFd, SOL_SOCKET, SO_RCVBUF, &rcvBufSize, sizeof(int));
			if(rc != 0 )
				log_warning("Could not set receive buffer to %d bytes", rcvBufSize);
		}
		int optval;
		int optlen = sizeof(optval);
		int rc = getsockopt(socketClientFd, SOL_SOCKET, SO_RCVBUF, &optval, &optlen);
		if(rc == 0 )
			log_notice("Current receive buffer: %d bytes", optval);
		else
			log_syserr("Could not get receive buffer size");

		// id is host+port
		char*     hostId;
		in_port_t port;

		if( sscanf(serverId, "%m[^:]:%" SCNu16, &hostId, &port) != 2)
		{
			log_fatal("Invalid fanout server specification %s\n", serverId);
			exit(EXIT_FAILURE);
		}

		// The address family must be compatible with the local host

		struct addrinfo hints = {
		        .ai_flags = AI_ADDRCONFIG,
		        .ai_family = AF_INET };
		struct addrinfo* addrInfo;

		if( getaddrinfo(hostId, NULL, &hints, &addrInfo) !=0 )
		{
			log_syserr("getaddrinfo() failure for %s", hostId);
		}
		else {
            struct sockaddr_in sockaddr = *(struct sockaddr_in  * ) (addrInfo->ai_addr);
            sockaddr.sin_port 			= htons(port);

            //log_info("Connecting to fanout server:  %s:%" PRIu16 "\n", hostId, port);

            if( connect(socketClientFd, (const struct sockaddr *) &sockaddr, sizeof(sockaddr)) )
            {
                log_syserr("Error connecting to fanout server %s: %s\n", serverId, strerror(errno));
            }
            else {
                log_notice("Connected to fanout server:  %s:%" PRIu16 "\n", hostId, port);
                int status = buildFrameRoutine(socketClientFd);
                if (status == NBS_SYSTEM) {
                    log_add("System failure on input thread");
                    log_flush_fatal();
                    exit(EXIT_FAILURE);
                }
                log_add("Lost connection with fanout server. Will retry after 60 s. "
                        "(%s:%" PRIu16 ")", hostId, port);
                log_flush_error();
            } // Connected

            freeaddrinfo(addrInfo);
            free(hostId);
		} // Got address information

        close(socketClientFd);
        sleep(60);
    } // for

    return NULL; // Accommodates Eclipse
}

static void
createThreadAndDetach(const char* hostId)
{
	pthread_t inputClientThread;
	log_notice("Server to connect to: %s", hostId);

	if(pthread_create(  &inputClientThread, NULL, inputClientRoutine,
						(void*) hostId) < 0)
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
		createThreadAndDetach(serverAddresses[i]);// host+port
	}
	return 0;
}
