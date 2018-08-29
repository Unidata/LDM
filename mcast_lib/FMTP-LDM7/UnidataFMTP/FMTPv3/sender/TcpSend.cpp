/**
 * Copyright (C) 2014 University of Virginia. All rights reserved.
 *
 * @file      TcpSend.cpp
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
 * @brief     Implement the interfaces and structures of sender side TCP layer
 *            abstracted funtions.
 *
 * The TcpSend class includes a set of transmission functions, which are
 * basically the encapsulation of tcp system calls themselves. This abstracted
 * new layer acts as the sender side transmission library.
 */


#include "TcpSend.h"

#include <errno.h>
#include <exception>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdexcept>
#include <system_error>


#ifndef NULL
    #define NULL 0
#endif
#define MAX_CONNECTION 100


/**
 * Contructor for TcpSend class.
 *
 * @param[in] tcpaddr     Specification of the interface on which the TCP
 *                        server will listen as a dotted-decimal IPv4 address.
 * @param[in] tcpport     tcp port number (in host order) specified by sending
 *                        application. (or 0, meaning system will use random
 *                        available port)
 */
TcpSend::TcpSend(std::string tcpaddr, unsigned short tcpport)
    : tcpAddr(tcpaddr), tcpPort(tcpport), sockListMutex(),
      servAddr()
{
}


/**
 * Destructor for TcpSend class. Release all the allocated resources, including
 * mutex, socket lists and etc.
 *
 * @param[in] none
 */
TcpSend::~TcpSend()
{
    {
        std::unique_lock<std::mutex> lock(sockListMutex); // cache coherence
        connSockList.clear();
    }
}


/**
 * Sets the keep-alive mechanism on a TCP socket. When the mechanism determines
 * that the socket is no longer connected, a subsequent `read()` on the socket
 * should either cause a SIGPIPE to be generated or the `read()` to return `-1`.
 *
 * @param[in] sock               The TCP socket on which to set keep-alive
 * @throws    std::system_error  Keep-alive couldn't be set
 */
void TcpSend::setKeepAlive(
        const int sock)
{
    int             enabled = 1;
    const socklen_t intlen = sizeof(int);
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &enabled, intlen))
        throw std::system_error(errno, std::system_category(),
                "TcpSend::setKeepAlive() Couldn't enable TCP keep-alive on "
                "socket " + std::to_string(sock));

#ifdef SO_NOSIGPIPE
    // Favor synchronous notification of disconnection via `read()`
    if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &enabled, intlen))
        throw std::system_error(errno, std::system_category(),
                "TcpSend::setKeepAlive() Couldn't disable SIGPIPE on socket " +
                std::to_string(sock));
#endif

    int idle = 60;     // number of idle seconds before probing
    int interval = 30; // seconds between probes
    int count = 5;     // number of probes before failure

#ifdef IPPROTO_TCP
    int proto_level = IPPROTO_TCP;
#elif defined(SOL_TCP)
    int proto_level = SOL_TCP;
#else
    #error No macro defined for the TCP protocol level
#endif

#if __sun__
    #if TCP_KEEPALIVE_THRESHOLD
        int idle_opt = TCP_KEEPALIVE_THRESHOLD;
    #elif TCP_NOTIFY_THRESHOLD
        int idle_opt = TCP_NOTIFY_THRESHOLD;
    #else
        #error No TCP keep-alive idle-time macro
    #endif
    #if TCP_KEEPALIVE_ABORT_THRESHOLD
        int duration_opt = TCP_KEEPALIVE_ABORT_THRESHOLD;
    #elif TCP_ABORT_THRESHOLD
        int duration_opt = TCP_ABORT_THRESHOLD;
    #else
        #error No TCP keep-alive duration macro
    #endif
    idle *= 1000; // Milliseconds
    unsigned duration = (interval*(count-1)) * 1000; // Milliseconds
    if (setsockopt(sock, proto_level, idle_opt, &idle, intlen) ||
            setsockopt(sock, proto_level, duration_opt, &duration,
                    sizeof(duration)))
