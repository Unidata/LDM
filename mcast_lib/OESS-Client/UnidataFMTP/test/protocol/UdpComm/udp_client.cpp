#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "UdpComm.h"
#include <unistd.h>

int main(int argc, char* argv[])
{
    int sock;
    char sendbuf[] = "Potomac says hello!";
    char recvbuf[8192];
    struct sockaddr_in serv_addr;
	bzero(&serv_addr, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr("128.143.137.117");
    serv_addr.sin_port = htons(5000);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        printf("create socket failed.\n");
    }
    socklen_t serv_size = sizeof(serv_addr);
    while(1)
    {
        sendto(sock, sendbuf, sizeof(sendbuf), 0, (struct sockaddr *) &serv_addr, serv_size);
        recvfrom(sock, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *) &serv_addr, &serv_size);
        std::cout << recvbuf << std::endl;
        sleep(1);
    }
    return 0;
}
