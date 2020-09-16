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
#include <cerrno>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

UdpRecv::UdpRecv()
	: sd{-1}
	, hmac()
	, header{}
	, haveHeader{false}
{}

UdpRecv::UdpRecv(const std::string& srcAddr,
		         const std::string& mcastAddr,
		         const in_port_t    mcastPort,
				 const std::string& ifAddr,
				 const std::string& hmacKey)
	: sd{::socket(AF_INET, SOCK_DGRAM, 0)}
	, hmac(hmacKey)
	, header{}
	, haveHeader(false)
{
    if (sd < 0)
        throw std::system_error(errno, std::system_category(),
                "UdpRecv::UdpRecv() ::socket() failure");

    #ifdef LDM_LOGGING
    	log_debug("Created socket %d", sd);
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
            log_debug("Closing socket %d", sd);
        #endif
        ::close(sd);
	}
}

UdpRecv& UdpRecv::operator=(UdpRecv&& rhs)
{
	this->sd = rhs.sd;
	rhs.sd = -1; // To prevent the socket from being closed
	this->hmac = std::move(rhs.hmac);
	this->header = std::move(rhs.header);
	this->haveHeader = std::move(rhs.haveHeader);

	return *this;
}

bool UdpRecv::discardPayload()
{
	if (!haveHeader)
		throw std::logic_error("UdpRecv::discard() Discarding unread FMTP "
                "message");

	char buf[FMTP_HEADER_LEN + header.payloadlen];
	auto nbytes = ::read(sd, buf, sizeof(buf));
	if (nbytes == -1)
		throw std::system_error(errno, std::system_category(),
				"UdpRecv::discardPayload() Couldn't read payload");

	haveHeader = false;

	return nbytes == sizeof(buf);
}

const FmtpHeader& UdpRecv::peekHeader()
{
	if (haveHeader)
		throw std::logic_error("UdpRecv::getHeader() Already read FMTP header");

	for (;;) {
		/*
		 * To avoid extra copying operations, recv() is called with a MSG_PEEK
		 * flag to prevent the packet from being removed from the input buffer.
		 * The recv() call will block until a packet arrives.
		 */
		int  cancelState;
		(void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelState);
			const ssize_t nread = ::recv(sd, &header, FMTP_HEADER_LEN, MSG_PEEK);
		(void)pthread_setcancelstate(cancelState, &cancelState);

		if (nread < 0)
			throw std::system_error(errno, std::system_category(),
                    "UdpRecv::read() ::recv() failure on socket " +
					std::to_string(sd));

		if (nread != FMTP_HEADER_LEN) {
			#ifdef LDM_LOGGING
				log_warning("FMTP header is too short");
			#endif
			continue;
		}

		header.prodindex  = ntohl(header.prodindex);
		header.seqnum     = ntohl(header.seqnum);
		header.payloadlen = ntohs(header.payloadlen);
		header.flags      = ntohs(header.flags);
		haveHeader         = true;

		break;
	}

	return header;
}

bool UdpRecv::readPayload(char* payload)
{
	if (!haveHeader)
		throw std::logic_error("UdpRecv::getPayload() FMTP header wasn't read");

	char         buf[FMTP_HEADER_LEN];
	struct iovec iov[2];
	iov[0].iov_base = buf;
	iov[0].iov_len = FMTP_HEADER_LEN;
	iov[1].iov_base = payload;
	iov[1].iov_len = header.payloadlen;

	int cancelState;
	(void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &cancelState);
		const auto nbytes = ::readv(sd, iov, 2);
	(void)pthread_setcancelstate(cancelState, &cancelState);

	if (nbytes < 0)
		throw std::system_error(errno, std::system_category(),
				"UdpRecv::read() ::readv() failure");

	const bool success = (nbytes == FMTP_HEADER_LEN + header.payloadlen);
	if (!success) {
		#ifdef LDM_LOGGING
			log_warning("Payload is too short");
		#endif
	}

	haveHeader = false;

	return success;
}
