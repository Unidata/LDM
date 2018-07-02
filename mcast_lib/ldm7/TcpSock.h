/**
 * This file declares a TCP socket.
 *
 *        File: TcpSock.h
 *  Created on: Jan 30, 2018
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_LDM7_TCPSOCK_H_
#define MCAST_LIB_LDM7_TCPSOCK_H_

#include "Internet.h"

#include <cstddef>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>

/**
 * A TCP socket.
 */
class TcpSock
{
protected:
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

    /**
     * Constructs from an implementation.
     * @param[in] impl  An implementation
     */
    TcpSock(Impl* impl);

public:
    /**
     * Default constructs.
     */
    TcpSock();

    /**
     * Constructs from the address family.
     * @param[i]  family         Address family (e.g, `InetFamily::IPV4`)
     * @throw std::system_error  Couldn't create socket
     */
    TcpSock(const InetFamily family);

    /**
     * Constructs.
     * @param[in] sd  Socket descriptor from `accept()`, for example
     */
    TcpSock(const int sd);

    /**
     * Constructs from the the local endpoint address.
     * @param[in] localAddr      Local address
     * @throw std::system_error  Couldn't bind socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Compatible but not safe
     */
    TcpSock(const InetSockAddr localAddr);

    /**
     * Connects to a remote endpoint.
     * @param[in] rmtSockAddr    Address of remote endpoint
     * @throw std::system_error  Couldn't connect socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Compatible but not safe
     */
    void connect(const InetSockAddr& rmtSockAddr) const;

    /**
     * Returns the Internet socket address of the local endpoint.
     * @return           Internet socket address of local endpoint
     * @exceptionsafety  Strong Guarantee
     * @threadsafety     Safe
     */
    InetSockAddr getLocalSockAddr() const;

    /**
     * Sends to the remote address.
     * @param[in] buf            Data to send
     * @param[in] nbytes         Number of bytes to send
     * @throw std::system_error  I/O failure
     */
    void write(
            const void*  buf,
            const size_t nbytes) const;

    /**
     * Gather-sends to the remote address.
     * @param[in] iov            I/O vector
     * @param[in] iovlen         Number of elements in `iov`
     * @throw std::system_error  I/O failure
     */
    void writev(
            const struct iovec* iov,
            const int           iovcnt) const;

    /**
     * Reads from the TCP connection.
     * @param[in] buf            Buffer into which to read data
     * @param[in] nbytes         Number of bytes to read
     * @retval 0                 Connection is closed
     * @retval `nbytes`          Success
     * @throw std::system_error  I/O failure
     */
    size_t read(
            void*        buf,
            const size_t nbytes) const;

    /**
     * Scatter-reads from the TCP connection.
     * @param[in] iov            I/O vector
     * @param[in] iovlen         Number of elements in `iov`
     * @retval 0                 Connection is closed
     * @retval                   Number of bytes specified by `iov`
     * @throw std::system_error  I/O failure
     */
    size_t readv(
            const struct iovec* iov,
            const int           iovcnt) const;

    /**
     * Returns a string representation of this instance's socket.
     * @return                   String representation of this instance
     * @throws std::bad_alloc    Required memory can't be allocated
     * @exceptionsafety          Strong
     * @threadsafety             Safe
     */
    std::string to_string() const;

    /**
     * Closes the connection.
     * @throw std::system_error  Socket couldn't be closed
     * @exceptionsafety          Strong
     * @threadsafety             Safe
     */
    void close();
};

/**
 * A server-side TCP socket.
 */
class SrvrTcpSock final : public TcpSock
{
    class Impl;

public:
    /**
     * Constructs.
     * @param[in] sockAddr  Local endpoint address on which to accept
     *                      connections
     * @param[in] backlog   Size of backlog queue
     */
    explicit SrvrTcpSock(
            const struct sockaddr_in localAddr,
            const int                backlog = 5);

    /**
     * Constructs. Binds the socket to the local address and an ephemeral port
     * and readies it to accept incoming connections. A subsequent `getPort()`
     * will not return `0`.
     * @param[in] inetAddr  Local endpoint address on which to accept
     *                      connections
     * @param[in] backlog   Size of backlog queue
     * @see getPort()
     */
    explicit SrvrTcpSock(
            const InetAddr& localAddr,
            const int       backlog = 5);

    /**
     * Constructs. Binds the socket to the local address. If the specified port
     * number is zero, then an ephemeral port is chosen; otherwise, the socket
     * is bound to the specified port. The socket is readied to accept incoming
     * connections. A subsequent `getPort()` will not return `0`.
     * @param[in] sockAddr  Local endpoint address on which to accept
     *                      connections
     * @param[in] backlog   Size of backlog queue
     * @see getPort()
     */
    explicit SrvrTcpSock(
            const InetSockAddr& localAddr,
            const int           backlog = 5);

    /**
     * Constructs. The socket will accept connections on all available
     * interfaces.
     * @param[in] family    Internet address family (e.g., `InetFamily::IPv4`)
     * @param[in] backlog   Size of backlog queue
     */
    explicit SrvrTcpSock(
            const InetFamily family,
            const int        backlog = 5);

    /**
     * Returns the port number of the local socket address.
     * @return                   Port number of local socket address in host
     *                           byte-order
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Safe
     */
    in_port_t getPort() const noexcept;

    /**
     * Returns an incoming connection.
     * @return                   Incoming connection
     * @throw std::system_error  `accept(2)` failure
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Safe
     */
    TcpSock accept();
};

#endif /* MCAST_LIB_LDM7_TCPSOCK_H_ */
