/**
 * This file defines the remote-procedure-call API for the multicast LDM.
 *
 *        File: MldmRpc.cpp
 *  Created on: Feb 7, 2018
 *      Author: Steven R. Emmerson
 */

#include "config.h"

#include "CidrAddr.h"
#include "log.h"
#include "MldmRpc.h"
#include "TcpSock.h"

#include <chrono>
#include <deque>
#include <fcntl.h>
#include <mutex>
#include <random>
#include <system_error>
#include <unistd.h>
#include <unordered_set>

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
     * @return                   IP address in network byte order
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
     * @throw std::logic_error   `fmtpAddr` wasn't previously reserved
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
        if (tcpSock.read(&ldm7Status, sizeof(ldm7Status)) == 0)
            throw std::system_error(errno, std::system_category(),
                    "Socket was closed");
        if (ldm7Status == LDM7_NOENT)
            throw std::logic_error("IP address " + to_string(fmtpAddr) +
                    " wasn't previously reserved");
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
    catch (const std::logic_error& ex) {
        log_add(ex.what());
        return LDM7_NOENT;
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
 * Pool of Available IP Addresses:
 ******************************************************************************/
class InAddrPool::Impl final
{
    /// Available IP addresses
    std::deque<in_addr_t>           available;
    /// Allocated IP addresses
    std::unordered_set<in_addr_t>   allocated;
    /// Concurrency support
    typedef std::mutex              Mutex;
    typedef std::lock_guard<Mutex>  LockGuard;
    mutable Mutex                   mutex;

public:
    /**
     * Constructs.
     * @param[in] subnet             Subnet specification
     */
    Impl(const CidrAddr& subnet)
        : available{cidrAddr_getNumHostAddrs(&subnet),
                cidrAddr_getAddr(&subnet)}
        , allocated{}
        , mutex{}
    {
        auto size = available.size();
        for (in_addr_t i = 1; i <= size; ++i)
            available[i] |= htonl(i);
    }

    /**
     * Reserves an address.
     * @return                    Reserved address in network byte-order
     * @throw std::out_of_range   No address is available
     * @threadsafety              Safe
     * @exceptionsafety           Strong guarantee
     */
    in_addr_t reserve()
    {
        LockGuard lock{mutex};
        in_addr_t addr = {available.at(0)};
        available.pop_front();
        allocated.insert(addr);
        return addr;
    }

    /**
     * Indicates if an IP address has been previously reserved.
     * @param[in] addr   IP address to check
     * @retval `true`    IP address has been previously reserved
     * @retval `false`   IP address has not been previously reserved
     * @threadsafety     Safe
     * @exceptionsafety  Nothrow
     */
    bool isReserved(const in_addr_t addr) const noexcept
    {
        LockGuard lock{mutex};
        return allocated.find(addr) != allocated.end();
    }

    /**
     * Releases an address so that it can be subsequently reserved.
     * @param[in] addr          Reserved address to be released in network
     *                          byte-order
     * @throw std::logic_error  `addr` wasn't previously reserved
     * @threadsafety            Compatible but not safe
     * @exceptionsafety         Strong guarantee
     */
    void release(const in_addr_t addr)
    {
        LockGuard lock{mutex};
        auto      iter = allocated.find(addr);
        if (iter == allocated.end())
            throw std::logic_error("IP address " + to_string(addr) +
                    " wasn't previously reserved");
        available.push_back(addr);
        allocated.erase(iter);
    }
}; // class InAddrPool::Impl

InAddrPool::InAddrPool(const CidrAddr& subnet)
    : pImpl{new Impl(subnet)}
{}

in_addr_t InAddrPool::reserve() const
{
    return pImpl->reserve();
}

bool InAddrPool::isReserved(const in_addr_t addr) const noexcept
{
    return pImpl->isReserved(addr);
}

void InAddrPool::release(const in_addr_t addr) const
{
    pImpl->release(addr);
}

void* inAddrPool_new(const CidrAddr* subnet)
{
    return new InAddrPool{*subnet};
}

bool inAddrPool_isReserved(void* inAddrPool, const in_addr_t addr)
{
    return static_cast<InAddrPool*>(inAddrPool)->isReserved(addr);
}

void inAddrPool_delete(void* inAddrPool)
{
    delete static_cast<InAddrPool*>(inAddrPool);
}

/******************************************************************************
 * Multicast LDM RPC Server:
 ******************************************************************************/

class MldmSrvr::Impl final
{

    /// Pool of available IP addresses
    InAddrPool                     inAddrPool;
    /// Server's listening socket
    SrvrTcpSock                    srvrSock;
    /// Authentication secret
    uint64_t                       secret;
    typedef std::mutex             Mutex;
    typedef std::lock_guard<Mutex> Lock;
    mutable Mutex                  mutex;
    bool                           stopRequested;

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
            if (sock.read(&clntSecret, sizeof(clntSecret)) == 0)
                throw std::runtime_error("Socket was prematurely closed");
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
        return (connSock.read(&action, sizeof(action)) == 0)
                ? CLOSE_CONNECTION
                : action;
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
            if (connSock.read(&fmtpAddr, sizeof(fmtpAddr)) == 0)
                throw std::runtime_error("Socket was closed");
        }
        catch (const std::exception& ex) {
            log_add(ex.what());
            throw std::runtime_error("Couldn't read IP address to release");
        }
        Ldm7Status ldm7Status;
        try {
            inAddrPool.release(fmtpAddr);
            ldm7Status = LDM7_OK;
        }
        catch (const std::logic_error& ex) {
            ldm7Status = LDM7_NOENT;
        }
        try {
            connSock.write(&ldm7Status, sizeof(ldm7Status));
        }
        catch (const std::exception& ex) {
            log_add(ex.what());
            throw std::runtime_error("Couldn't reply to client");
        }
    }

    /**
     * Indicates if `stop()` has been called.
     * @return `true`  Iff `stop()` has been called
     * @see            `stop()`
     */
    bool done() const
    {
        Lock lock{mutex};
        return stopRequested;
    }

