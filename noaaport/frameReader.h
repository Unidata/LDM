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

typedef struct sockaddr_in SOCK4ADDR;
typedef struct frameReader {
	int 		policy;
    in_addr_t 	ipAddress;		// passed in network byte order
    in_port_t 	ipPort;  		// in host byte order
    int 		sd;  			// socket descriptor
    int 		frameSize;

} FrameReaderConf_t;

typedef struct socketFd {
	int 		socketFd;
} SocketFd_t;

#endif /* FRAMEREADER_H_ */
