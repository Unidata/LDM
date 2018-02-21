/**
 * This file defines the remote-procedure-call API for the multicast LDM.
 *
 *        File: MldmRpc.cpp
 *  Created on: Feb 7, 2018
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "log.h"
#include "MldmRpc.h"
#include "TcpSock.h"

#include <chrono>
#include <deque>
#include <fcntl.h>
#include <random>
#include <system_error>
#include <unistd.h>

/**
 * Returns the pathname of the file that contains the authorization secret.
 * @param[in] port   Port number of multicast LDM RPC server
 * @return           Pathname of secret-containing file
 * @exceptionsafety  Nothrow
 * @threadsafety     Safe
 */
static std::string getSecretFilePathname(const in_port_t port) noexcept
{
    const char* dir = ::getenv("HOME");
    if (dir == nullptr)
        dir = "/tmp";
    return dir + std::string{"/MldmRpc_"} + std::to_string(port);
}

/**
 * Returns the shared secret between the multicast LDM RPC server and its client
 * processes on the same system and belonging to the same user.
 * @param port               Port number of authorization server in host
 *                           byte-order
 * @throw std::system_error  Couldn't open secret-file
 * @throw std::system_error  Couldn't read secret from secret-file
 */
static uint64_t getSecret(const in_port_t port)
{
    uint64_t secret;
    const std::string pathname = getSecretFilePathname(port);
    auto fd = ::open(pathname.c_str(), O_RDONLY);
    if (fd < 0)
        throw std::system_error(errno, std::system_category(),
                "Couldn't open multicast LDM RPC secret-file " +
                pathname + " for reading");
    try {
        if (::read(fd, &secret, sizeof(secret)) != sizeof(secret))
            throw std::system_error(errno, std::system_category(),
                    "Couldn't read secret from secret-file " + pathname);
        ::close(fd);
    } // `fd` is open
    catch (const std::exception& ex) {
        ::close(fd);
        throw;
    }
    return secret;
}

/******************************************************************************
 * Multicast LDM RPC Client:
 ******************************************************************************/

class MldmClnt::Impl final
{
    TcpSock tcpSock;

public:
    /**
     * Constructs.
     * @param[in] port           Port number of the relevant multicast LDM RPC
     *                           server in host byte-order.
     * @throw std::system_error  Couldn't connect to server
     * @throw std::system_error  Couldn't get shared secret
     */
    Impl(const in_port_t port)
        : tcpSock{InetSockAddr{InetAddr{"127.0.0.1"}}}
    {
       tcpSock.connect(InetSockAddr{InetAddr{"127.0.0.1"}, port});
       auto secret = getSecret(port);
       tcpSock.write(&secret, sizeof(secret));
    }

    /**
     * Reserves an IP address for a downstream FMTP layer to use as the local
     * endpoint of the TCP connection for data-block recovery.
     * @return                   IP address
     * @throw std::system_error  I/O failure
     * @see   release()
     */
    in_addr_t reserve()
    {
        static const auto action = MldmRpcAct::RESERVE_ADDR;
        tcpSock.write(&action, sizeof(action));
        in_addr_t inAddr;
        tcpSock.read(&inAddr, sizeof(inAddr));
        return inAddr;
    }

    /**
     * Releases an IP address for subsequent reuse.
     * @param[in] fmtpAddr       IP address to be release in network byte-order
     * @throw std::system_error  I/O failure
     * @see   reserve()
     */
    void release(in_addr_t fmtpAddr)
    {
        static auto action = MldmRpcAct::RELEASE_ADDR;
        struct iovec iov[2] = {
                {&action,   sizeof(action)},
                {&fmtpAddr, sizeof(fmtpAddr)}
        };
        tcpSock.writev(iov, 2);
        Ldm7Status ldm7Status;
        tcpSock.read(&ldm7Status, sizeof(ldm7Status));
    }
};

MldmClnt::MldmClnt(const in_port_t port)
    : pImpl{new Impl(port)}
{}

in_addr_t MldmClnt::reserve() const
{
    return pImpl->reserve();
}

void MldmClnt::release(const in_addr_t fmtpAddr) const
{
    pImpl->release(fmtpAddr);
}

void* mldmClnt_new(const in_port_t port)
{
    return new MldmClnt(port);
}

Ldm7Status mldmClnt_reserve(
        void*      mldmClnt,
        in_addr_t* fmtpAddr)
{
    try {
        *fmtpAddr = static_cast<MldmClnt*>(mldmClnt)->reserve();
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        return LDM7_SYSTEM;
    }
    return LDM7_OK;
}

Ldm7Status mldmClnt_release(
        void*           mldmClnt,
        const in_addr_t fmtpAddr)
{
    try {
        static_cast<MldmClnt*>(mldmClnt)->release(fmtpAddr);
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        return LDM7_SYSTEM;
    }
    return LDM7_OK;
}

