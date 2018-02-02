/**
 * This file declares a TCP socket.
 *
 *        File: TcpSock.h
 *  Created on: Jan 30, 2018
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___TCPSOCK_H_
#define MCAST_LIB_C___TCPSOCK_H_

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
     * @throw std::system_error  Couldn't create socket
     */
    TcpSock();

    /**
     * Constructs.
     * @param[in] sd  Socket descriptor from `accept()`, for example
     */
    TcpSock(const int sd);

    /**
     * Binds the local endpoint to an address.
     * @param[in] localAddr      Local address
     * @throw std::system_error  Couldn't bind socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Compatible but not safe
     */
    void bind(const struct sockaddr_in localAddr) const;

    /**
     * Connects to a remote endpoint.
     * @param[in] srvrAddr       Address of remote endpoint
     * @throw std::system_error  Couldn't connect socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Compatible but not safe
     */
    void connect(const struct sockaddr_in remoteAddr) const;

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
     * Receives from the remote address.
     * @param[in] buf            Buffer into which to read data
     * @param[in] nbytes         Number of bytes to read
     * @retval 0                 Connection is closed
     * @return                   Number of bytes read. Might be less than
     *                           `nbytes`.
     * @throw std::system_error  I/O failure
     */
    size_t read(
            void*        buf,
            const size_t nbytes) const;

    /**
     * Scatter-receives from the remote address.
     * @param[in] iov            I/O vector
     * @param[in] nbytes         Number of elements in `iov`
     * @retval 0                 Connection is closed
     * @return                   Number of bytes read. Might be less than
     *                           specified.
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
            const int                backlog = 0);

    /**
     * Constructs. The socket will accept connections on all available
     * interfaces.
     * @param[in] backlog   Size of backlog queue
     */
    explicit SrvrTcpSock(const int backlog = 0);

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

#endif /* MCAST_LIB_C___TCPSOCK_H_ */
