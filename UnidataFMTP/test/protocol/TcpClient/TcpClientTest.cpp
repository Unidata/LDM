#include <iostream>
#include "TcpClient.h"
#include <errno.h>
#include <string>
#include <stdexcept>

using namespace std;

int main(int argc, char* argv[])
{
    const string demoServHost = "rivanna.cs.virginia.edu";
    const int demoServPort = 1234;
    char sendDataBuf[256], recvDataBuf[28];

    // clear two buffers
    bzero(sendDataBuf, 256);
    bzero(recvDataBuf, 28);

    TcpClient demoClient(demoServHost, demoServPort);

    // test Connect() function
    demoClient.Connect();

    // test GetSocket() function
    cout << "Socket file descriptor number is: " << demoClient.GetSocket() << endl;

    cout << "Enter your message:" << endl;
    // read input from stdin (e.g. keyboard)
    fgets(sendDataBuf, 255, stdin);
    strtok(sendDataBuf, "\n");

    // test Send() function
    demoClient.Send(sendDataBuf, sizeof(sendDataBuf));

    // test Receive() function
    demoClient.Receive(recvDataBuf, sizeof(recvDataBuf));
    cout << recvDataBuf;

    cout << "TcpClient Tested okay!" << endl;
    while(1);
    return 0;
}
