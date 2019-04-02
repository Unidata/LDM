/**
 * This file declares a TCP socket.
 *
 *        File: TcpSock.cpp
 *  Created on: Jan 30, 2018
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "Internet.h"
#include "TcpSock.h"

#include <arpa/inet.h>
#include <cerrno>
#include <system_error>
#include <unistd.h>

/******************************************************************************
 * TCP socket:
 ******************************************************************************/

class TcpSock::Impl
{
    size_t ioVecLen(
            const struct iovec* iov,
            int                 iovlen)
    {
        size_t nbytes = 0;
        while (iovlen > 0)
            nbytes += iov[--iovlen].iov_len;
        return nbytes;
    }

protected:
    int sd;

    /**
     * Returns the local address of the socket.
     * @return                   Local address of socket. Will be all zeros if
     *                           the socket is unbound.
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Safe
     */
    struct sockaddr_in localAddr() const
    {
        struct sockaddr_in addr = {};
        socklen_t          len = sizeof(addr);

        (void)::getsockname(sd,
                reinterpret_cast<struct sockaddr*>(&addr), &len);

        return addr;
    }

    /**
     * Returns the remote address of the socket.
     * @return  Remote address of socket. Will be all zeros if the socket is
     *          unconnected.
     */
    struct sockaddr_in remoteAddr() const
    {
        struct sockaddr_in addr = {};
        socklen_t          len = sizeof(addr);

        (void)::getpeername(sd,
                reinterpret_cast<struct sockaddr*>(&addr), &len);

        return addr;
    }

    /**
     * Returns the string representation of the local address of the socket.
     * @return  String representation of local address of socket
     */
    std::string localAddrStr() const
    {
        return ::to_string(localAddr());
    }

    /**
     * Returns the string representation of the remote address of the socket.
     * @return String representation of remote address of socket
     */
    std::string remoteAddrStr() const
    {
        return ::to_string(remoteAddr());
    }

public:
    /**
     * Constructs.
     * @param[in] sd  Socket descriptor from, for example, `accept()`
     */
    explicit Impl(const int sd)
        : sd{sd}
    {}

    /**
     * Constructs from the desired address family.
     * @param[in] family         Address family (e.g., IPV4)
     * @throw std::system_error  Couldn't create socket
     */
    Impl(const InetFamily family)
        : Impl{::socket(family, SOCK_STREAM, IPPROTO_TCP)}
    {
        if (sd < 0)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't create TCP socket");
    }

    /**
     * Constructs from the local endpoint address.
     * @param[in] localAddr      Local address. If the port number if zero, then
     *                           the operating system will choose an ephemeral
     *                           port.
     * @throw std::system_error  Couldn't bind socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Compatible but not safe
     */
    Impl(const InetSockAddr localAddr)
        : Impl{localAddr.getFamily()}
    {
        localAddr.bind(sd);
    }

    /**
     * Destroys.
     */
    virtual ~Impl() noexcept
    {
        ::close(sd);
        sd = -1;
    }

    /**
     * Connects to a remote endpoint.
     * @param[in] rmtSockAddr    Address of remote endpoint
     * @throw std::system_error  Couldn't connect socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Compatible but not safe
     */
    void connect(const InetSockAddr& rmtSockAddr)
    {
        try {
            rmtSockAddr.connect(sd);
        }
        catch (const std::exception& ex) {
            std::throw_with_nested(std::runtime_error(
                    "Couldn't connect TCP socket " + toString() + " to " +
                    rmtSockAddr.to_string()));
        }
    }

    /**
     * Returns the Internet socket address of the local endpoint.
     * @return           Internet socket address of local endpoint
     * @exceptionsafety  Strong guarantee
     * @threadsafety     Safe
     */
    InetSockAddr getLocalSockAddr() const
    {
        return InetSockAddr{localAddr()};
    }

