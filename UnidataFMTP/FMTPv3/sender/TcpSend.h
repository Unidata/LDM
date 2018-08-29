/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      TcpSend.h
 * @author    Shawn Chen <sc7cq@virginia.edu>
 * @version   1.0
 * @date      Nov 17, 2014
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
 * @brief     Define the interfaces and structures of sender side TCP layer
 *            abstracted funtions.
 *
 * The TcpSend class includes a set of transmission functions, which are
 * basically the encapsulation of tcp system calls themselves. This abstracted
 * new layer acts as the sender side transmission library.
 */



#ifndef FMTP_SENDER_TCPSEND_H_
#define FMTP_SENDER_TCPSEND_H_


#include <arpa/inet.h>
#include <pthread.h>
#include <atomic>
#include <list>
#include <mutex>
#include <string>

#include "TcpBase.h"
#include "fmtpBase.h"


class TcpSend : public TcpBase
{
public:
    /** source port would be initialized to 0 if not being specified. */
    TcpSend(std::string tcpaddr, unsigned short tcpport = 0);
    ~TcpSend();

    int acceptConn();
    void dismantleConn(int sockfd);
    /** return the reference of a socket list */
    const std::list<int> getConnSockList();
    int getMinPathMTU();
    unsigned short getPortNum();
    void Init(); /*!< start point that upper layer should call */
    /** only parse the header part of a coming packet */
    int parseHeader(int retxsockfd, FmtpHeader* recvheader);
    /** read any data coming into this given socket */
    int readSock(int retxsockfd, char* pktBuf, int bufSize);
    void rmSockInList(int sockfd);
    /** gathering send by calling io vector system call */
    int sendData(int retxsockfd, FmtpHeader* sendheader, char* payload,
                 size_t paylen);
    static int send(int retxsockfd, FmtpHeader* sendheader, char* payload,
                    size_t paylen);
    void updatePathMTU(int sockfd);

private:
    struct sockaddr_in servAddr;
    std::string        tcpAddr;
    unsigned short     tcpPort;
    std::list<int>     connSockList;
    std::mutex         sockListMutex; /*!< to protect shared sockList */
    std::atomic<int>   pmtu; /* min path MTU of the mcast group */

    /**
     * Sets the keep-alive mechanism on a TCP socket.
     *
     * @param[in] sock               The TCP socket on which to set keep-alive
     * @throws    std::system_error  Keep-alive couldn't be set
     */
    void setKeepAlive(const int sock);
};


#endif /* FMTP_SENDER_TCPSEND_H_ */
