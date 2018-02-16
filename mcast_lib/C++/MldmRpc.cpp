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

/******************************************************************************
 * Multicast LDM RPC Client:
 ******************************************************************************/

class MldmClnt::Impl final
{
    TcpSock tcpSock;

public:
    /**
     * Constructs.
     * @param[in] port  Port number of the relevant multicast LDM RPC server in
     *                  host byte-order.
     */
    Impl(const in_port_t port)
        : tcpSock{InetSockAddr{InetAddr{"127.0.0.1"}}}
    {
       tcpSock.connect(InetSockAddr{InetAddr{"127.0.0.1"}, port});
       // TODO: Send secret
    }

    /**
     * Reserves an IP address for a downstream FMTP layer for its TCP connection
     * for recovering data-blocks.
     * @return  IP address
     */
    in_addr_t reserve()
    {
        tcpSock.w
        struct in_addr addr{};
        return addr;
    }

    void release(const struct in_addr& fmtpAddr)
    {
        // TODO
    }
};

MldmClnt::MldmClnt(const in_port_t port)
    : pImpl{new Impl(port)}
{}

struct in_addr MldmClnt::reserve() const
{
    return pImpl->reserve();
}

void MldmClnt::release(const struct in_addr& fmtpAddr) const
{
    return pImpl->release(fmtpAddr);
}

void* mldmClnt_new(const in_port_t port)
{
    return new MldmClnt(port);
}

Ldm7Status mldmClnt_reserve(
        void*           mldmClnt,
        struct in_addr* fmtpAddr)
{
    try {
        *fmtpAddr = static_cast<MldmClnt*>(mldmClnt)->reserve();
    }
    catch (const std::exception& ex) {
        log_error(ex.what());
        return LDM7_SYSTEM;
    }
    return LDM7_OK;
}

Ldm7Status mldmClnt_release(
        void*                 mldmClnt,
        const struct in_addr* fmtpAddr)
{
    try {
        static_cast<MldmClnt*>(mldmClnt)->release(*fmtpAddr);
    }
    catch (const std::exception& ex) {
        log_error(ex.what());
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
         * (all host bits bits on).
         * @param[in] prefixLen          Length of network prefix in bits
         * @return                       Number of addresses
         * @throw std::invalid_argument  `prefixLen >= 31`
         * @threadsafety                 Safe
         */
        static std::size_t getNumAddrs(const unsigned prefixLen)
        {
            if (prefixLen >= 31)
                throw std::invalid_argument("Invalid network prefix length: " +
                        std::to_string(prefixLen));
            return (1 << (32 - prefixLen)) - 2;
        }

    public:
        /**
         * Constructs.
         * @param[in] networkPrefix      prefix in network byte-order
         * @param[in] prefixLen          Number of bits in network prefix
         * @throw std::invalid_argument  `prefixLen >= 31`
         * @throw std::invalid_argument  `networkPrefix` and `prefixLen` are
         *                               incompatible
         */
        InAddrPool(
                const in_addr   networkPrefix,
                const unsigned  prefixLen)
            : pool{getNumAddrs(prefixLen), networkPrefix.s_addr}
        {
            if (ntohl(networkPrefix.s_addr) & ((1ul<<(32-prefixLen))-1)) {
                char dottedQuad[INET_ADDRSTRLEN];
                throw std::invalid_argument(std::string("Network prefix ") +
                        inet_ntop(AF_INET, &networkPrefix.s_addr, dottedQuad,
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

    InAddrPool   inAddrPool;
    /// Server's listening socket
    SrvrTcpSock  srvrSock;
    /// Authentication secret
    uint64_t     secret;
    Authorizer   authDb;

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

public:
    /**
     * Constructs. Creates a listening server-socket and a file that contains a
     * secret.
     * @param[in] authDb  Authorization database to use
     */
    Impl(Authorizer& authDb)
        : srvrSock{InetSockAddr{InetAddr{"127.0.0.1"}}, 32}
        , secret{initSecret(srvrSock.getPort())}
        , authDb{authDb}
    {}

    /**
     * Destroys. Removes the secret-file.
     */
    ~Impl() noexcept
    {
        ::unlink(getSecretFilePathname(srvrSock.getPort()).c_str());
    }

    /**
     * Runs the server. Doesn't return unless an exception is thrown.
     * @throw std::system_error   `accept()` failure
     * @throw std::runtime_error  Couldn't authorize FMTP client
     */
    void operator()()
    {
        for (;;) {
            auto      connSock = srvrSock.accept();
            uint64_t  clntSecret;
            in_addr_t fmtpAddr;
            struct iovec iov[2];
            iov[0].iov_base = &clntSecret;
            iov[0].iov_len = sizeof(clntSecret);
            iov[1].iov_base = &fmtpAddr;
            iov[1].iov_len = sizeof(fmtpAddr);
            try {
                connSock.readv(iov, 2);
            }
            catch (const std::exception& ex) {
                log_add(ex.what());
                log_notice("Couldn't read authorization request from "
                        "socket %s. Ignoring request.");
                continue;
            }
            if (clntSecret != secret) {
                log_notice("Invalid secret read from socket %s. "
                        "Ignoring authorization request.",
                        connSock.to_string().c_str());
            }
            else {
                try {
                    struct in_addr addr = {fmtpAddr};
                    authDb.authorize(addr);
                    Ldm7Status ldm7Status = LDM7_OK;
                    try {
                        connSock.write(&ldm7Status, sizeof(ldm7Status));
                    }
                    catch (const std::exception& ex) {
                        log_notice(ex.what());
                        log_notice("Couldn't reply to authorization request "
                                " on socket %s", connSock.to_string().c_str());
                    }
                }
                catch (const std::exception& ex) {
                    std::throw_with_nested(std::runtime_error(
                            std::string{"Couldn't authorize FMTP client "} +
                            ::to_string(fmtpAddr)));
                }
            }
        }
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

MldmSrvr::MldmSrvr(Authorizer& authDb)
    : pImpl{new Impl(authDb)}
{}

in_port_t MldmSrvr::getPort() const noexcept
{
    return pImpl->getPort();
}

void MldmSrvr::operator ()() const
{
    pImpl->operator()();
}

void* mldmSrvr_new(void* authDb)
{
    return new MldmSrvr(*static_cast<Authorizer*>(authDb));
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
