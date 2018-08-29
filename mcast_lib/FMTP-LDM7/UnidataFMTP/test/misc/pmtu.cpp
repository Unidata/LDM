/**
 * Copyright (C) 2015 University of Virginia. All rights reserved.
 *
 * @file      pmtu.cpp
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      May 13, 2016
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
 * @brief     Path MTU discovery
 */


#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>

int main(int argc, char* argv[])
{
    /* works for both DGRAM and STREAM */
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct ifreq ifr;
    strcpy(ifr.ifr_name, "eth0");
    if(!ioctl(sock, SIOCGIFMTU, &ifr)) {
        printf("%d\n", ifr.ifr_mtu);
    }
    return 0;
}
