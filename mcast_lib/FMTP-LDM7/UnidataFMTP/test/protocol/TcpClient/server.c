#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>

/****************************************************************************
 * Function Name: errorHandler()
 *
 * Description: This function is called when a system call fails. It displays
 * a message about the error on stderr and then aborts the program.
 * 
 * Input: *msg    Error information or log
 * Output: none
 ***************************************************************************/
void errorHandler(const char *msg)
{
    perror(msg);
    exit(1);
}

/****************************************************************************
 * Function Name: main()
 *
 * Description: Open a socket on server side, bind address and port on the
 * socket and then listen on it. Print the received message on stdout.
 * 
 * Input: argc, *argv[]    Argument lists.
 * Output: none
 ****************************************************************************/
int main(int argc, char *argv[])
{
     // file descriptors
     int sockfd, newsockfd;
     // port number TCP socket to listen on
     int portno;
     // clilen stores the size of the address of the client
     socklen_t clilen;
     // incoming data buffer
     char buffer[256];
     // sockaddr_in contains the internet address, defined in netinet/in.h
     struct sockaddr_in serv_addr, cli_addr;
     // return value of read() and write() (e.g. number of characters read)
     int n;

     // check arguments first, make sure parameters are sufficient.
     // argv[0] should be server hostname or ip. argv[1] should be
     // port number that TCP socket will listen on.
     if (argc < 2) {
         fprintf(stderr,"ERROR, no port provided\n");
         exit(1);
     }

     // Open a socket, set network domain to AF_INET, socket type to stream,
     // If cannot open socket, call errorHandler. Creation fails with -1.
     sockfd = socket(AF_INET, SOCK_STREAM, 0);
     if (sockfd < 0) 
        errorHandler("ERROR opening socket");

     // clear serv_addr array
     bzero((char *) &serv_addr, sizeof(serv_addr));
     // convert port number from char to int.
     portno = atoi(argv[1]);
     // always set sin_family to AF_INET for net sockets
     serv_addr.sin_family = AF_INET;
     // listen on 0.0.0.0 for incoming connections
     serv_addr.sin_addr.s_addr = INADDR_ANY;
     // convert a port number in host order to network order
     serv_addr.sin_port = htons(portno);

     // bind serv_addr to the open socket
     if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) 
              errorHandler("ERROR on binding");
     // listen to assigned port for connections, allow 5 connections maximum
     listen(sockfd,5);
     clilen = sizeof(cli_addr);
     // wait for clients to connect to current socket
     // block the whole process until a connection is connected to the server
     newsockfd = accept(sockfd, 
                 (struct sockaddr *) &cli_addr, 
                 &clilen);
     // can't accept client's request
     if (newsockfd < 0) 
          errorHandler("ERROR on accept");

     while(1)
     {
	     // clear buffer
	     bzero(buffer,256);
	     // read from the socket and write to buffer
	     n = read(newsockfd,buffer,255);
	     if (n < 0) errorHandler("ERROR reading from socket");

             if(buffer[0] != '\0')
		     printf("Here is the message: %s\n", buffer);
	     // write to the socket
	     n = write(newsockfd,"message received by server.\n",28);
	     if (n < 0) errorHandler("ERROR writing to socket");
     }

     // close server side socket.
     close(newsockfd);
     close(sockfd);
     return 0; 
}
