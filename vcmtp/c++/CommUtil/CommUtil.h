/*
 * CommUtil.h
 *
 *  Created on: Jun 26, 2011
 *      Author: jie
 */

#ifndef COMMUTIL_H_
#define COMMUTIL_H_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <iostream>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

using namespace std;

enum MsgType {
    NODE_NAME = 1,
    IP_ADDRESS = 2,
    INFORMATIONAL = 3,
    WARNING = 4,
    COMMAND = 5,
    COMMAND_RESPONSE = 6,
    EXP_RESULT_REPORT = 7,
    PARAM_SETTING = 8
};


#define BUFFER_SIZE 	65536


#endif /* COMMUTIL_H_ */