#elif __APPLE__
    if (setsockopt(sock, proto_level, TCP_KEEPALIVE, &idle, intlen) ||
            setsockopt(sock, proto_level, TCP_KEEPINTVL, &interval, intlen) ||
            setsockopt(sock, proto_level, TCP_KEEPCNT, &count, intlen))
#elif __linux__
    if (setsockopt(sock, proto_level, TCP_KEEPIDLE, &idle, intlen) ||
            setsockopt(sock, proto_level, TCP_KEEPINTVL, &interval, intlen) ||
            setsockopt(sock, proto_level, TCP_KEEPCNT, &count, intlen))
#else
    #error Do not know how to set keep-alive parameters for this O/S
#endif
        throw std::system_error(errno, std::system_category(),
                "TcpSend::setKeepAlive() Couldn't set TCP keep-alive "
                "parameters on socket " + std::to_string(sock));
}


/**
 * Accept incoming tcp connection requests and push them into the socket list.
 * Then return the current socket file descriptor for further use. The socket
 * list is a globally shared resource, thus it needs to be protected by a lock.
 *
 * @param[in] none
 * @return    newsockfd       file descriptor of the newly connected socket.
 * @throw  std::system_error  if accept() system call fails.
 * @throw  std::system_error  if TCP keepalive can't be enabled on the socket.
 */
int TcpSend::acceptConn()
{
    struct sockaddr_in addr;
    unsigned           addrLen = sizeof(addr);
    if (getsockname(sockfd, (struct sockaddr*)&addr, &addrLen)) {
        throw std::system_error(errno, std::system_category(),
                "TcpSend::acceptConn() couldn't get address of socket " +
                std::to_string(static_cast<long long>(sockfd)));
    }

    int newsockfd = accept(sockfd, NULL, NULL);
    if(newsockfd < 0) {
        throw std::system_error(errno, std::system_category(),
                "TcpSend::acceptConn() error reading from socket");
    }

    setKeepAlive(newsockfd);

    {
        std::unique_lock<std::mutex> lock(sockListMutex);
        connSockList.push_back(newsockfd);
    }

    return newsockfd;
}


/**
 * Closes tcp connections and removes them from the current connection list.
 *
 * @param[in] sockfd          Socket to be closed.
 * @throw  std::system_error  if close() system call fails.
 */
void TcpSend::dismantleConn(int sockfd)
{
    rmSockInList(sockfd);
    if (close(sockfd) < 0) {
        throw std::system_error(errno, std::system_category(),
                "TcpSend::dismantleConn() error closing socket");
    }
}


/**
 * Accept incoming tcp connection requests and push them into the socket list.
 * Then return the current socket list.
 *
 * @param[in] none
 * @return    connSockList          connected socket list (a collection of
 */
const std::list<int> TcpSend::getConnSockList()
{
    std::unique_lock<std::mutex> lock(sockListMutex);
    return connSockList;
}


/**
 * Gets the min path MTU.
 *
 * @param[in] none
 * @return    pmtu    Up-to-date min path MTU.
 */
int TcpSend::getMinPathMTU()
{
    return pmtu;
}


/**
 * Return the local port number.
 *
 * @return                   The local port number in host byte-order.
 * @throw std::system_error  The port number cannot be obtained.
 */
unsigned short TcpSend::getPortNum()
{
    struct sockaddr_in tmpAddr;
    socklen_t          tmpAddrLen = sizeof(tmpAddr);

    if (getsockname(sockfd, (struct sockaddr*)&tmpAddr, &tmpAddrLen) < 0) {
        throw std::system_error(errno, std::system_category(),
                "TcpSend::getPortNum() error getting port number");
    }

    return ntohs(tmpAddr.sin_port);
}


