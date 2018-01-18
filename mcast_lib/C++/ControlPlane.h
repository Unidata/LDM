/**
 * This file declares the API for the FMTP control-plane
 *
 *        File: ControlPlaneModule.h
 *  Created on: Jan 2, 2018
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_C___CONTROLPLANE_H_
#define MCAST_LIB_C___CONTROLPLANE_H_

#include <memory>
#include <netinet/in.h>
#include <string>

template<class Key>
class ControlPlane final
{
    class                 Impl;
    std::shared_ptr<Impl> pImpl;

    ControlPlane(Impl& impl);

public:
    /**
     * Returns the singleton instance.
     * @return The singleton instance
     */
    static ControlPlane get();

    /**
     * Adds an entry.
     * @param[in] key             Key
     * @param[in] fmtpServerAddr  Local FMTP server address (NB: no port number)
     * @param[in] vlanId          AL2S VLAN ID
     * @param[in] switchPortId    ID of AL2S switchport
     * @param[in] minInAddr       Minimum address for remote FMTP layer
     * @param[in] maxInAddr       Maximum address for remote FMTP layer
     */
    void add(
            const Key&            key,
            const struct in_addr& fmtpServerAddr,
            const unsigned        vlanId,
            const std::string&    switchPortId,
            const struct in_addr& minInAddr,
            const struct in_addr& maxInAddr) const;

    /**
     * Returns local FMTP server information.
     * @param[in]  key             Key
     * @param[out] fmtpServerAddr  Local FMTP server address. Port number will
     *                             be `0` if `set()` not called.
     * @param[out] vlanId          AL2S VLAN ID
     * @param[out] switchPortId    AL2S switchport ID
     */
    void get(
            const Key&          key,
            struct sockaddr_in& fmtpServerAddr,
            unsigned&           vlanId,
            std::string&        switchPortId) const;

    /**
     * Sets the port number of the local FMTP server.
     * @param[in] key   Key
     * @param[in] port  Port number of local FMTP server
     */
    void set(
            const Key&      key,
            const in_port_t port) const;

    /**
     * Reserves a remote FMTP address.
     * @param[in]  key             Key
     * @param[out] remoteInAddr    Reserved address for remote FMTP layer
     */
    void reserve(
            const Key&      key,
            struct in_addr& remoteInAddr) const;

    /**
     * Releases a remote FMTP address for re-use.
     * @param[in] key           Key
     * @param[in] remoteInAddr  Remote FMTP address to re-use
     */
    void release(
            const Key&            key,
            const struct in_addr& remoteInAddr) const;
};

#endif /* MCAST_LIB_C___CONTROLPLANE_H_ */
