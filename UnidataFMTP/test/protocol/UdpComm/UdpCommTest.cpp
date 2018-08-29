#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "UdpComm.h"
#include <unistd.h>

int main(int argc, char* argv[])
{
    char sendbuf[] = "Rivanna says hello!";
    char recvbuf[8192];
    struct sockaddr_in cli_addr;
	bzero(&cli_addr, sizeof(cli_addr));

    // test constructor
    UdpComm demoUDP(5000);
    // test SetSocketBufferSize()
    demoUDP.SetSocketBufferSize(8192);
    socklen_t cli_size = sizeof(cli_addr);
    while(1)
    {
        if( demoUDP.RecvFrom(recvbuf, sizeof(recvbuf), 0, (struct sockaddr *) &cli_addr, &cli_size) < 0 )
        {
            SysError("RecvFrom() failed.\n");
        }
        std::cout << recvbuf << std::endl;
        demoUDP.SendTo(sendbuf, sizeof(sendbuf), 0, (struct sockaddr *) &cli_addr, cli_size);
        sleep(1);
    }
    return 0;
}
