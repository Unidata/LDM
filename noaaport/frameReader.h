/*
 * frameReader.h
 *
 *  Created on: Aug 27, 2021
 *      Author: Mustapha Iles
 */

#ifndef FRAMEREADER_H_
#define FRAMEREADER_H_

#include <arpa/inet.h>

#define FIN                         0
#define MAX_SEQ_NUM                 UINT32_MAX      // (~(uint32_t)0)  // not used
#define MAX_SERVERS					20				// hosts to connect to
#define MAX_HOST_LEN				40				// hosts length

typedef struct sockaddr_in SOCK4ADDR;
typedef struct frameReader {
	int 		policy;
    char** 		serverAddresses;	// host:port list
    int 		serverCount;		// hosts count
    int 		frameSize;

} FrameReaderConf_t;

typedef struct socketFd {
	int 		socketFd;
} SocketFd_t;

#endif /* FRAMEREADER_H_ */
