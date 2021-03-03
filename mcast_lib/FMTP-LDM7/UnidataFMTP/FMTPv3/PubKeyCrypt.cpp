/**
 * Public-key cryptography.
 *
 *        File: PubKeyCrypt.cpp
 *  Created on: Sep 2, 2020
 *      Author: Steven R. Emmerson
 */
#include "FmtpConfig.h"

#include "SslHelp.h"
#include "PubKeyCrypt.h"

#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <exception>
#include <string>
#include <system_error>

PubKeyCrypt::PubKeyCrypt()
#if USE_RSA
    : rsa{nullptr}
    , rsaSize{-1}
#else
#endif
{}

PubKeyCrypt::~PubKeyCrypt()
{
#if USE_RSA
    ::RSA_free(rsa);
#else
#endif
}

PublicKey::PublicKey(const std::string& pubKey)
	: PubKeyCrypt{}
{
#   if USE_RSA
        BIO* bio = BIO_new_mem_buf(pubKey.data(), pubKey.length());
        if (bio == nullptr)
            throw std::runtime_error("BIO_new_mem_buf() failure. "
                    "Code=" + std::to_string(ERR_get_error()));

        try {
            rsa = PEM_read_bio_RSAPublicKey(bio, nullptr, nullptr, nullptr);
            if (rsa == nullptr)
                throw std::runtime_error("PEM_read_bio_RSAPublicKey() failure. "
                        "Code=" + std::to_string(ERR_get_error()));

            rsaSize = RSA_size(rsa);
            BIO_free_all(bio);
        } // `bio` allocated
        catch (const std::exception& ex) {
            BIO_free_all(bio);
            throw;
        }
#   else

#   endif
}

void PublicKey::encrypt(const std::string& plainText,
                        std::string&       cipherText) const
{
    char       buf[rsaSize];
    const auto cipherLen = RSA_public_encrypt(plainText.length(),
            reinterpret_cast<const unsigned char*>(plainText.data()),
            reinterpret_cast<unsigned char*>(buf), rsa, padding);
    if (cipherLen == -1)
        throw std::runtime_error("RSA_public_encrypt() failure. "
                "Code=" + std::to_string(ERR_get_error()));

    cipherText.assign(buf, cipherLen);
}

void PublicKey::decrypt(const std::string& cipherText,
                        std::string&       plainText) const
{
    char      buf[rsaSize];
    const int plainLen = ::RSA_public_decrypt(cipherText.length(),
            reinterpret_cast<const unsigned char*>(cipherText.data()),
            reinterpret_cast<unsigned char*>(buf), rsa, padding);
    if (plainLen == -1)
        throw std::runtime_error("RSA_public_decrypt() failure. "
                "Code=" + std::to_string(ERR_get_error()));

    plainText.assign(buf, plainLen);
}

/******************************************************************************/

PrivateKey::PrivateKey()
    : PubKeyCrypt{}
{
#   if USE_RSA
        BIGNUM* bigNum = ::BN_new();
        if (bigNum == nullptr)
            throw std::runtime_error("BN_new() failure. "
                    "Code=" + std::to_string(ERR_get_error()));

        try {
            if (::BN_set_word(bigNum, RSA_F4) == 0)
                throw std::runtime_error("BN_set_word() failure. "
                        "Code=" + std::to_string(ERR_get_error()));

            rsa = ::RSA_new();
            if (rsa == nullptr)
                throw std::runtime_error("RSA_new() failure. "
                        "Code=" + std::to_string(ERR_get_error()));

            try {
                const int numBits = 2048;

                SslHelp::initRand(numBits/8);
                if (::RSA_generate_key_ex(rsa, numBits, bigNum, NULL) == 0)
                    throw std::runtime_error("RSA_generate_key_ex() failure. "
                            "Code=" + std::to_string(ERR_get_error()));

                rsaSize = RSA_size(rsa);
                BIO* bio = ::BIO_new(BIO_s_mem());
                if (bio == nullptr)
                    throw std::runtime_error("BIO_new() failure. "
                            "Code=" + std::to_string(ERR_get_error()));

                try {
                    // Doesn't NUL-terminate
                    if (::PEM_write_bio_RSAPublicKey(bio, rsa) == 0)
                        throw std::runtime_error(
                                "PEM_write_bio_RSAPublicKey() failure. "
                                "Code=" + std::to_string(ERR_get_error()));

                    const size_t keyLen = BIO_pending(bio);
                    char         keyBuf[keyLen]; // NB: No trailing NUL

                    if (::BIO_read(bio, keyBuf, keyLen) != keyLen)
                        throw std::runtime_error("BIO_read() failure. "
                                "Code=" + std::to_string(ERR_get_error()));

                    // Finally!
                    pubKey = std::string(static_cast<char*>(keyBuf), keyLen);

                    ::BIO_free_all(bio);
                } // `bio` allocated
                catch (const std::exception& ex) {
                    ::BIO_free_all(bio);
                    throw;
                }
            } // `rsa` allocated
            catch (const std::exception& ex) {
                ::RSA_free(rsa);
                throw;
            }

            ::BN_free(bigNum);
        } // `bigNum` allocated
        catch (const std::exception& ex) {
            ::BN_free(bigNum);
            throw;
        }
#   else
        EC_builtin_curve curve;

        EC_get_builtin_curves(&curve, 1);
        EC_KEY* ecKey = EC_KEY_new_by_curve_name(curve.nid);
        if (ecKey == nullptr)
            throw std::runtime_error("EC_KEY_new_by_curve_name() failure");

        try {
            EC_POINT ecPoint;
            EC_KEY_get0_public_key(&ecPoint);
        } // `ecKey` allocated
        catch (...) {
            EC_KEY_free(ecKey);
            throw;
        }
#   endif
}

const std::string& PrivateKey::getPubKey() const noexcept
{
    return pubKey;
}

int PrivateKey::encrypt(const char* plainText,
                        const int   plainLen,
                        char*       cipherText,
                        int         cipherLen) const
{
    if (cipherLen < rsaSize)
        throw std::invalid_argument(std::to_string(cipherLen) + "-byte "
                "ciphertext buffer is smaller than " +
                std::to_string(rsaSize) + " bytes");

    cipherLen = RSA_private_encrypt(plainLen,
            reinterpret_cast<const unsigned char*>(plainText),
            reinterpret_cast<unsigned char*>(cipherText), rsa, padding);
    if (cipherLen == -1)
        throw std::runtime_error("(RSA_private_encrypt) failure. "
                "Code=" + std::to_string(ERR_get_error()));

    return cipherLen;
}

void PrivateKey::encrypt(const std::string& plainText,
                         std::string&       cipherText) const
{
    char       buf[rsaSize];
    const auto cipherLen = encrypt(
            reinterpret_cast<const char*>(plainText.data()),
            static_cast<int>(plainText.length()),
            reinterpret_cast<char*>(buf), rsaSize);

    cipherText.assign(buf, cipherLen);
}

void PrivateKey::decrypt(const std::string& cipherText,
                         std::string&       plainText) const
{
    char      buf[RSA_size(rsa)];
    const int plainLen = ::RSA_private_decrypt(cipherText.length(),
            reinterpret_cast<const unsigned char*>(cipherText.data()),
            reinterpret_cast<unsigned char*>(buf), rsa, padding);
    if (plainLen == -1)
        throw std::runtime_error("RSA_private_decrypt() failure. "
                "Code=" + std::to_string(ERR_get_error()));

    plainText.assign(buf, plainLen);
}