    /**
     * Sends to the remote address.
     * @param[in] buf            Data to send
     * @param[in] nbytes         Number of bytes to send
     * @throw std::system_error  I/O failure
     */
    void write(
            const void*  buf,
            const size_t nbytes)
    {
        auto status = ::send(sd, buf, nbytes, MSG_NOSIGNAL);
        if (status != nbytes)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't send " + std::to_string(nbytes) + " to remote "
                    "address " + remoteAddrStr());
    }

    /**
     * Gather-writes to the remote address.
     * @param[in] iov            I/O vector
     * @param[in] iovlen         Number of elements in `iov`
     * @throw std::system_error  I/O failure
     */
    void writev(
            const struct iovec* iov,
            const int           iovlen)
    {
        struct msghdr msghdr = {};
        msghdr.msg_iov = const_cast<struct iovec*>(iov);
        msghdr.msg_iovlen = iovlen;
        auto status = ::sendmsg(sd, &msghdr, MSG_NOSIGNAL);
        auto nbytes = ioVecLen(iov, iovlen);
        if (status != nbytes)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't send " + std::to_string(nbytes) + " to remote "
                    "address " + remoteAddrStr());
    }

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
            const size_t nbytes)
    {
        const auto status = ::recv(sd, buf, nbytes, MSG_WAITALL);

        if (status == -1)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't receive " + std::to_string(nbytes) +
                    " bytes from remote address " + remoteAddrStr() +
                    " on socket " + std::to_string(sd));

        return status < nbytes ? 0 : status;
    }

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
            const int           iovlen)
    {
        struct msghdr msghdr = {};
        msghdr.msg_iov = const_cast<struct iovec*>(iov);
        msghdr.msg_iovlen = iovlen;
        const auto status = ::recvmsg(sd, &msghdr, MSG_WAITALL);
        const auto nbytes = ioVecLen(iov, iovlen);
        if (status == 0 || status == nbytes)
            return status;
        throw std::system_error(errno, std::system_category(),
                "Couldn't receive " + std::to_string(nbytes) + " bytes from "
                " remote address " + remoteAddrStr());
    }

    /**
     * Returns the string representation of the socket.
     * @return           String representation of socket
     * @exceptionsafety  Strong guarantee
     * @threadsafety     Safe
     */
    std::string toString()
    {
        return std::string{"{sd="} + std::to_string(sd) + ", localAddr=" +
                localAddrStr() + ", remoteAddr=" + remoteAddrStr() + "}";
    }

    /**
     * Closes the connection.
     * @throw std::system_error  Socket couldn't be closed
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Safe
     */
    void close()
    {
        auto status = ::close(sd);
        if (status == -1)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't close socket " + std::to_string(sd));
    }
};

TcpSock::TcpSock()
    : pImpl{}
{}

TcpSock::TcpSock(const InetFamily family)
    : pImpl{new Impl(family)}
{}

TcpSock::TcpSock(Impl* impl)
    : pImpl{impl}
{}

TcpSock::TcpSock(const int sd)
    : pImpl{new Impl(sd)}
{}

TcpSock::TcpSock(const InetSockAddr localAddr)
    : pImpl{new Impl(localAddr)}
{}

void TcpSock::connect(const InetSockAddr& rmtSockAddr) const
{
    pImpl->connect(rmtSockAddr);
}

InetSockAddr TcpSock::getLocalSockAddr() const
{
    return pImpl->getLocalSockAddr();
}

void TcpSock::write(
        const void*  buf,
        const size_t nbytes) const
{
    return pImpl->write(buf, nbytes);
}

void TcpSock::writev(
        const struct iovec* iov,
        const int           iovcnt) const
{
    pImpl->writev(iov, iovcnt);
}

size_t TcpSock::read(
        void*        buf,
        const size_t nbytes) const
{
    return pImpl->read(buf, nbytes);
}

size_t TcpSock::readv(
        const struct iovec* iov,
        const int           iovcnt) const
{
    return pImpl->readv(iov, iovcnt);
}

std::string TcpSock::to_string() const
{
    return pImpl->toString();
}

