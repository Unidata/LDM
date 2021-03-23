/**
 * Copyright (C) 2021 University of Virginia. All rights reserved.
 *
 * @file      mac.cpp
 * @author    Steven R. Emmerson <emmerson@ucar.edu>
 * @version   1.0
 * @date      Mar 12, 2021
 *
 * @section   LICENSE
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @brief     Message authentication module
 */

#include "FmtpConfig.h"

#include "Ed25519.h"
#include "mac.h"
#include "SslHelp.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <openssl/rand.h>
#include <stdexcept>

class Mac::Impl
{
protected:
    /**
     * Constructs.
     *
     * @param[in] macLen  Length of a MAC in bytes
     */
    Impl(const unsigned macLen)
        : MAC_LEN{macLen}
    {}

public:
    const unsigned MAC_LEN; ///< MAC length in bytes

    /**
     * Destroys.
     */
    virtual ~Impl()
    {}

    /**
     * Returns the MAC key.
     *
     * @return  MAC key
     */
    virtual std::string getKey() const =0;

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
    virtual size_t getMac(const char*  msg,
                          const size_t msgLen,
                          char*        mac,
                          const size_t macLen) =0;

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
    virtual bool verify(const char* msg,
                        size_t      msgLen,
                        const char* mac,
                        size_t      macLen) =0;
};

class NoMac final : public Mac::Impl
{
public:
    static const unsigned MAC_SIZE = 0;

    /**
     * Constructs.
     */
    NoMac()
        : Impl{MAC_SIZE}
    {}

    /**
     * Constructs.
     *
     * @param[in] key                MAC key from `getKey()`
     * @throw std::invalid_argument  MAC key is not the empty string
     */
    NoMac(const std::string& key)
        : Impl(0)
    {
        if (key.size())
            throw std::invalid_argument("MAC key isn't empty string: \"" +
                    key + "\"");
    }

    /**
     * Returns the empty string.
     *
     * @return  Empty string
     */
    std::string getKey() const override {
        return std::string();
    }

    /**
     * Does nothing.
     *
     * @param[in]     msg         Message
     * @param[in]     msgLen      Length of message in bytes
     * @param[out]    mac         Buffer for MAC of message
     * @param[in]     macLen      Length of MAC buffer in bytes
     * @return                    0
     */
    size_t getMac(const char*   msg,
                  const size_t  msgLen,
                  char*         mac,
                  const size_t  macLen) override {
        return 0;
    }

    /**
     * Does nothing.
     *
     * @param[in]  msg            Message
     * @param[in]  msgLen         Length of message in bytes
     * @param[in]  mac            MAC to be verified
     * @param[in]  macLen         Length of MAC in bytes
     * @return     true           MAC length is zero
     * @return     false          MAC length is not zero
     */
    bool verify(const char*  msg,
                const size_t msgLen,
                const char*  mac,
                const size_t macLen) override {
        return macLen == 0;
    }
};

class Hmac final : public Mac::Impl
{
private:
    std::string      key;            ///< HMAC key
    EVP_PKEY*        pKey;           ///< OpenSSL HMAC key
    EVP_MD_CTX*      mdCtx;          ///< Message-digest context

    /**
     * Vets the size of the HMAC key.
     *
     * @param[in] key                 HMAC key
     * @throws std::invalid_argument  `key.size() < 2*SIZE`
     */
    static void vetKeySize(const std::string& key)
    {
        if (key.size() < 2*MAC_SIZE) // Double hash size => more secure
            throw std::invalid_argument("key.size()=" +
                    std::to_string(key.size()));
    }

    /**
     * Assigns members `key`, `pkey`, and `mdCtx` after vetting the size of the
     * HMAC key.
     *
     * @param[in] key                HMAC key
     * @throw std::invalid_argument  `key.size() < 2*SIZE`
     * @throw std::runtime_error     OpenSSL failure
     */
    void init(const std::string& key)
    {
        vetKeySize(key);
        this->key = key;

        pKey = EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC, NULL,
                reinterpret_cast<const unsigned char*>(key.data()), key.size());
        if (pKey == nullptr)
            SslHelp::throwOpenSslError("EVP_PKEY_new_raw_private_key() failure");

