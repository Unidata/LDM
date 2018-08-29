/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      udp_send.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Mar 25, 2015
 *
 * @section   LICENSE
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief     UDP sending test program
 */


#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>


int main(int argc, char* argv[])
{
    int s;
    int ret;
    char buf[1460];
    struct sockaddr_in addr;
    (void*)memset(buf, 0xa, sizeof(buf));

    if (argc != 3) {
        puts("usage: send ipaddr port");
        exit(1);
    }

    addr.sin_family = AF_INET;
    ret = inet_aton(argv[1], &addr.sin_addr);
    if (ret == 0) {
        perror("inet_aton");
        exit(1);
    }
    addr.sin_port = htons(atoi(argv[2]));

    s = socket(PF_INET, SOCK_DGRAM, 0);
    if (s == -1) {
        perror("socket");
        exit(1);
    }

    while(1) {
        ret = sendto(s, buf, strlen(buf), 0, (struct sockaddr *)&addr,
                     sizeof(addr));
        if (ret == -1) {
            perror("sendto");
            exit(1);
        }
    }

    return 0;
}
