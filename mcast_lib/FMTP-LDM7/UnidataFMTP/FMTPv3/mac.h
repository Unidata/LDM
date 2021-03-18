/**
 * Message authentication module
 *
 *        File: mac.h
 *  Created on: Mar 15, 2021
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_MAC_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_MAC_H_

#include <string>
#include <memory>

/**
 * Class for message authentication.
 */
class Mac final
{
public:
    class Impl;

private:
    using Pimpl = std::shared_ptr<Impl>;
    Pimpl pImpl;

public:
    static const char* ENV_NAME; ///< Name of controlling environment variable
    int maxLen;                  ///< Maximum MAC length in bytes

    /**
     * Default constructs. This constructor is appropriate for signers of
     * authenticated messages.
     */
    Mac();

    /**
     * Constructs from a MAC key returned by `getKey()`. This constructor
     * is appropriate for verifiers of authenticated messages.
     *
     * @param[in] key             MAC key
     * @throw std::runtime_error  Failure
     * @see getKey();
     */
    Mac(const std::string& pubKey);

    Mac(Mac& Mac) =delete;

    Mac(Mac&& Mac) =delete;

    virtual ~Mac();

    Mac& operator=(const Mac& Mac) =delete;

    Mac& operator=(const Mac&& Mac) =delete;

    /**
     * Returns the MAC key in a form suitable for construction by
     * `Mac(const std::string&)`.
     *
     * @return                    MAC key
     * @throw std::runtime_error  Failure
     * @see Mac(const std::string& key)
     */
    std::string getKey() const;

    /**
     * Returns the MAC of a message.
     *
     * @param[in]     msg         Message
     * @param[in]     msgLen      Length of message in bytes
     * @param[out]    mac         Buffer for MAC of message
     * @param[in]     macLen      Length of MAC buffer in bytes
     * @return                    Number of bytes written to MAC buffer
     * @throw std::runtime_error  Failure
     */
    size_t getMac(const char*  msg,
                  const size_t msgLen,
                  char*        mac,
                  const size_t macLen) const;

    /**
     * Returns the MAC of a message.
     *
     * @param[in]  msg            Message
     * @return                    MAC of message
     * @throw std::runtime_error  Failure
     */
    std::string getMac(const std::string& msg) const;

    /**
     * Verifies a MAC.
     *
     * @param[in]  msg            Message
     * @param[in]  msgLen         Length of message in bytes
     * @param[in]  mac            MAC to be verified
     * @param[in]  macLen         Length of MAC in bytes
     * @retval     true           MAC is verified
     * @retval     false          MAC is not verified
     * @throw std::runtime_error  Failure
     */
    bool verify(const char*  msg,
                const size_t msgLen,
                const char*  mac,
                const size_t macLen) const;

    /**
     * Verifies a MAC.
     *
     * @param[in]  msg            Message
     * @param[in]  mac            MAC to be verified
     * @retval     true           MAC is verified
     * @retval     false          MAC is not verified
     * @throw std::runtime_error  Failure
     */
    bool verify(const std::string& msg,
                const std::string& mac) const;
};

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_MAC_H_ */