void mldmClnt_delete(void* mldmClnt)
{
    delete static_cast<MldmClnt*>(mldmClnt);
}

/******************************************************************************
 * Multicast LDM RPC Server:
 ******************************************************************************/

class MldmSrvr::Impl final
{
    class InAddrPool final
    {
        /// Available IP addresses
        std::deque<in_addr_t> pool;

        /**
         * Returns the number of IPv4 addresses in a subnet -- excluding the
         * network identifier address (all host bits off) and broadcast address
         * (all host bits on).
         * @param[in] prefixLen          Length of network prefix in bits
         * @return                       Number of addresses
         * @throw std::invalid_argument  `prefixLen >= 31`
         * @threadsafety                 Safe
         */
        static in_addr_t getNumAddrs(const unsigned prefixLen)
        {
            if (prefixLen >= 31)
                throw std::invalid_argument("Invalid network prefix length: " +
                        std::to_string(prefixLen));
            return (1 << (32 - prefixLen)) - 2;
        }

    public:
        /**
         * Constructs.
         * @param[in] networkPrefix      Network prefix in network byte-order
         * @param[in] prefixLen          Number of bits in network prefix
         * @throw std::invalid_argument  `prefixLen >= 31`
         * @throw std::invalid_argument  `networkPrefix` and `prefixLen` are
         *                               incompatible
         */
        InAddrPool(
                const in_addr_t networkPrefix,
                const unsigned  prefixLen)
            : pool{getNumAddrs(prefixLen), networkPrefix}
        {
            if (ntohl(networkPrefix) & ((1ul<<(32-prefixLen))-1)) {
                char dottedQuad[INET_ADDRSTRLEN];
                throw std::invalid_argument(std::string("Network prefix ") +
                        inet_ntop(AF_INET, &networkPrefix, dottedQuad,
                                sizeof(dottedQuad)) +
                                " is incompatible with prefix length " +
                                std::to_string(prefixLen));
            }
            auto size = pool.size();
            for (in_addr_t i = 1; i <= size; ++i)
                pool[i] |= htonl(i);
        }

        /**
         * Reserves an address.
         * @return                    Reserved address in network byte-order
         * @throw std::out_of_range   No address is available
         * @threadsafety              Compatible but not safe
         * @exceptionsafety           Strong guarantee
         */
        in_addr_t reserve()
        {
            in_addr_t addr = {pool.at(0)};
            pool.pop_front();
            return addr;
        }

        /**
         * Releases an address so that it can be subsequently reserved.
         * @param[in] addr          Reserved address to be released in network
         *                          byte-order
         * @threadsafety            Compatible but not safe
         * @exceptionsafety         Strong guarantee
         */
        void release(const in_addr_t addr)
        {
            pool.push_back(addr);
        }
    }; // class InAddrPool

    /// Pool of available IP addresses
    InAddrPool   inAddrPool;
    /// Server's listening socket
    SrvrTcpSock  srvrSock;
    /// Authentication secret
    uint64_t     secret;

