/**
 * This file declares a TCP socket.
 *
 *        File: TcpSock.cpp
 *  Created on: Jan 30, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

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
protected:
    int sd;

    /**
     * Returns the local address of the socket.
     * @return          Local address of socket
     * @exceptionsafety Strong guarantee
     * @threadsafety    Safe
     */
    struct sockaddr_in localAddr() const
    {
        struct sockaddr_in addr = {};
        socklen_t len = sizeof(addr);
        ::getsockname(sd, reinterpret_cast<struct sockaddr*>(&addr), &len);
        return addr;
    }

    /**
     * Returns the remote address of the socket.
     * @return Remote address of socket
     */
    struct sockaddr_in remoteAddr() const
    {
        struct sockaddr_in addr = {};
        socklen_t len = sizeof(addr);
        ::getpeername(sd, reinterpret_cast<struct sockaddr*>(&addr), &len);
        return addr;
    }

    /**
     * Returns the string representation of an address.
     * @return String representation of address
     */
    static std::string to_string(const struct sockaddr_in sockAddr)
    {
        char dottedQuad[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &sockAddr.sin_addr, dottedQuad,
                sizeof(dottedQuad));
        return std::string{dottedQuad} + ":" +
                std::to_string(ntohs(sockAddr.sin_port));
    }

    /**
     * Returns the string representation of the local address of the socket.
     * @return  String representation of local address of socket
     */
    std::string localAddrStr() const
    {
        return to_string(localAddr());
    }

    /**
     * Returns the string representation of the remote address of the socket.
     * @return String representation of remote address of socket
     */
    std::string remoteAddrStr() const
    {
        return to_string(remoteAddr());
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
     * Default constructs.
     * @throw std::system_error  Couldn't create socket
     */
    Impl()
        : Impl{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)}
    {
        if (sd < 0)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't create TCP socket");
    }

    /**
     * Destroys.
     */
    ~Impl() noexcept
    {
        ::close(sd);
    }

    /**
     * Binds the local endpoint to an address.
     * @param[in] localAddr      Local address
     * @throw std::system_error  Couldn't bind socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Compatible but not safe
     */
    void bind(const struct sockaddr_in localAddr)
    {
        if (::bind(sd, reinterpret_cast<const struct sockaddr*>(&localAddr),
                sizeof(localAddr)))
            throw std::system_error{errno, std::system_category(),
                    std::string{"Couldn't bind TCP socket to local address "} +
                    to_string(localAddr)};
    }

    /**
     * Connects to a remote endpoint.
     * @param[in] remoteAddr     Address of remote endpoint
     * @throw std::system_error  Couldn't connect socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Compatible but not safe
     */
    void connect(const struct sockaddr_in remoteAddr)
    {
        if (::connect(sd, reinterpret_cast<const struct sockaddr*>(&remoteAddr),
                sizeof(remoteAddr)))
            throw std::system_error{errno, std::system_category(),
                    std::string{"Couldn't connect socket to remote address "} +
                    to_string(remoteAddr)};
    }

    /**
     * Sends to the remote address.
     * @param[in] buf     Data to send
     * @param[in] nbytes  Number of bytes to send
     */
    void send(
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
     * Receives from the remote address.
     * @param[in] buf     Buffer into which to read data
     * @param[in] nbytes  Number of bytes to read
     * @retval 0          Connection is closed
     * @return            Number of bytes read. Might be less than `nbytes`.
     */
    size_t recv(
            void*        buf,
            const size_t nbytes)
    {
        auto status = ::recv(sd, buf, nbytes, MSG_WAITALL);
        if (status < 0)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't receive " + std::to_string(nbytes) +
                    " from remote address " + remoteAddrStr());
        return status;
    }

    /**
     * Returns the string representation of the socket.
     * @return           String representation of socket
     * @exceptionsafety  Strong guarantee
     * @threadsafety     Safe
     */
    std::string to_string()
    {
        return std::string{"{localAddr="} + localAddrStr() +
                ", remoteAddr=" + remoteAddrStr() + "}";
    }
};

TcpSock::TcpSock()
    : pImpl{new Impl()}
{}

TcpSock::TcpSock(Impl* impl)
    : pImpl{impl}
{}

TcpSock::TcpSock(const int sd)
    : pImpl{new Impl(sd)}
{}

void TcpSock::bind(const struct sockaddr_in localAddr) const
{
    pImpl->bind(localAddr);
}

void TcpSock::connect(const struct sockaddr_in remoteAddr) const
{
    pImpl->connect(remoteAddr);
}

void TcpSock::send(
        const void*  buf,
        const size_t nbytes) const
{
    return pImpl->send(buf, nbytes);
}

size_t TcpSock::recv(
        void*        buf,
        const size_t nbytes) const
{
    return pImpl->recv(buf, nbytes);
}

std::string TcpSock::to_string() const
{
    return pImpl->to_string();
}

/******************************************************************************
 * Server-side TCP socket:
 ******************************************************************************/

class SrvrTcpSock::Impl final : public TcpSock::Impl
{
    /**
     * Initializes.
     * @param[in] localAddr        Local address of socket
     * @param[in] backlog          Size of `listen(2)` queue
     * @throws std::system_error  `listen(2)` failure
     */
    void init(
            const struct sockaddr_in localAddr,
            const int                backlog)
    {
        bind(localAddr);
        if (::listen(sd, backlog))
            throw std::system_error{errno, std::system_category(),
                    std::string{"listen() failure on socket "} +
                    localAddrStr()};
    }

public:
    /**
     * Constructs. The socket will accept connections on all available
     * interfaces.
     * @param[in] backlog         Size of `listen(2)` queue
     * @throws std::system_error  `listen(2)` failure
     */
    Impl(const int backlog)
        : TcpSock::Impl{}
    {
        struct sockaddr_in localAddr = {};
        init(localAddr, backlog);
    }

    /**
     * Constructs.
     * @param[in] localAddr       Local address of socket
     * @param[in] backlog         Size of `listen(2)` queue
     * @throws std::system_error  `listen(2)` failure
     */
    Impl(   const struct sockaddr_in localAddr,
            const int                backlog)
        : TcpSock::Impl{}
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
        return ::ntohs(localAddr().sin_port);
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
        const struct sockaddr_in localAddr,
        const int                backlog)
    : TcpSock{new Impl(localAddr, backlog)}
{}

SrvrTcpSock::SrvrTcpSock(const int backlog)
    : TcpSock{new Impl(backlog)}
{}

in_port_t SrvrTcpSock::getPort() const noexcept
{
    return static_cast<Impl*>(pImpl.get())->getPort();
}

TcpSock SrvrTcpSock::accept()
{
    return static_cast<Impl*>(pImpl.get())->accept();
}
