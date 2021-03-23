/**
 * Hash-based message authentication code (HMAC).
 *
 *        File: hmac.h
 *  Created on: Sep 3, 2020
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_HMACIMPL_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_HMACIMPL_H_

#include "PubKeyCrypt.h"

#include <openssl/evp.h>
#include <string>
#include <sys/uio.h>
#include "FMTPv3/FmtpBase.h"

class HmacImpl
{
    std::string key;    ///< HMAC key
    EVP_PKEY*   pkey;   ///< OpenSSL HMAC key
    EVP_MD_CTX* mdCtx;  ///< Message-digest context

    void init(const std::string& key);

public:
    static const int HMAC_SIZE;

    /**
     * Default constructs. A new HMAC key will be pseudo-randomly chosen. This
     * is used by the sender.
     *
     * @throw std::runtime_error  OpenSSL failure
     */
    HmacImpl();

    /**
     * Constructs from an HMAC key. This is used by a receiver.
     *
     * @param[in] key                Key for computing HMAC-s. The length of the
     *                               key must be at least `2*MAC_SIZE`.
     * @throw std::runtime_error     OpenSSL failure
     * @throw std::invalid_argument  `key.size() < 2*MAC_SIZE`
     */
    HmacImpl(const std::string& key);

    HmacImpl(const HmacImpl& hmac) =delete;

    /**
     * Destroys.
     */
    ~HmacImpl();

    HmacImpl& operator=(const HmacImpl& rhs) =delete;

    HmacImpl& operator=(HmacImpl&& rhs);

    /**
     * Returns the key for computing HMAC-s.
     *
     * @return  The HMAC key
     */
    const std::string& getKey() const noexcept {
        return key;
    }

    /**
     * Returns the message authentication code (MAC) of an FMTP message.
     *
     * @param[in]  header         FMTP header
     * @param[in]  payload        FMTP message payload. `header.payloadlen` is
     *                            the length in bytes. Ignored if zero length.
     * @param[out] mac            Computed MAC
     * @throw std::logic_error    `header.payloadlen > 0 && `payload == nullptr`
     * @throw std::runtime_error  OpenSSL failure
     */
    void getMac(const FmtpHeader& header,
                const void*       payload,
                char              mac[MAC_SIZE]);

    /**
     * Returns the string representation of a MAC.
     * @param[in] mac  MAC
     * @return         String representation
     */
    static std::string to_string(const char mac[MAC_SIZE]);

    /**
     * Indicates if HMAC usage is disabled. Usage is disabled if the environment
     * variable `DISABLE_HMAC` is defined and not the empty string,
     *
     * @retval `true`   HMAC usage is disabled
     * @retval `false`  HMAC usage is not disabled
     */
    static bool isDisabled();
};

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_HMACIMPL_H_ */