    /**
     * Creates the secret that's shared between the multicast LDM RPC server and
     * its client processes on the same system and belonging to the same user.
     * @param port               Port number of server in host byte-order
     * @return                   Secret value
     * @throw std::system_error  Couldn't open secret-file
     * @throw std::system_error  Couldn't write secret to secret-file
     */
    static uint64_t initSecret(const in_port_t port)
    {
        const std::string pathname = getSecretFilePathname(port);
        auto fd = ::open(pathname.c_str(), O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
        if (fd < 0)
            throw std::system_error(errno, std::system_category(),
                    "Couldn't open multicast LDM RPC secret-file " +
                    pathname + " for writing");
        uint64_t secret;
        try {
            auto seed = std::chrono::high_resolution_clock::now()
                .time_since_epoch().count();
            secret = std::mt19937_64{seed}();
            if (::write(fd, &secret, sizeof(secret)) != sizeof(secret))
                throw std::system_error(errno, std::system_category(),
                        "Couldn't write secret to secret-file " + pathname);
            ::close(fd);
        } // `fd` is open
        catch (const std::exception& ex) {
            ::close(fd);
            throw;
        }
        return secret;
    }

    /**
     * Accepts an incoming connection. Reads the shared secret and verifies it.
     * @return                    Connection socket
     * @throw std::runtime_error  Couldn't read shared secret
     * @throw std::runtime_error  Invalid shared secret
     * @throw std::system_error   `accept(2)` failure
     */
    TcpSock accept()
    {
        auto     sock = srvrSock.accept();
        uint64_t clntSecret;
        try {
            sock.read(&clntSecret, sizeof(clntSecret));
        }
        catch (const std::exception& ex) {
            log_add(ex.what());
            throw std::runtime_error("Couldn't read shared secret from socket "
                    + sock.to_string());
        }
        if (clntSecret != secret) {
            throw std::runtime_error("Invalid secret read from socket "
                    + sock.to_string());
        }
        return sock;
    }

    MldmRpcAct getAction(TcpSock& connSock)
    {
        MldmRpcAct action;
        connSock.read(&action, sizeof(action));
        return action;
    }

    /**
     * Reserves an IP address for use by a remote FMTP layer.
     * @param[in] connSock        Connection socket
     * @throw std::out_of_range   No address is available
     * @throw std::system_error   I/O failure
     */
    void reserveAddr(TcpSock& connSock)
    {
        auto fmtpAddr = inAddrPool.reserve();
        try {
            connSock.write(&fmtpAddr, sizeof(fmtpAddr));
        }
        catch (const std::exception& ex) {
            inAddrPool.release(fmtpAddr);
            log_add(ex.what());
            throw std::system_error(errno, std::system_category(),
                    "Couldn't reply to client");
        }
    }

    /**
     * Releases the IP address used by a remote FMTP layer.
     * @param[in] connSock        Connection socket
     * @throw std::runtime_error  I/O error
     */
    void releaseAddr(TcpSock& connSock)
    {
        in_addr_t fmtpAddr;
        try {
            connSock.read(&fmtpAddr, sizeof(fmtpAddr));
        }
        catch (const std::exception& ex) {
            log_add(ex.what());
            throw std::runtime_error("Couldn't read IP address to release");
        }
        inAddrPool.release(fmtpAddr);
        try {
            Ldm7Status ldm7Status = LDM7_OK;
            connSock.write(&ldm7Status, sizeof(ldm7Status));
        }
        catch (const std::exception& ex) {
            log_add(ex.what());
            throw std::runtime_error("Couldn't reply to client");
        }
    }

public:
    /**
     * Constructs. Creates a listening server-socket and a file that contains a
     * secret.
     * @param[in] networkPrefix  Prefix for IP addresses in network byte-order
     * @param[in] prefixLen      Number of bits in network prefix
     */
    Impl(   const in_addr_t networkPrefix,
            const unsigned  prefixLen)
        : inAddrPool{networkPrefix, prefixLen}
        , srvrSock{InetSockAddr{InetAddr{"127.0.0.1"}}, 32}
        , secret{initSecret(srvrSock.getPort())}
    {}

    /**
     * Destroys. Removes the secret-file.
     */
    ~Impl() noexcept
    {
        ::unlink(getSecretFilePathname(srvrSock.getPort()).c_str());
    }

    /**
     * Runs the server. Doesn't return unless a fatal exception is thrown.
     * @throw std::system_error   `accept()` failure
     */
    void operator()()
    {
        for (;;) {
            try {
                // Performs authentication/authorization
                auto connSock = accept();
                try {
                    for (;;) {
                        auto action = getAction(connSock);
                        switch (action) {
                        case RESERVE_ADDR:
                            reserveAddr(connSock);
                            break;
                        case RELEASE_ADDR:
                            releaseAddr(connSock);
                            break;
                        default:
                            throw std::logic_error("Invalid RPC action: " +
                                    std::to_string(action));
                        }
                    } // Individual client transaction
                }
                catch (const std::exception& ex) {
                    log_add(ex.what());
                    log_notice("Couldn't serve client %s",
                            connSock.to_string().c_str());
                }
            }
            catch (const std::system_error& ex) {
                throw; // Fatal error
            }
            catch (const std::exception& ex) {
                log_notice(ex.what()); // Non-fatal error
            }
        } // Individual client session
    }

    /**
     * Returns the port number of the server.
     * @return Port number of server in host byte-order
     */
    in_port_t getPort() const noexcept
    {
        return srvrSock.getPort();
    }
};

MldmSrvr::MldmSrvr(
        const in_addr_t networkPrefix,
        const unsigned  prefixLen)
    : pImpl{new Impl(networkPrefix, prefixLen)}
{}

in_port_t MldmSrvr::getPort() const noexcept
{
    return pImpl->getPort();
}

void MldmSrvr::operator ()() const
{
    pImpl->operator()();
}

void* mldmSrvr_new(
        const in_addr_t networkPrefix,
        const unsigned  prefixLen)
{
    return new MldmSrvr(networkPrefix, prefixLen);
}

in_port_t mldmSrvr_getPort(void* mldmSrvr)
{
    return static_cast<MldmSrvr*>(mldmSrvr)->getPort();
}

Ldm7Status mldmSrvr_run(void* mldmSrvr)
{
    try {
        static_cast<MldmSrvr*>(mldmSrvr)->operator()();
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
    }
    return LDM7_SYSTEM;
}

void mldmSrvr_delete(void* mldmSrvr)
{
}
