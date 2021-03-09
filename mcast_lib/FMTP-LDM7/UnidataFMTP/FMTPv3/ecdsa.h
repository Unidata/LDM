/**
 * Elliptic curve digital signing algorithm (ECDSA).
 *
 *        File: ecdsa.h
 *  Created on: Mar 8, 2021
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_ECDSA_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_ECDSA_H_

#include <string>

/**
 * Base class for ECDSA.
 */
class Ecdsa
{
protected:
    /**
     * Default constructs.
     */
    Ecdsa();

    /**
     * Throws a nested runtime exception due to an OpenSSL error.
     *
     * @param[in] msg  Outermost message
     */
    void throwExcept(const std::string& msg) const;

public:
    Ecdsa(Ecdsa& ecdsa) =delete;

    Ecdsa(Ecdsa&& ecdsa) =delete;

    /**
     * Destroys.
     */
    virtual ~Ecdsa();

    Ecdsa& operator=(const Ecdsa& ecdsa) =delete;

    Ecdsa& operator=(const Ecdsa&& ecdsa) =delete;
};

/**
 * A signing ECDSA instance.
 */
class EcdsaSigner final : public Ecdsa
{

public:
    /**
     * Default constructs. A public/private key-pair is chosen at random.
     *
     * @throw std::runtime_error  OpenSSL failure
     */
    EcdsaSigner();

    /**
     * Returns the public key.
     *
     * @return  The ECDSA public key
     */
    const std::string& getPubKey() const noexcept;

    /**
     * Signs a message.
     *
     * @param[in]  message        Message to be signed
     * @param[out] signature      Message signature
     * @throw std::runtime_error  OpenSSL failure
     */
    void sign(const std::string& message,
              std::string&       signature);
};

/**
 * A verifying ECDSA instance.
 */
class EcdsaVerifier final : public Ecdsa
{
public:
    /**
     * Constructs from an ECDSA public-key.
     *
     * @param[in] pubKey          ECDSA public-key
     * @throw std::runtime_error  OpenSSL failure
     */
    EcdsaVerifier(const std::string& pubKey);

    /**
     * Verifies a signed message.
     *
     * @param[in]  message        Message to be verified
     * @param[in]  signature      Message signature
     * @retval     true           Message is verified
     * @retval     false          Message is not verified
     * @throw std::runtime_error  OpenSSL failure
     */
    bool verify(const std::string& message,
                const std::string& signature);
};

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_ECDSA_H_ */
