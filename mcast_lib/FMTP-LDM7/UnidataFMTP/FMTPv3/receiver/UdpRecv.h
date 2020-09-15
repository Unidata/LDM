/**
 *  Receives multicast FMTP UDP messages
 *
 *        File: UdpRecv.h
 *  Created on: Sep 10, 2020
 *      Author: Steven R. Emmerson
 */

#ifndef FMTPV3_RECEIVER_UDPRECV_H_
#define FMTPV3_RECEIVER_UDPRECV_H_

#include "fmtpBase.h"
#include "hmac.h"

#include <cstddef>
#include <netinet/in.h>
#include <string>

class UdpRecv
{
	int        sd;          ///< Socket descriptor
	Hmac       hmac;        ///< HMAC calculator
	FmtpHeader header;      ///< FMTP header
	bool       haveHeader;  ///< `header` is set?

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
	 * @param[in] hmacKey        HMAC key
     * @throw std::system_error  Socket failure
	 */
	UdpRecv(const std::string& srcAddr,
            const std::string& mcastAddr,
            const in_port_t    mcastPort,
			const std::string& ifAddr,
			const std::string& hmacKey);

	UdpRecv(const UdpRecv& udpRecv) =delete;

	/**
	 * Destroys.
	 */
	~UdpRecv();

	UdpRecv& operator=(const UdpRecv& rhs) =delete;

	UdpRecv& operator=(UdpRecv&& rhs);

	/**
	 * Returns the header of the next, valid, FMTP message. The message will not
	 * be removed from the input buffer. Upon return, the next call should be to
	 * either `getPayload()` or `discardPayload()`. Enables thread cancellation
	 * while and only while reading the socket.
	 *
	 * @return                   FMTP header
	 * @throw std::logic_error   Header was already read
	 * @throw std::system_error  I/O failure
	 * @see   getPayload()
	 * @see   discardPayload()
	 */
	const FmtpHeader& peekHeader();

	/**
	 * Returns the payload of the current FMTP message (the FMTP header is
	 * skipped-over). The message is removed from the input buffer. Upon return,
	 * the next call should be to `getHeader()`. Enables thread cancellation
	 * while and only while reading the socket.
	 *
	 * @param[out] payload        FMTP message payload. Must be at least
	 *                            `getHeader().payloadlen` long.
	 * @retval     `true`         FMTP message was valid
	 * @retval     `false`        FMTP message was not valid
	 * @throw std::logic_error    The FMTP header hasn't been read
	 * @throw std::system_error   I/O failure
	 * @see   getHeader()
	 */
	bool readPayload(char* payload);

	/**
	 * Discards the current FMTP message. Upon return, then next call should be
	 * to `getHeader()`. Enables thread cancellation while and only while
	 * reading the socket.
	 *
	 * @throw std::logic_error   Unread FMTP messages is being discarded
	 * @retval     `true`        FMTP message was valid
	 * @retval     `false`       FMTP message was not valid
	 * @throw std::system_error  I/O failure
	 * @see   getHeader()
	 */
	bool discardPayload();
};

#endif /* FMTPV3_RECEIVER_UDPRECV_H_ */