void TcpSock::close()
{
    return pImpl->close();
}

/******************************************************************************
 * Server-side TCP socket:
 ******************************************************************************/

class SrvrTcpSock::Impl final : public TcpSock::Impl
{
    /**
     * Initializes. Binds the socket to the local address and calls `::listen()`
     * on it. A subsequent `getPort()` will not return `0`.
     * @param[in] localAddr        Local address of socket
     * @param[in] backlog          Size of `listen(2)` queue
     * @throws std::system_error  `listen(2)` failure
     * @see getPort()
     */
    void init(
            const struct InetSockAddr& localAddr,
            const int                  backlog)
    {
        localAddr.bind(sd);
        if (::listen(sd, backlog))
            throw std::system_error{errno, std::system_category(),
                    std::string{"listen() failure on socket "} +
                    localAddrStr()};
    }

public:
    /**
     * Constructs. The socket will accept connections on all available
     * interfaces.
     * @param[in] family          Internet address family
     *                            (e.g., `InetFamily::IPv4`)
     * @param[in] backlog         Size of `listen(2)` queue
     * @throws std::system_error  `listen(2)` failure
     */
    Impl(   const InetFamily family,
            const int        backlog)
        : TcpSock::Impl{family}
    {
        init(InetSockAddr{family}, backlog);
    }

    /**
     * Constructs. Binds the socket to the local address and readies it to
     * accept incoming connections. A subsequent `getPort()` will not return
     * `0`.
     * @param[in] localAddr       Local address of socket
     * @param[in] backlog         Size of `listen(2)` queue
     * @throws std::system_error  `listen(2)` failure
     * @see getPort()
     */
    Impl(   const InetAddr& localAddr,
            const int       backlog)
        : TcpSock::Impl{localAddr.getFamily()}
    {
        init(InetSockAddr{localAddr, 0}, backlog);
    }

    /**
     * Constructs. Binds the socket to the local address and readies it to
     * accept incoming connections. A subsequent `getPort()` will not return
     * `0`.
     * @param[in] localAddr       Local address of socket
     * @param[in] backlog         Size of `listen(2)` queue
     * @throws std::system_error  `listen(2)` failure
     * @see getPort()
     */
    Impl(   const InetSockAddr& localAddr,
            const int           backlog)
        : TcpSock::Impl{localAddr.getFamily()}
    {
        init(localAddr, backlog);
    }

    /**
     * Returns the port number of the socket's local address.
     * @return           Port number of socket's local address in host
     *                   byte-order
     * @exceptionsafety  Strong guarantee
     * @threadsafety     Safe
     */
    in_port_t getPort() const noexcept
    {
        return ntohs(localAddr().sin_port);
    }

    /**
     * Accepts an incoming connection.
     * @return                   New connection
     * @throw std::system_error  `accept(2)` failure
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Safe
     */
    TcpSock accept()
    {
        auto connSock = ::accept(sd, nullptr, nullptr);
        if (connSock < 0)
            throw std::system_error{errno, std::system_category(),
                    std::string{"accept() failure on socket "} +
                    localAddrStr()};
        return TcpSock{connSock};
    }
};

SrvrTcpSock::SrvrTcpSock(
        const InetAddr& localAddr,
        const int       backlog)
    : TcpSock{new Impl(localAddr, backlog)}
{}

SrvrTcpSock::SrvrTcpSock(
        const InetSockAddr& localAddr,
        const int           backlog)
    : TcpSock{new Impl(localAddr, backlog)}
{}

SrvrTcpSock::SrvrTcpSock(
        const InetFamily family,
        const int        backlog)
    : TcpSock{new Impl(family, backlog)}
{}

in_port_t SrvrTcpSock::getPort() const noexcept
{
    return static_cast<Impl*>(pImpl.get())->getPort();
}

TcpSock SrvrTcpSock::accept()
{
    return static_cast<Impl*>(pImpl.get())->accept();
}
