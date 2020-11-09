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

void UdpRecv::init()
{
    iov[0].iov_base = &netHeader;
    iov[0].iov_len  = FMTP_HEADER_LEN;
    iov[2].iov_base = mac;
    iov[2].iov_len  = sizeof(mac);
}

bool UdpRecv::isValid(const FmtpHeader& header,
		              const void*       payload)
{
	char compMac[MAC_SIZE];
	hmacImpl.getMac(header, payload, compMac);

	const bool valid = ::memcmp(mac, compMac, MAC_SIZE) == 0;
    #ifdef LDM_LOGGING
        if (!valid)
            log_warning("Invalid FMTP message: flags=%#x, prodindex=%u, "
            		"seqnum=%u, payloadlen=%u, msgMac=%s, compMap=%s",
            		(unsigned)header.flags, (unsigned)header.prodindex,
					(unsigned)header.seqnum, (unsigned)header.payloadlen,
            		HmacImpl::to_string(mac).c_str(),
            		HmacImpl::to_string(compMac).c_str());
    #endif

	return valid;
}

void UdpRecv::skipPacket() const
{
    char buf[1];
    if (::recv(sd, buf, sizeof(buf), 0) != sizeof(buf)) // Removes packet
        throw std::system_error(errno, std::system_category(),
                "UdpRecv::skipPacket() ::recv() failure on socket " +
                std::to_string(sd));
}

UdpRecv::UdpRecv()
	: sd{-1}
	, hmacImpl()
	, netHeader{}
	, mac{}
	, iov{}
{
	init();
}

UdpRecv::UdpRecv(const std::string& srcAddr,
		         const std::string& mcastAddr,
		         const in_port_t    mcastPort,
				 const std::string& ifAddr,
				 const std::string& hmacKey)
	: sd{::socket(AF_INET, SOCK_DGRAM, 0)}
	, hmacImpl(hmacKey)
	, netHeader{}
	, mac{}
	, iov{}
{
    init();

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
	init(); // Otherwise `msgIov` wouldn't be correctly initialized

	sd = std::move(rhs.sd);
	rhs.sd = -1; // To prevent the socket from being closed
	hmacImpl = std::move(rhs.hmacImpl);
	netHeader = std::move(rhs.netHeader);

	return *this;
}

void UdpRecv::peekHeader(FmtpHeader& header)
{
	for (;;) {
		/*
		 * `::recv()` is called with a MSG_PEEK flag to prevent the packet from
		 * being removed from the input buffer. The call will block until a
		 * packet arrives.
		 */
		int  cancelState;
		(void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelState);
			const ssize_t nbytes = ::recv(sd, &netHeader, FMTP_HEADER_LEN, MSG_PEEK);
		(void)pthread_setcancelstate(cancelState, &cancelState);

		if (nbytes == -1)
			throw std::system_error(errno, std::system_category(),
                    "UdpRecv::read() ::recv() failure on socket " +
					std::to_string(sd));

		if (nbytes != FMTP_HEADER_LEN) {
			#ifdef LDM_LOGGING
				log_warning("Ignoring FMTP message with short header: "
						"nbytes=%zd", nbytes);
			#endif
			skipPacket();
		}
		else {
            header.prodindex  = ntohl(netHeader.prodindex);
            header.seqnum     = ntohl(netHeader.seqnum);
            header.payloadlen = ntohs(netHeader.payloadlen);
            header.flags      = ntohs(netHeader.flags);

            if (header.payloadlen > MAX_FMTP_PAYLOAD) {
                #ifdef LDM_LOGGING
                    log_warning("Ignoring FMTP message with too large payload: "
                            "payloadlen=%u",
                            static_cast<unsigned>(header.payloadlen));
                #endif
                skipPacket();
            }
            else {
                break;
            } // Payload isn't too large
		} // Header is correct length
	} // Packet read loop
}

bool UdpRecv::readPayload(const FmtpHeader& header,
		                  char*             payload)
{
	iov[1].iov_base = payload;
	iov[1].iov_len  = header.payloadlen;

	int cancelState;
	(void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelState);
		const auto nbytes = ::readv(sd, iov, sizeof(iov)/sizeof(iov[0]));
	(void)pthread_setcancelstate(cancelState, &cancelState);

	if (nbytes == -1)
		throw std::system_error(errno, std::system_category(),
				"UdpRecv::read() ::readv() failure");

	bool valid = (nbytes == FMTP_HEADER_LEN + header.payloadlen +
			sizeof(mac));
	if (!valid) {
        #ifdef LDM_LOGGING
			log_warning("Small FMTP payload: expected=%zu, actual=%zd",
					(size_t)header.payloadlen, nbytes);
        #endif
	}
	else {
		valid = isValid(header, payload);
	}

	return valid;
}

bool UdpRecv::discardPayload(const FmtpHeader& header)
{
	char payload[MAX_FMTP_PAYLOAD];
	return readPayload(header, payload);
}
