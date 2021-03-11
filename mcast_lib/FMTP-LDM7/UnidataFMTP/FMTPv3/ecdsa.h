/**
 * Elliptic curve digital signing algorithm (ECDSA).
 *
 *        File: ecdsa.h
 *  Created on: Mar 8, 2021
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_ECDSA_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_ECDSA_H_

#include <openssl/ec.h>
#include <queue>
#include <string>

/**
 * Base class for ECDSA.
 */
class Ecdsa
{
    using OpenSslErrCode = unsigned long;
    using CodeQ          = std::queue<OpenSslErrCode>;

    EC_KEY* ecKey; ///< Elliptic curve key

    /**
     * Throws a queue of OpenSSL errors as a nested exception. If the queue is
     * empty, then simply returns. Recursive.
     *
     * @param[in,out] codeQ         Queue of OpenSSL error codes. Will be empty
     *                              on return.
     * @throw std::runtime_error    Earliest OpenSSL error
     * @throw std::nested_exception Nested OpenSSL runtime exceptions
     */
    static void throwExcept(CodeQ& codeQ);

protected:
    /**
     * Default constructs.
     */
    Ecdsa();

    /**
     * Throws an OpenSSL error. If a current OpenSSL error exists, then it is
     * thrown as a nested exception; otherwise, a regular exception is thrown.
     *
     * @param msg                     Top-level (non-OpenSSL) message
     * @throw std::runtime_exception  Regular or nested exception
     */
    static void throwOpenSslError(const std::string& msg);

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
    EVP_PKEY* pKey;

public:
    /**
     * Default constructs. A public/private key-pair is chosen at random.
     *
     * @throw std::runtime_error  OpenSSL failure
     */
    EcdsaSigner();

    /**
     * Destroys.
     */
    ~EcdsaSigner();

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
