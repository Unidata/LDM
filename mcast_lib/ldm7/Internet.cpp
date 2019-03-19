/**
 * This file defines miscellaneous network-related stuff.
 *
 *        File: net.cpp
 *  Created on: Feb 2, 2018
 *      Author: steve
 */

#include "config.h"

#include "Internet.h"
#include "log.h"

#include <cerrno>
#include <system_error>

std::string to_string(const in_addr_t addr)
{
    char dottedQuad[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &addr, dottedQuad, sizeof(dottedQuad));
    return std::string{dottedQuad};
}

std::string to_string(const struct sockaddr_in& sockAddr)
{
    return to_string(sockAddr.sin_addr.s_addr) + ":" +
            std::to_string(ntohs(sockAddr.sin_port));
}

std::string to_string(const struct sockaddr& sockAddr)
{
    if (sockAddr.sa_family == AF_INET)
        return ::to_string(
                *reinterpret_cast<const struct sockaddr_in*>(&sockAddr));

    return "{Unsupported address family, " +
            std::to_string(sockAddr.sa_family) + "}";
}

InetAddr::InetAddr(const char* addrSpec)
    : structInAddr{0}
{
    if (::inet_pton(AF_INET, addrSpec, &structInAddr.s_addr) != 1)
        throw std::invalid_argument(std::string{"Invalid IPv4 address: \""}
                + addrSpec + + "\"");
}

int InetAddr::socket(
        const int type,
        const int protocol)
{
    int sd = ::socket(AF_INET, type, protocol);
    if (sd < 0)
        throw std::system_error(errno, std::system_category(),
                "Couldn't create IPv4 socket of type " + std::to_string(type) +
                ", protocol " + std::to_string(protocol));
    return sd;
}

void InetAddr::setSockAddr(
        struct sockaddr& sockAddr,
        const in_port_t  port) const
{
    auto sockAddrIn = reinterpret_cast<struct sockaddr_in*>(&sockAddr);
    sockAddrIn->sin_family = getFamily();
    sockAddrIn->sin_addr = structInAddr;
    sockAddrIn->sin_port = htons(port);
}

void InetSockAddr::bind(const int sd) const
{
    struct sockaddr sockAddr{};

    inAddr.setSockAddr(sockAddr, port);

    log_debug("Binding socket %d to address %s", sd,
            ::to_string(sockAddr).c_str());

    if (::bind(sd, &sockAddr, sizeof(sockAddr)))
        throw std::system_error{errno, std::system_category(),
                std::string{"Couldn't bind socket "} + std::to_string(sd) +
                " to address " + to_string()};
}

void InetSockAddr::connect(const int sd) const
{
    struct sockaddr sockAddr{};

    inAddr.setSockAddr(sockAddr, port);

    log_debug("Connecting socket %d to %s", sd, to_string().c_str());

    if (::connect(sd, &sockAddr, sizeof(sockAddr)))
        throw std::system_error{errno, std::system_category(),
                std::string{"Couldn't connect socket to remote address "} +
                to_string()};
}
