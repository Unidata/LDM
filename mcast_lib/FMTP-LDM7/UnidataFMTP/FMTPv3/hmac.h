/**
 * Hash-based message authentication code (HMAC).
 *
 *        File: hmac.h
 *  Created on: Sep 3, 2020
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_HMAC_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_HMAC_H_

// Because `fmtpBase.h` includes this file and defines `FmtpHeader`
typedef struct FmtpPacketHeader FmtpHeader;

#include <openssl/evp.h>

#include <string>
#include <sys/uio.h>

class Hmac
{
	std::string key;   ///< HMAC key
	EVP_PKEY*   pkey;  ///< OpenSSL HMAC key
	EVP_MD_CTX* mdCtx; ///< Message-digest context

	void init(const std::string& key);

public:
	static const unsigned SIZE = 32; ///< HMAC size in bytes

	/**
	 * Default constructs. A new HMAC key will be pseudo-randomly chosen.
	 *
     * @throw std::runtime_error  OpenSSL failure
	 */
	Hmac();

	/**
	 * Constructs from an HMAC key.
	 *
	 * @param[in] key                Key for computing HMAC-s. The length of the
	 *                               key must be at least `2*SIZE`.
     * @throw std::runtime_error     OpenSSL failure
     * @throw std::invalid_argument  `key.size() < 2*SIZE`
	 */
	Hmac(const std::string& key);

	Hmac(const Hmac& hmac) =delete;

	Hmac(const Hmac&& hmac) =delete;

	/**
	 * Destroys.
	 */
	~Hmac();

	Hmac& operator=(const Hmac& rhs) =delete;

	Hmac& operator=(Hmac&& rhs);

	/**
	 * Returns the key for computing HMAC-s.
	 *
	 * @return  The HMAC key
	 */
	const std::string& getKey() const noexcept {
		return key;
	}

	/**
	 * Returns the HMAC of the bytes designated by an I/O vector.
	 *
	 * @param[in] iov   I/O vector
	 * @param[in] nvec  Number of elements in the I/O vector
	 * @return          Corresponding HMAC
	 */
	std::string getHmac(
			const struct iovec* iov,
			unsigned            nvec);

	/**
	 * Returns the HMAC of an FMTP message. The MAC of the FMTP header is not
	 * used in the computation.
	 *
	 * @param[in]  header         FMTP header
	 * @param[in]  payload        FMTP message payload. `header.payloadlen` is
	 *                            the length in bytes. Ignored if zero length or
	 *                            `nullptr`.
	 * @return                    HMAC
	 * @throw std::runtime_error  OpenSSL failure
	 */
	std::string getHmac(
			const FmtpHeader& header,
			const void*       payload);
};

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_HMAC_H_ */
