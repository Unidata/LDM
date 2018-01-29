/**
 * This file implements the FMTP control-plane.
 *
 *        File: ControlPlane.cpp
 *  Created on: Jan 5, 2018
 *      Author: Steven R. Emmerson
 */
#include "config.h"

#include "ControlPlane.h"

#include <cerrno>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

template<class Key>
class ControlPlane<Key>::Impl final
{
    class Client final
    {
        struct sockaddr_un srvrAddr;
        int                sd;
        std::string        clntPathname;

    public:
        Client( const std::string& srvrPathname,
                const std::string& clntPathname)
            : srvrAddr{}
            , sd{::socket(AF_LOCAL, SOCK_SEQPACKET, IPPROTO_SCTP)}
            , clntPathname{clntPathname}
        {
            if (sd < 0)
                throw std::system_error(errno, std::system_category(),
                        "Couldn't create Unix domain socket");
            try {
                // Set address of server's Unix domain socket
                srvrAddr.sun_family = AF_LOCAL;
                ::strncpy(srvrAddr.sun_path, srvrPathname.c_str(),
                        sizeof(srvrAddr.sun_path));
                // Create client-side Unix domain socket
                ::unlink(clntPathname.c_str()); // Failure is acceptable
                struct sockaddr_un clntAddr = {};
                clntAddr.sun_family = AF_LOCAL;
                ::strncpy(clntAddr.sun_path, clntPathname.c_str(),
                        sizeof(clntAddr.sun_path));
                if (::bind(sd, static_cast<struct sockaddr*>(&clntAddr),
                        SUN_LEN(&clntAddr)))
                throw std::system_error(errno, std::system_category(),
                        "Couldn't bind Unix domain socket to " + clntPathname);
            }
            catch (const std::exception& ex) {
                ::close(sd);
            }
        }
    };
    Client clnt;
    static const std::string pathnamePrefix = "/tmp/";

public:
    Impl(const pid_t srvrPid)
        : clnt{pathnamePrefix + "ControlPlaneServer_" + std::to_string(srvrPid),
               pathnamePrefix + "ControlPlaneClient_" +
               std::to_string(::getpid())}
    {}

    ~Impl()
    {
        ::close(sd);
        ::unlink(clntPathname);
    }
};

template<class Key>
ControlPlane<Key>::ControlPlane(Impl& impl)
    : pImpl{&impl}
{}

template<class Key>
ControlPlane ControlPlane<Key>::get()
{
    static ControlPlane<Key>::Impl impl{}; // Singleton instance
    return ControlPlane{impl};
}

template<class Key>
void ControlPlane<Key>::add(
        const Key&            key,
        const struct in_addr& fmtpServerAddr,
        const unsigned        vlanId,
        const std::string&    switchPortId,
        const struct in_addr& minInAddr,
        const struct in_addr& maxInAddr) const
{
    pImpl->add(key, fmtpServerAddr, vlanId, switchPortId, minInAddr, maxInAddr);
}

template<class Key>
void ControlPlane<Key>::get(
        const Key&          key,
        struct sockaddr_in& fmtpServerAddr,
        unsigned &          vlanId,
        std::string&        switchPortId) const
{
    pImpl->get(key, fmtpServerAddr, vlanId, switchPortId);
}

template<class Key>
void ControlPlane<Key>::set(
        const Key&      key,
        const in_port_t port) const
{
    pImpl->set(key, port);
}

template<class Key>
void ControlPlane<Key>::reserve(
        const Key&      key,
        struct in_addr& remoteInAddr) const
{
    pImpl->reserve(key, remoteInAddr);
}

template<class Key>
void ControlPlane<Key>::release(
        const Key&            key,
        const struct in_addr& remoteInAddr) const
{
    pImpl->release(key, remoteInAddr);
}
