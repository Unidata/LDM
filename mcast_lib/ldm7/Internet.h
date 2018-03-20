/**
 * This file declares miscellaneous network-related stuff.
 *
 *        File: net.h
 *  Created on: Feb 2, 2018
 *      Author: steve
 */

#ifndef MCAST_LIB_LDM7_INTERNET_H_
#define MCAST_LIB_LDM7_INTERNET_H_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string>
#include <stdexcept>

enum InetFamily
{
    IPV4 = AF_INET,
    IPV6 = AF_INET6
};

std::string to_string(const in_addr_t addr);

/**
 * Returns the string representation of an IPv4 socket address.
 * @param[in] sockAddr  IPv4 socket address
 * @return              String representation of address
 */
std::string to_string(const struct sockaddr_in& sockAddr);

class InetAddr
{
    struct in_addr structInAddr;

public:
    InetAddr()
        : structInAddr{}
    {}

    inline explicit InetAddr(const InetFamily family)
        : structInAddr{INADDR_ANY}
    {}

    inline explicit InetAddr(const in_addr_t addr)
        : structInAddr{addr}
    {}

    inline InetAddr(const InetAddr& inAddr)
        : structInAddr(inAddr.structInAddr)
    {}

    /**
     * Constructs from a C string.
     * @param[in] addrSpec           IPv4 address specification
     * @throw std::invalid_argument  Address is invalid
     */
    explicit InetAddr(const char* addrSpec);

    /**
     * Constructs from a string.
     * @param[in] addrSpec           IPv4 address specification
     * @throw std::invalid_argument  Address is invalid
     */
    inline explicit InetAddr(const std::string& addrSpec)
        : InetAddr{addrSpec.c_str()}
    {}

    /**
     * Returns the Internet address family.
     * @return Internet address family
     */
    static inline InetFamily getFamily()
    {
        return InetFamily::IPV4;
    }

    /**
     * Creates a socket.
     * @param[in] type           Socket type (e.g., SOCK_STREAM)
     * @param[in] protocol       Protocol (e.g., IPPROTO_TCP)
     * @return                   Socket descriptor
     * @throw std::system_error  Couldn't create socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Safe
     */
    static int socket(
            const int type,
            const int protocol);

    void setSockAddr(
            struct sockaddr& sockAddr,
            const in_port_t  port) const;

    inline std::string to_string() const
    {
        return ::to_string(structInAddr.s_addr);
    }

    inline std::string to_string(const in_port_t port) const
    {
        return to_string() + ":" + std::to_string(port);
    }
};

class InetSockAddr
{
    InetAddr  inAddr;
    in_port_t port;

public:
    inline explicit InetSockAddr(const struct sockaddr_in& sockAddr)
        : inAddr{sockAddr.sin_addr.s_addr}
        , port{ntohs(sockAddr.sin_port)}
    {}

    inline explicit InetSockAddr(
            const InetAddr  inAddr,
            const in_port_t port = 0)
        : inAddr{inAddr}
        , port{port}
    {}

    /**
     * Constructs from the address family. The Internet address will be
     * everything and the port number will be `0`.
     */
    inline InetSockAddr(const InetFamily family)
        : inAddr{family}
        , port{0}
    {}

    inline InetFamily getFamily() const
    {
        return inAddr.getFamily();
    }

    /**
     * Binds the local endpoint to an address.
     * @param[in] sd             Socket descriptor
     * @throw std::system_error  Couldn't bind socket
     * @exceptionsafety          Strong guarantee
     * @threadsafety             Compatible but not safe
     */
    void bind(const int sd) const;

    /**
     * Connects a socket to a remote endpoint.
     * @param[in] sd             Socket descriptor
     * @throw std::system_error  Couldn't connect socket
     * @execptionsafety          Strong guarantee
     * @threadsafety             Safe
     */
    void connect(const int sd) const;

    inline std::string to_string() const
    {
        return inAddr.to_string(port);
    }
};

#endif /* MCAST_LIB_LDM7_INTERNET_H_ */