public:
    /**
     * Constructs. Creates a listening server-socket and a file that contains a
     * secret.
     * @param[in] inAddrPool     Pool of available IP addresses
     */
    Impl(InAddrPool& inAddrPool)
        : inAddrPool{inAddrPool}
        , srvrSock{InetSockAddr{InetAddr{"127.0.0.1"}}, 32}
        , secret{initSecret(srvrSock.getPort())}
        , mutex{}
        , stopRequested{false}
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
        while (!done()) {
            try {
                // Performs authentication/authorization
                auto connSock = accept();
                try {
                    for (auto action = getAction(connSock);
                            !done() && action != CLOSE_CONNECTION;
                            action = getAction(connSock)) {
                        switch (action) {
                        case RESERVE_ADDR:
                            reserveAddr(connSock);
                            break;
                        case RELEASE_ADDR:
                            releaseAddr(connSock);
                            break;
                        case CLOSE_CONNECTION:
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

    void stop()
    {
        Lock lock{mutex};
        stopRequested = true;
        srvrSock.close();
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
        InAddrPool& inAddrPool)
    : pImpl{new Impl(inAddrPool)}
{}

in_port_t MldmSrvr::getPort() const noexcept
{
    return pImpl->getPort();
}

void MldmSrvr::operator ()() const
{
    pImpl->operator()();
}

void MldmSrvr::stop()
{
    pImpl->stop();
}

void* mldmSrvr_new(void* inAddrPool)
{
    return new MldmSrvr(*static_cast<InAddrPool*>(inAddrPool));
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
        return LDM7_SYSTEM;
    }
    return LDM7_OK;
}

Ldm7Status mldmSrvr_stop(void* mldmSrvr)
{
    try {
        static_cast<MldmSrvr*>(mldmSrvr)->stop();
    }
    catch (const std::exception& ex) {
        log_add(ex.what());
        return LDM7_SYSTEM;
    }
    return LDM7_OK;
}

void mldmSrvr_delete(void* mldmSrvr)
{
    delete static_cast<MldmSrvr*>(mldmSrvr);
}
