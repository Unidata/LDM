/**
 * Prints TCP properties.
 *
 * tcp_prop.c
 *
 *  Created on: Oct 11, 2019
 *      Author: steve
 */

#include "config.h"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/socket.h>

int main(int argc, char** argv)
{
	int status;
	int sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (sd == -1) {
		perror("socket() failure");
		status = 1;
	}
	else {
		int       intVal;
		socklen_t intLen = sizeof(int);

		status = getsockopt(sd, SOL_SOCKET, SO_RCVBUF, &intVal, &intLen);

		if (status == -1) {
			perror("SO_RCVBUF failure");
			status = 1;
		}
		else {
			printf("SO_RCVBUF: %d\n", intVal);

			status = getsockopt(sd, IPPROTO_TCP, TCP_MAXSEG, &intVal, &intLen);

			if (status == -1) {
				perror("TCP_MAXSEG failure");
				status = 1;
			}
			else {
				printf("TCP_MAXSEG: %d\n", intVal);
			}

		}
	}

	return status;
}