/**
 * Initializer for TcpSend class, taking tcp address and tcp port to establish
 * a tcp connection. When the connection is established, keep listen on it with
 * a maximum of MAX_CONNECTION allowed to connect. Consistency is strongly
 * ensured by canceling whatever has been done before exceptions are thrown.
 *
 * @param[in] none
 *
 * @throw  std::system_error    if `tcpAddr` is invalid.
 * @throw  std::system_error    if socket creation fails.
 * @throw  std::system_error    if socket bind() operation fails.
 */
void TcpSend::Init()
{
    int alive = 1;
    //int aliveidle = 10; /* keep alive time = 10 sec */
    int aliveintvl = 30; /* keep alive interval = 30 sec */
    pmtu = MIN_MTU; /* initialize pmtu with defined min MTU */

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        throw std::system_error(errno, std::system_category(),
                "TcpSend::Init() error creating socket");
    }

    try {
        (void) memset((char *) &servAddr, 0, sizeof(servAddr));
        servAddr.sin_family = AF_INET;
        in_addr_t inAddr = inet_addr(tcpAddr.c_str());
        if ((in_addr_t)(-1) == inAddr) {
            throw std::system_error(errno, std::system_category(),
                    "TcpSend::Init() Invalid interface: " + tcpAddr);
        }
        servAddr.sin_addr.s_addr = inAddr;
        /* If tcpPort = 0, OS will automatically choose an available port number. */
        servAddr.sin_port = htons(tcpPort);
#if 0
        cerr << std::string("TcpSend::TcpSend() Binding TCP socket to ").
                append(tcpAddr).append(":").append(std::to_string(tcpPort)).
                append("\n");
#endif
        if(::bind(sockfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0) {
            throw std::system_error(errno, std::system_category(),
                    "TcpSend::TcpSend(): Couldn't bind " + tcpAddr + ":"
                    + std::to_string(static_cast<unsigned int>(tcpPort)));
        }
    }
    catch (std::runtime_error& e) {
        close(sockfd);
        /**
         * Let the sender make some noise instead of quietly sending a FIN
         * to the receiver. The exception caught in TcpSend will bubble up
         * and eventually being logged in the LDM log file.
         */
        std::rethrow_exception(std::current_exception());
    }
    /* listen() returns right away, it's non-blocking */
    listen(sockfd, MAX_CONNECTION);
}


/**
 * Read an amount of bytes from the socket while the number of bytes equals the
 * FMTP header size. Parse the buffer which stores the packet header and fill
 * each field of FmtpHeader structure with corresponding information. If the
 * read() system call fails, return immediately. Otherwise, return when this
 * function finishes.
 *
 * @param[in] retxsockfd    retransmission socket file descriptor.
 * @param[in] *recvheader   pointer of a FmtpHeader structure, whose fields
 *                          are to hold the parsed out information.
 * @return    retval        return the status value returned by read()
 */
int TcpSend::parseHeader(int retxsockfd, FmtpHeader* recvheader)
{
    char recvbuf[FMTP_HEADER_LEN];
    if (recvall(retxsockfd, recvbuf, FMTP_HEADER_LEN) < FMTP_HEADER_LEN)
        return 0;

    // TODO: re-write using sizeof()
    memcpy(&recvheader->prodindex,  recvbuf,    4);
    memcpy(&recvheader->seqnum,     recvbuf+4,  4);
    memcpy(&recvheader->payloadlen, recvbuf+8,  2);
    memcpy(&recvheader->flags,      recvbuf+10, 2);
    recvheader->prodindex  = ntohl(recvheader->prodindex);
    recvheader->seqnum     = ntohl(recvheader->seqnum);
    recvheader->payloadlen = ntohs(recvheader->payloadlen);
    recvheader->flags      = ntohs(recvheader->flags);

    return FMTP_HEADER_LEN;
}


