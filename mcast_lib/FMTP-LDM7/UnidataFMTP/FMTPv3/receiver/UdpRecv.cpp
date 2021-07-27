/**
 *  Receives multicast FMTP UDP messages
 *
 *        File: UdpRecv.h
 *  Created on: Sep 10, 2020
 *      Author: Steven R. Emmerson
 */

#include "UdpRecv.h"
#ifdef LDM_LOGGING
	#include "log.h"
#endif

#include <arpa/inet.h>
#include <cassert>
#include <cerrno>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <system_error>

UdpRecv::UdpRecv()
    : sd{-1}
    , packet{}
    , verifier{}
    , MIN_PACKET{FMTP_HEADER_LEN + verifier.getSize()}
{}

UdpRecv::UdpRecv(const std::string& srcAddr,
                 const std::string& mcastAddr,
                 const in_port_t    mcastPort,
                 const std::string& ifAddr,
                 const std::string& macKey)
    : sd{::socket(AF_INET, SOCK_DGRAM, 0)}
    , packet{}
    , verifier(macKey)
    , MIN_PACKET{FMTP_HEADER_LEN + verifier.getSize()}
{
    if (sd < 0)
        throw std::system_error(errno, std::system_category(),
                "UdpRecv::UdpRecv() ::socket() failure");

    #ifdef LDM_LOGGING
    	log_debug("Created UDP socket %d", sd);
    #endif

    try {
        struct sockaddr_in groupAddr = {};
        groupAddr.sin_family = AF_INET;
        groupAddr.sin_port = htons(mcastPort);
        groupAddr.sin_addr.s_addr = inet_addr(mcastAddr.c_str());

        if (::bind(sd, (struct sockaddr*) &groupAddr, sizeof(groupAddr)) < 0)
            throw std::system_error(errno, std::system_category(),
                    "UdpRecv::UdpRecv(): Couldn't bind socket " +
                    std::to_string(sd) + " to multicast group " + mcastAddr +
                    ":" + std::to_string(mcastPort));

        struct ip_mreq_source  mreq = {}; // Zero fills
        mreq.imr_multiaddr.s_addr = inet_addr(mcastAddr.c_str());
        mreq.imr_interface.s_addr = inet_addr(ifAddr.c_str());
        mreq.imr_sourceaddr.s_addr = inet_addr(srcAddr.c_str());

        if (::setsockopt(sd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP, &mreq,
                sizeof(mreq)) < 0 )
            throw std::system_error(errno, std::system_category(),
                    "UdpRecv::UdpRecv() Couldn't join multicast group " +
                    mcastAddr + " from source " + srcAddr + " on interface " +
                    ifAddr);
    } // `sd` is open
    catch (...) {
    	::close(sd);
    	throw;
    }
}

UdpRecv::~UdpRecv()
{
	if (sd >= 0) {
        #ifdef LDM_LOGGING
            log_debug("Closing UDP socket %d", sd);
        #endif
        ::close(sd);
	}
}

UdpRecv& UdpRecv::operator=(UdpRecv&& rhs)
{
	sd = std::move(rhs.sd);
	rhs.sd = -1; // To prevent the socket from being closed
	verifier = std::move(rhs.verifier);

	return *this;
}

void UdpRecv::getPacket(FmtpHeader&  header,
                        char** const payload)
{
    for (;;) {
        int  cancelState;
        (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelState);
            const ssize_t nbytes = ::recv(sd, packet.bytes, MAX_FMTP_PACKET, 0);
        (void)pthread_setcancelstate(cancelState, &cancelState);

        if (nbytes == -1)
            throw std::system_error(errno, std::system_category(),
                    "UdpRecv::read() ::recv() failure on socket " +
                    std::to_string(sd));

        header.prodindex  = ntohl(packet.header.prodindex);
        header.seqnum     = ntohl(packet.header.seqnum);
        header.payloadlen = ntohs(packet.header.payloadlen);
        header.flags      = ntohs(packet.header.flags);

        if (nbytes < MIN_PACKET + header.payloadlen) {
            #ifdef LDM_LOGGING
                log_warning("Ignoring too-small FMTP message: "
                        "nbytes=%zd, MIN_PACKET=%u, payload=%u",
                        nbytes, MIN_PACKET, header.payloadlen);
            #endif
        }
        else {
            const unsigned msgLen = FMTP_HEADER_LEN + header.payloadlen;

            if (verifier.verify(packet.bytes, msgLen, packet.bytes+msgLen,
                    nbytes-msgLen)) {
                *payload = packet.payload;
                break;
            }

            #ifdef LDM_LOGGING
                log_warning("Ignoring inauthentic packet");
            #endif
        } // Non-MAC portion of packet has valid length
    } // Packet read loop
}
