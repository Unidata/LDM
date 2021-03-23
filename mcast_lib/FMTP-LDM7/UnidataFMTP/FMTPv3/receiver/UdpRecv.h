/**
 *  Receives multicast FMTP UDP messages
 *
 *        File: UdpRecv.h
 *  Created on: Sep 10, 2020
 *      Author: Steven R. Emmerson
 */

#ifndef FMTPV3_RECEIVER_UDPRECV_H_
#define FMTPV3_RECEIVER_UDPRECV_H_

#include "FmtpBase.h"
#include "mac.h"

#include <cstddef>
#include <netinet/in.h>
#include <string>

class UdpRecv
{
    int            sd;         ///< Socket descriptor
    FmtpPacket     packet;     ///< Buffer for FMTP packet
    Mac            verifier;   ///< Verifier of message authentication code
    const unsigned MIN_PACKET; ///< Minimum packet size in bytes

    public:
    /**
     * Default constructs.
     */
    UdpRecv();

    /**
     * Constructs.
     *
     * @param[in] srcAddr        IPv4 address of the source of the multicast
     * @param[in] mcastAddr      Multicast group IPv4 address
     * @param[in] mcastPort      Multicast group port number
     * @param[in] ifAddr         IPv4 address of interface on which to receive
     *                           multicast and retransmitted FMTP messages
     * @param[in] macKey         Message authentication key
     * @throw std::system_error  Socket failure
     */
    UdpRecv(const std::string& srcAddr,
            const std::string& mcastAddr,
            const in_port_t    mcastPort,
            const std::string& ifAddr,
            const std::string& macKey);

    UdpRecv(const UdpRecv& udpRecv) =delete;

    /**
     * Destroys.
     */
    ~UdpRecv();

    UdpRecv& operator=(const UdpRecv& rhs) =delete;

    UdpRecv& operator=(UdpRecv&& rhs);

    /**
     * Returns the next FMTP packet. Blocks until one is available or an error
     * occurs. Skips over invalid FMTP packets. Enables thread cancellation
     * while and only while reading the socket.
     *
     * @param[out] header   FMTP header of the next packet
     * @param[out] payload  FMTP packet payload. Will have `head.payloadlen`
     *                      bytes.
     */
    void getPacket(FmtpHeader& header, char** payload);
};

#endif /* FMTPV3_RECEIVER_UDPRECV_H_ */