/**
 * Read a given amount of bytes from the socket.
 *
 * @param[in] retxsockfd    retransmission socket file descriptor.
 * @param[in] *pktBuf       pointer to the buffer to store the received bytes.
 * @param[in] bufSize       size of that buffer
 * @return    int           return the return value of read() system call.
 */
int TcpSend::readSock(int retxsockfd, char* pktBuf, int bufSize)
{
    return read(retxsockfd, pktBuf, bufSize);
}


/**
 * Removes the given socket from the list.
 *
 * @param[in] sockfd    retransmission socket file descriptor.
 */
void TcpSend::rmSockInList(int sockfd)
{
    std::unique_lock<std::mutex> lock(sockListMutex);
    connSockList.remove(sockfd);
}


/**
 * Sends a FMTP packet through the given retransmission connection identified
 * by retxsockfd. It blocks until all sending is finished. Or it can terminate
 * with error occurred.
 *
 * @param[in] retxsockfd    retransmission socket file descriptor.
 * @param[in] *sendheader   pointer of a FmtpHeader structure, whose fields
 *                          are to hold the ready-to-send information.
 * @param[in] *payload      pointer to the ready-to-send memory buffer which
 *                          holds the packet payload.
 * @param[in] paylen        size to be sent (size of the payload)
 * @return    retval        return the total bytes sent.
 */
int TcpSend::sendData(int retxsockfd, FmtpHeader* sendheader, char* payload,
                      size_t paylen)
{
    sendall(retxsockfd, sendheader, sizeof(FmtpHeader));
    sendall(retxsockfd, payload, paylen);

    return (sizeof(FmtpHeader) + paylen);
}


/**
 * Sends a FMTP packet through the given retransmission connection identified
 * by retxsockfd. It blocks until all sending is finished. Or it can terminate
 * with error occurred. This is the static member function in alternative to
 * the TcpSend::sendData().
 *
 * @param[in] retxsockfd    retransmission socket file descriptor.
 * @param[in] *sendheader   pointer of a FmtpHeader structure, whose fields
 *                          are to hold the ready-to-send information.
 * @param[in] *payload      pointer to the ready-to-send memory buffer which
 *                          holds the packet payload.
 * @param[in] paylen        size to be sent (size of the payload)
 * @return    retval        return the total bytes sent.
 */
int TcpSend::send(int retxsockfd, FmtpHeader* sendheader, char* payload,
                  size_t paylen)
{
    sendallstatic(retxsockfd, sendheader, sizeof(FmtpHeader));
    sendallstatic(retxsockfd, payload, paylen);

    return (sizeof(FmtpHeader) + paylen);
}


/**
 * Reads the path MTU of a receiver connection, and updates the minimum path
 * MTU with the new obtained value (or remains unchanged).
 *
 * @param[in] sockfd    socket file descriptor of a receiver connection.
 *
 * @throw  std::system_error    if obtained MTU is invalid.
 */
void TcpSend::updatePathMTU(int sockfd)
{
    int mtu = MIN_MTU;
#ifdef IP_MTU
    socklen_t mtulen = sizeof(mtu);
    /*
     * Coverity Scan #1: Fix #7: getsockopt has a return value of 0 if successful, -1 if unsuccessful. 
     * The fix simply handles the return value and throws a runtime error should the return value be -1
     */
    if (getsockopt(sockfd, IPPROTO_IP, IP_MTU, &mtu, &mtulen)){
	throw std::runtime_error("fmtpRecvv3::updatePathMTU() getsockopt failed with return value -1 in an attempt to obtain MTU.");
    }
    if (mtu <= 0) {
        throw std::system_error(errno, std::system_category(),
                "TcpSend::updatePathMTU() error obtaining MTU");
    }
    /* force mtu to be at least MIN_MTU, cannot afford mtu to be too small */
    mtu = (mtu < MIN_MTU) ? MIN_MTU : mtu;
#endif
    /* update pmtu with the newly joined mtu */
    if (mtu < pmtu) {
         pmtu = mtu;
    }
}
