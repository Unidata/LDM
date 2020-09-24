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
#include "HmacImpl.h"

#include <cstddef>
#include <netinet/in.h>
#include <string>

class UdpRecv
{
	int          sd;            ///< Socket descriptor
	HmacImpl     hmacImpl;      ///< HMAC calculator
	FmtpHeader   netHeader;     ///< FMTP header in network byte-order
	char         mac[MAC_SIZE]; ///< MAC in message
	struct iovec iov[3];        ///< FMTP message I/O vector

	/**
	 * Initializes some of the members of this instance.
	 */
	void init();

	/**
	 * Indicates if an FMTP message is valid. Compares the MAC of the message
	 * with the MAC in the message.
	 *
	 * @param[in] header          Associated FMTP header
	 * @param[in] payload         Payload of the FMTP message
	 * @retval    `true`          Message is valid
	 * @retval    `false`         Message is invalid
	 * @throw std::runtime_error  OpenSSL failure
	 */
	bool isValid(const FmtpHeader& header,
			     const void*       payload);

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
	 * Peeks at the header of the current FMTP message. Blocks until one is
	 * available or an error occurs. Skips over messages that can't be an FMTP
	 * header. The message will not be removed from the input buffer. Upon
	 * return, the next call should be to either `getPayload()` or
	 * `discardPayload()`. Enables thread cancellation while and only while
	 * reading the socket.
	 *
	 * @param[out] header        FMTP header
	 * @throw std::system_error  I/O failure
	 * @see   readPayload()
	 * @see   discardPayload()
	 */
	void peekHeader(FmtpHeader& header);

	/**
	 * Returns the payload of the current FMTP message. The message is removed
	 * from the input buffer. Upon return, the next call should be to
	 * `peekHeader()`. Enables thread cancellation while and only while reading
	 * the socket.
	 *
	 * @param[in]  header         Associated FMTP header
	 * @param[out] payload        FMTP message payload. Must be at least
	 *                            `getHeader().payloadlen` long.
	 * @retval     `true`         FMTP message was valid
	 * @retval     `false`        FMTP message was not valid
	 * @throw std::system_error   I/O failure
	 * @see   peekHeader()
	 * @cancellationpoint
	 */
	bool readPayload(const FmtpHeader& header,
			         char*             payload);

	/**
	 * Discards the current FMTP message. Upon return, then next call should be
	 * to `getHeader()`. Enables thread cancellation while and only while
	 * reading the socket.
	 *
	 * @param[in]  header        Associated FMTP header
	 * @throw std::logic_error   Unread FMTP messages is being discarded
	 * @retval     `true`        FMTP message was valid
	 * @retval     `false`       FMTP message was not valid
	 * @throw std::system_error  I/O failure
	 * @see   peekHeader()
	 * @cancellationpoint
	 */
	bool discardPayload(const FmtpHeader& header);
};

#endif /* FMTPV3_RECEIVER_UDPRECV_H_ */
