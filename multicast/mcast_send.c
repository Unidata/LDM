/**
 * mcast_send.c -- multicasts "hello, world!" to a multicast group once a second
 *
 * Created by: Antony Courtney, 25/11/94
 * Modified by: Shawn Chen, Apr. 7, 2015
 */


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


#define HELLO_PORT 5173
#define HELLO_GROUP "224.0.0.1"


int main(int argc, char *argv[])
{
    if (argc < 1) {
        perror("insufficient arguments");
    }

    struct sockaddr_in addr;
    struct in_addr iface;
    int fd, cnt;
    struct ip_mreq mreq;
    char message[] = "hello multicast world";
    /* create what looks like an ordinary UDP socket */
    if ((fd=socket(AF_INET,SOCK_DGRAM,0)) < 0) {
        perror("socket");
        exit(1);
    }
    /* set up destination address */
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(HELLO_GROUP);
    addr.sin_port = htons(HELLO_PORT);

    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        perror("connect");
        exit(1);
    }

    /* code for setting TTL */
    int newttl = 1;
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &newttl,
                   sizeof(newttl)) < 0) {
        perror("set TTL");
        exit(1);
    };

    iface.s_addr = inet_addr(argv[1]);
    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &iface,
                   sizeof(iface)) < 0) {
        perror("set IF");
        exit(1);
    };

    /* now just sendto() our destination! */
    while (1) {
        if (send(fd, message, sizeof(message), 0) < 0) {
            perror("sendto");
            exit(1);
        }
        sleep(1);
    }

    return 0;
}