        mdCtx = EVP_MD_CTX_new();
        if (mdCtx == NULL)
            SslHelp::throwOpenSslError("EVP_MD_CTX_new() failure");
    }

public:
    static const unsigned MAC_SIZE = 32;   ///< HMAC size in bytes

    /**
     * Constructs. Returned instance is appropriate for a signer.
     *
     * @throw std::runtime_error  OpenSSL failure
     */
    Hmac()
        : Impl{MAC_SIZE}
        , key{}
        , pKey{nullptr}
        , mdCtx{nullptr}
    {
        unsigned char bytes[2*MAC_SIZE];

        SslHelp::initRand(sizeof(bytes));
        if (RAND_bytes(bytes, sizeof(bytes)) == 0)
            SslHelp::throwOpenSslError("RAND_bytes() failure");

        init(std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes)));
    }

    /**
     * Constructs. Returns instance is appropriate for a verifier.
     *
     * @param[in] key                HMAC key from `getKey()`
     * @throw std::invalid_argument  `key.size() < 2*MAC_LEN`
     * @throw std::runtime_error     OpenSSL failure
     */
    Hmac(const std::string& key)
        : Impl{MAC_SIZE}
        , key{}
        , pKey{nullptr}
        , mdCtx{nullptr}
    {
        init(key);
    }

    /**
     * Returns the HMAC key.
     *
     * @return  HMAC key
     */
    std::string getKey() const override {
        return this->key;
    }

    /**
     * Returns the HMAC of a message.
     *
     * @param[in]     msg         Message
     * @param[in]     msgLen      Length of message in bytes
     * @param[out]    mac         Buffer for HMAC of message
     * @param[in]     macLen      Length of HMAC buffer in bytes
     * @return                    Number of bytes written to HMAC buffer
     * @throw std::runtime_error  OpenSSL Failure
     */
    size_t getMac(const char* msg,
                  size_t      msgLen,
                  char*       mac,
                  size_t      macLen) override {
        if (EVP_DigestSignInit(mdCtx, NULL, EVP_sha256(), NULL, pKey) != 1)
            SslHelp::throwOpenSslError("EVP_DigestSignInit() failure");

        if (!EVP_DigestSign(mdCtx,
                reinterpret_cast<unsigned char*>(mac), &macLen,
                reinterpret_cast<const unsigned char*>(msg), msgLen))
            SslHelp::throwOpenSslError("EVP_DigestSign() failure");
        assert(MAC_SIZE == macLen);

        return macLen;
    }

    /**
     * Verifies an HMAC.
     *
     * @param[in]  msg            Message
     * @param[in]  msgLen         Length of message in bytes
     * @param[in]  mac            HMAC to be verified
     * @param[in]  macLen         Length of MAC in bytes
     * @retval     true           HMAC is verified
     * @retval     false          HMAC is not verified
     * @throw std::runtime_error  OpenSSL Failure
     */
    bool verify(const char*  msg,
                const size_t msgLen,
                const char*  mac,
                const size_t macLen) override {
        //(void)EVP_MD_CTX_reset(mdCtx);
        if (EVP_DigestSignInit(mdCtx, NULL, EVP_sha256(), NULL, pKey) != 1)
            SslHelp::throwOpenSslError("EVP_DigestSignInit() failure");

        unsigned char compMac[EVP_MAX_MD_SIZE];
        size_t        compMacLen = EVP_MAX_MD_SIZE;
        if (!EVP_DigestSign(mdCtx, compMac, &compMacLen,
                reinterpret_cast<const unsigned char*>(msg), msgLen))
            SslHelp::throwOpenSslError("EVP_DigestSign() failure");

        return (compMacLen == macLen) &&
                (::memcmp(compMac, mac, macLen) == 0);
    }
};

class Dsa final : public Mac::Impl
{
    Ed25519 digSig; ///< Ed25519 digital signer/verifier

public:
    static const unsigned MAC_SIZE = Ed25519::SIGLEN;

    /**
     * Constructs. Returned instance is appropriate for a signer.
     *
     * @throw std::runtime_error  OpenSSL failure
     */
    Dsa()
        : Impl{MAC_SIZE}
    {}

