/**
 * Session key cryptography.
 *
 *        File: SessKeyCrypt.h
 *  Created on: Sep 2, 2020
 *      Author: Steven R. Emmerson
 */

#ifndef MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SESSKEYCRYPT_H_
#define MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SESSKEYCRYPT_H_

#include <cstdint>
#include <string>

#include <openssl/rsa.h>

class SessKeyCrypt
{
protected:
	RSA*              rsa;
	static const int  padding = RSA_PKCS1_OAEP_PADDING;

	static void initRandom(const int numBytes);

public:
	/**
	 * Default constructs.
	 */
	SessKeyCrypt();

	SessKeyCrypt(SessKeyCrypt& crypt) =delete;

	SessKeyCrypt(SessKeyCrypt&& crypt) =delete;

	/**
	 * Destroys.
	 */
	~SessKeyCrypt();

	SessKeyCrypt& operator=(const SessKeyCrypt& crypt) =delete;

	SessKeyCrypt& operator=(const SessKeyCrypt&& crypt) =delete;
};

/**
 * Decrypts a publisher's session key using a subscriber's private key
 */
class Decryptor final : public SessKeyCrypt
{
    std::string pubKey;

    public:
    /**
     * Default constructs. A public/private key-pair is chosen at random.
     *
     * @throw std::runtime_error  OpenSSL failure
     */
    Decryptor();

    /**
     * Destroys.
     */
    ~Decryptor() =default;

    /**
     * Returns the public key.
     *
     * @return  The public key
     */
    const std::string& getPubKey() const noexcept;

    /**
     * Decrypts a publisher's encrypted session key using the subscriber's
     * private key.
     *
     * @param[in] cipherText      Encrypted session key
     * @return                    Session key
     * @throw std::runtime_error  OpenSSL failure
     */
    std::string decrypt(const std::string& cipherText) const;
};

/**
 * Encrypts using an asymmetric key
 */
class Encryptor final : public SessKeyCrypt
{
public:
    /**
     * Constructs.
     *
     * @param[in] pubKey          Subscriber's X.509 public-key certificate in
     *                            PEM format
     * @throw std::runtime_error  OpenSSL failure
     */
    Encryptor(const std::string& pubKey);

    /**
     * Encrypts a publisher's session key using the subscriber's public key.
     *
     * @param[in] sessKey         Session key to be encrypted
     * @throw std::runtime_error  OpenSSL failure
     */
    std::string encrypt(const std::string& sessKey) const;
};

#endif /* MCAST_LIB_FMTP_LDM7_UNIDATAFMTP_FMTPV3_SESSKEYCRYPT_H_ */
