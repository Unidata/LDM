#include <iostream>
#include "MulticastComm.h"
#include <string>
#include <linux/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define on 1
#define off 0

using namespace std;

int main(int argc, char* argv[])
{
    const string demoServHost = "rivanna.cs.virginia.edu";
    const int demoServPort = 1234;
    const char sendDataBuf[] = "hello, multicast world!";
    char recvDataBuf[256];

    struct sockaddr_in demo_sain;
    bzero(&demo_sain, sizeof(demo_sain));
    const char if_name[] = "eth0";
    demo_sain.sin_family = AF_INET;
    demo_sain.sin_addr.s_addr = inet_addr("224.0.0.1");
    demo_sain.sin_port = htons(demoServPort);

    MulticastComm demoMcast;
    // test JoinGroup()
    int join_retval = demoMcast.JoinGroup((struct sockaddr *)&demo_sain, sizeof(demo_sain), if_name);
    if(join_retval == 0)
        cout << "UDP socket set, multicast group set." << endl;

    // test SetLoopBack()
    demoMcast.SetLoopBack(off);

    while(1) {
        // test SendData()
        demoMcast.SendData(sendDataBuf, sizeof(sendDataBuf), 0, NULL);
    }

    return 0;
}
