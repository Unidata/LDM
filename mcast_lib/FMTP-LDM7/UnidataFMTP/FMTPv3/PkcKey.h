/**
 * Session key cryptography.
 *
 *        File: PkcKey.h
 *  Created on: Sep 2, 2020
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_PKC_KEY_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_PKC_KEY_H_

#include <cstdint>
#include <string>

#include <openssl/rsa.h>

class PkcKey
{
protected:
    RSA*              rsa;
    static const int  padding = RSA_PKCS1_OAEP_PADDING;

    static void initRandom(const int numBytes);

    /**
     * Default constructs.
     */
    PkcKey();

public:
    PkcKey(PkcKey& crypt) =delete;

    PkcKey(PkcKey&& crypt) =delete;

    /**
     * Destroys.
     */
    ~PkcKey();

    PkcKey& operator=(const PkcKey& crypt) =delete;

    PkcKey& operator=(const PkcKey&& crypt) =delete;

    /**
     * Encrypts plaintext.
     *
     * @param[in]  plainText      Plain text
     * @param[out] cipherText     Encrypted text
     * @throw std::runtime_error  OpenSSL failure
     */
    void encrypt(const std::string& plainText,
                 std::string&       cypherText) const =0;

    /**
     * Decrypts ciphertext.
     *
     * @param[in]  cipherText     Encrypted text
     * @param[out] plainText      Plain text
     * @throw std::runtime_error  OpenSSL failure
     */
    void decrypt(const std::string& cipherText,
                 std::string&       plainText) const =0;
};

/**
 * Public key.
 */
class PublicKey final : public PkcKey
{
    std::string pubKey;

public:
    /**
     * Default constructs. A public/private key-pair is chosen at random.
     *
     * @throw std::runtime_error  OpenSSL failure
     */
    PublicKey();

    /**
     * Constructs from a public-key certificate.
     *
     * @param[in] pubKey          X.509 public-key certificate in PEM format
     * @throw std::runtime_error  OpenSSL failure
     * @throw std::runtime_error  OpenSSL failure
     */
    PublicKey(const std::string& pubKey);

    /**
     * Returns the public key.
     *
     * @return  The public key
     */
    const std::string& getPubKey() const noexcept;

    /**
     * Encrypts plaintext.
     *
     * @param[in]  plainText      Plain text
     * @param[out] cipherText     Encrypted text
     * @throw std::runtime_error  OpenSSL failure
     */
    void encrypt(const std::string& plainText,
                 std::string&       cypherText) const override;

    /**
     * Decrypts ciphertext.
     *
     * @param[in]  cipherText     Encrypted text
     * @param[out] plainText      Plain text
     * @throw std::runtime_error  OpenSSL failure
     */
    void decrypt(const std::string& cipherText,
                 std::string&       plainText) const override;
};

/**
 * Private key.
 */
class PrivateKey final : public PkcKey
{
public:
    /**
     * Constructs.
     *
     * @param[in] privateKey      X.509 private-key certificate in PEM format
     * @throw std::runtime_error  OpenSSL failure
     */
    PrivateKey(const std::string& privateKey);

    /**
     * Encrypts plaintext.
     *
     * @param[in]  plainText      Plain text
     * @param[out] cipherText     Encrypted text
     * @throw std::runtime_error  OpenSSL failure
     */
    void encrypt(const std::string& plainText,
                 std::string&       cypherText) const override;

    /**
     * Decrypts ciphertext.
     *
     * @param[in]  cipherText     Encrypted text
     * @param[out] plainText      Plain text
     * @throw std::runtime_error  OpenSSL failure
     */
    void decrypt(const std::string& cipherText,
                 std::string&       plainText) const override;
};

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_PKC_KEY_H_ */
