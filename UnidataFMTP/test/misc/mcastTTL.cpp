/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      mcastTTL.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Mar 26, 2015
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
 * @brief     setting IP Multicast TTL demo
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>


int main()
{
    int s;
    int ttl;
    int newttl = 4;
    socklen_t optlen;

    /* Create the socket */
    if((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("create socket error");
    }

    /* Check the status */
    if(getsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, &optlen) < 0) {
        perror("getsockopt()");
        close(s);
        exit(EXIT_FAILURE);
    }
    printf("TTL = %d\n", ttl);

    if(setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &newttl, sizeof(newttl)) < 0) {
        perror("setsockopt()");
        close(s);
        exit(EXIT_FAILURE);
    }

    /* Check the status again */
    if(getsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, &optlen) < 0) {
        perror("getsockopt()");
        close(s);
        exit(EXIT_FAILURE);
    }
    printf("TTL = %d\n", ttl);

    close(s);

    exit(EXIT_SUCCESS);
}