    /**
     * Constructs. Returns instance is appropriate for a verifier.
     *
     * @param[in] key                DSA key from `getKey()`
     * @throw std::runtime_error     OpenSSL failure
     */
    Dsa(const std::string& key)
        : Impl{MAC_SIZE}
        , digSig{key}
    {}

    /**
     * Returns the DSA key.
     *
     * @return  DSA key
     */
    std::string getKey() const override {
        return digSig.getPubKey();
    }

    /**
     * Returns the DSA of a message.
     *
     * @param[in]     msg         Message
     * @param[in]     msgLen      Length of message in bytes
     * @param[out]    mac         Buffer for MAC of message
     * @param[in]     macLen      Length of MAC buffer in bytes
     * @return                    Number of bytes written to MAC buffer
     * @throw std::runtime_error  OpenSSL Failure
     */
    size_t getMac(const char*  msg,
                  const size_t msgLen,
                  char*        mac,
                  const size_t macLen) override {
        return digSig.sign(msg, msgLen, mac, macLen);
    }

    /**
     * Verifies a MAC.
     *
     * @param[in]  msg            Message
     * @param[in]  msgLen         Length of message in bytes
     * @param[in]  mac            MAC to be verified
     * @param[in]  macLen         Length of MAC in bytes
     * @retval     true           MAC is verified
     * @retval     false          MAC is not verified
     * @throw std::runtime_error  OpenSSL Failure
     */
    bool verify(const char*  msg,
                const size_t msgLen,
                const char*  mac,
                const size_t macLen) override {
        return digSig.verify(msg, msgLen, mac, macLen);
    }
};

const char* Mac::ENV_NAME = "FMTP_MAC_LEVEL";

static int getMacLevel()
{
    const char* envStr = ::getenv(Mac::ENV_NAME);
    if (envStr == nullptr)
        envStr = "0";

    errno = 0;
    char* end;
    auto level = ::strtol(envStr, &end, 0);

    if (errno || *end || level < 0 || level > 2)
        throw std::runtime_error(std::string("Environment variable ") +
                Mac::ENV_NAME + " has an invalid value: " + envStr);

    return level;
}

unsigned Mac::getSize()
{
    switch (getMacLevel()) {
    default:
    case 0:
        return NoMac::MAC_SIZE;
    case 1:
        return Hmac::MAC_SIZE;
    case 2:
        return Dsa::MAC_SIZE;
    }
}

Mac::Mac()
    : pImpl{}
{
    switch (getMacLevel()) {
    default:
    case 0:
        pImpl = Pimpl{new NoMac()};
        break;
    case 1:
        pImpl = Pimpl{new Hmac()};
        break;
    case 2:
        pImpl = Pimpl{new Dsa()};
        break;
    }
}

Mac::Mac(const std::string& key)
    : pImpl{}
{
    switch (getMacLevel()) {
    default:
    case 0:
        pImpl = Pimpl{new NoMac(key)};
        break;
    case 1:
        pImpl = Pimpl{new Hmac(key)};
        break;
    case 2:
        pImpl = Pimpl{new Dsa(key)};
        break;
    }
}

Mac::~Mac()
{}

std::string Mac::getKey() const
{
    return pImpl->getKey();
}

size_t Mac::getMac(const char*  msg,
                   const size_t msgLen,
                   char*        mac,
                   const size_t macLen) const
{
    return pImpl->getMac(msg, msgLen, mac, macLen);
}

std::string Mac::getMac(const std::string& msg) const
{
    char         mac[pImpl->MAC_LEN];
    const size_t macLen = pImpl->getMac(msg.data(), msg.size(), mac,
            sizeof(mac));

    return std::string(mac, macLen);
}

bool Mac::verify(const char*  msg,
                 const size_t msgLen,
                 const char*  mac,
                 const size_t macLen) const
{
    return pImpl->verify(msg, msgLen, mac, macLen);
}

bool Mac::verify(const std::string& msg,
                 const std::string& mac) const
{
    return pImpl->verify(msg.data(), msg.size(), mac.data(), mac.size());
}
