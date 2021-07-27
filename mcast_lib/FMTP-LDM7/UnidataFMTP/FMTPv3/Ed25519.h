/**
 * Digital signature module.
 *
 *        File: DigSig.h
 *  Created on: Mar 8, 2021
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_ED25519_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_ED25519_H_

#include <openssl/evp.h>
#include <string>
#include <sys/uio.h>

/**
 * Class for digital signing and verifying.
 */
class Ed25519 final
{
protected:
    EVP_PKEY*   pKey;   ///< Public/private key-pair
    EVP_MD_CTX* mdCtx;  ///< Message digest context
    std::string pubKey; ///< Public key

public:
    static const int SIGLEN = 64; ///< Signature length in bytes

    /**
     * Default constructs. This constructor is appropriate for digital signers.
     *
     * @throw std::runtime_error  Failure
     */
    Ed25519();

    /**
     * Constructs from a public-key returned by `getPubKey()`. This constructor
     * is appropriate for verifiers.
     *
     * @param[in] pubKey          Public-key
     * @throw std::runtime_error  Failure
     * @see DigSigner::getPubKey();
     */
    Ed25519(const std::string& pubKey);

    Ed25519(Ed25519& digSig) =delete;

    Ed25519(Ed25519&& digSig) =delete;

    ~Ed25519();

    Ed25519& operator=(const Ed25519& digSig) =delete;

    Ed25519& operator=(const Ed25519&& digSig) =delete;

    /**
     * Returns a printable version of the public key in a form suitable for
     * construction by `DigSig()`.
     *
     * @return                    Public key
     * @throw std::runtime_error  Failure
     * @see DigVerifier()
     */
    std::string getPubKey() const;

    /**
     * Signs a message.
     *
     * @param[in]     msg         Message to be signed
     * @param[in]     msgLen      Length of message in bytes
     * @param[out]    sig         Message signature
     * @param[in]     sigLen      Length of signature buffer in bytes
     * @return                    Length of signature in bytes
     * @throw std::runtime_error  Failure
     */
    size_t sign(const char*  msg,
                const size_t msgLen,
                char*        sig,
                const size_t sigLen);

    /**
     * Signs a message.
     *
     * @param[in]  msg            Message to be signed
     * @return                    Message signature
     * @throw std::runtime_error  Failure
     */
    std::string sign(const std::string& message);

    /**
     * Verifies a signed message.
     *
     * @param[in]  msg            Message to be verified
     * @param[in]  msgLen         Length of message in bytes
     * @param[in]  sig            Message signature
     * @param[in]  sigLen         Length of signature in bytes
     * @retval     true           Message is verified
     * @retval     false          Message is not verified
     * @throw std::runtime_error  Failure
     */
    bool verify(const char*  msg,
                const size_t msgLen,
                const char*  sig,
                const size_t sigLen);

    /**
     * Verifies a signed message.
     *
     * @param[in]  msg            Message to be verified
     * @param[in]  sig            Message signature
     * @retval     true           Message is verified
     * @retval     false          Message is not verified
     * @throw std::runtime_error  Failure
     */
    bool verify(const std::string& msg,
                const std::string& sig);
};

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_ED25519_H_ */
